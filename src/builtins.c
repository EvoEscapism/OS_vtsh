#include "builtins.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_limit_value(rlim_t value)
{
    if (value == RLIM_INFINITY) {
        (void)printf("unlimited");
    } else {
        (void)printf("%llu", (unsigned long long)value);
    }
}

static int parse_rlim_value(const char *text, rlim_t *value)
{
    if (strcmp(text, "unlimited") == 0) {
        *value = RLIM_INFINITY;
        return 0;
    }

    if (text[0] == '-' || text[0] == '\0') {
        return -1;
    }

    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    rlim_t converted = (rlim_t)parsed;
    if ((unsigned long long)converted != parsed) {
        return -1;
    }
    *value = converted;
    return 0;
}

static int builtin_cd(const Command *command)
{
    if (command->argc > 2) {
        fprintf(stderr, "vtsh: cd: ожидалось не более одного аргумента\n");
        return 2;
    }

    const char *destination = command->argc == 2 ? command->argv[1] : getenv("HOME");
    if (destination == NULL || destination[0] == '\0') {
        fprintf(stderr, "vtsh: cd: переменная HOME не задана\n");
        return 1;
    }

    char *old_directory = getcwd(NULL, 0);
    if (chdir(destination) == -1) {
        fprintf(stderr, "vtsh: cd: %s: %s\n", destination, strerror(errno));
        free(old_directory);
        return 1;
    }

    char *new_directory = getcwd(NULL, 0);
    if (new_directory == NULL) {
        fprintf(stderr, "vtsh: cd: каталог изменён, но getcwd не удался: %s\n",
                strerror(errno));
        free(old_directory);
        return 1;
    }

    int result = 0;
    if (old_directory != NULL && setenv("OLDPWD", old_directory, 1) == -1) {
        fprintf(stderr, "vtsh: cd: не удалось обновить OLDPWD: %s\n", strerror(errno));
        result = 1;
    }
    if (setenv("PWD", new_directory, 1) == -1) {
        fprintf(stderr, "vtsh: cd: не удалось обновить PWD: %s\n", strerror(errno));
        result = 1;
    }

    free(old_directory);
    free(new_directory);
    return result;
}

static int builtin_exit(const Command *command, ShellState *state)
{
    if (command->argc > 2) {
        fprintf(stderr, "vtsh: exit: ожидался один числовой аргумент\n");
        return 2;
    }

    int code = state->last_status;
    if (command->argc == 2) {
        errno = 0;
        char *end = NULL;
        long parsed = strtol(command->argv[1], &end, 10);
        if (errno != 0 || end == command->argv[1] || *end != '\0') {
            fprintf(stderr, "vtsh: exit: аргумент должен быть целым числом\n");
            return 2;
        }
        code = (unsigned char)parsed;
    }

    state->should_exit = true;
    state->exit_status = code;
    return code;
}

static int builtin_limit(const Command *command, ShellState *state)
{
    if (command->argc == 2 && strcmp(command->argv[1], "nofile") == 0) {
        printf("RLIMIT_NOFILE: soft=");
        print_limit_value(state->nofile_limit.rlim_cur);
        printf(" hard=");
        print_limit_value(state->nofile_limit.rlim_max);
        putchar('\n');
        return 0;
    }

    if ((command->argc != 3 && command->argc != 4) ||
        strcmp(command->argv[1], "nofile") != 0) {
        fprintf(stderr, "Использование: limit nofile [soft [hard]]\n");
        return 2;
    }

    struct rlimit requested = state->nofile_limit;
    if (parse_rlim_value(command->argv[2], &requested.rlim_cur) != 0) {
        fprintf(stderr, "vtsh: limit: неверное значение soft limit: %s\n", command->argv[2]);
        return 2;
    }
    if (command->argc == 4) {
        if (parse_rlim_value(command->argv[3], &requested.rlim_max) != 0) {
            fprintf(stderr, "vtsh: limit: неверное значение hard limit: %s\n", command->argv[3]);
            return 2;
        }
    } else {
        /* A one-value form is a strict cap: lower soft and hard together. */
        requested.rlim_max = requested.rlim_cur;
    }

    if (requested.rlim_cur > requested.rlim_max) {
        fprintf(stderr, "vtsh: limit: soft limit не может быть больше hard limit\n");
        return 2;
    }
    if (requested.rlim_max > state->nofile_hard_ceiling) {
        fprintf(stderr, "vtsh: limit: нельзя поднять hard limit выше исходного значения процесса shell\n");
        return 2;
    }

    state->nofile_limit = requested;
    printf("Для следующих внешних программ установлен RLIMIT_NOFILE: soft=");
    print_limit_value(requested.rlim_cur);
    printf(" hard=");
    print_limit_value(requested.rlim_max);
    putchar('\n');
    return 0;
}

static int builtin_help(void)
{
    puts("VTSH — учебная оболочка\n"
         "Внешние программы можно запускать по имени через PATH или по пути, например /bin/echo hello.\n"
         "Встроенные команды:\n"
         "  cd [каталог]             изменить текущий каталог\n"
         "  exit [код]               завершить оболочку\n"
         "  limit nofile             показать лимит для запускаемых программ\n"
         "  limit nofile SOFT [HARD] настроить RLIMIT_NOFILE (без HARD оба лимита равны SOFT)\n"
         "  help                     показать эту справку\n"
         "Операторы: &&, ||, ;\n"
         "Ограничения: нет кавычек, подстановок, конвейеров и фонового запуска.");
    return 0;
}

int run_builtin(const Command *command, ShellState *state, int *handled)
{
    *handled = 1;
    if (strcmp(command->argv[0], "cd") == 0) {
        return builtin_cd(command);
    }
    if (strcmp(command->argv[0], "exit") == 0) {
        return builtin_exit(command, state);
    }
    if (strcmp(command->argv[0], "limit") == 0) {
        return builtin_limit(command, state);
    }
    if (strcmp(command->argv[0], "help") == 0) {
        if (command->argc != 1) {
            fprintf(stderr, "vtsh: help: аргументы не поддерживаются\n");
            return 2;
        }
        return builtin_help();
    }

    *handled = 0;
    return 0;
}
