#include "builtins.h"
#include "parser.h"
#include "process.h"
#include "shell.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

static int execute_one(const Command *command, ShellState *state)
{
    int handled = 0;
    int status = run_builtin(command, state, &handled);
    if (handled) {
        return status;
    }
    return run_external_command(command->argv, &state->nofile_limit);
}

static void execute_line(const CommandLine *line, ShellState *state)
{
    for (size_t i = 0; i < line->command_count; ++i) {
        if (i > 0) {
            Connector connector = line->connectors[i - 1];
            if (connector == CONNECT_AND && state->last_status != 0) {
                continue;
            }
            if (connector == CONNECT_OR && state->last_status == 0) {
                continue;
            }
            /* CONNECT_SEQUENCE always executes the next command. */
        }

        state->last_status = execute_one(&line->commands[i], state);
        if (state->should_exit) {
            break;
        }
    }
}

int main(void)
{
    ShellState state = {0};
    if (getrlimit(RLIMIT_NOFILE, &state.nofile_limit) == -1) {
        perror("vtsh: getrlimit(RLIMIT_NOFILE)");
        return 1;
    }
    state.nofile_hard_ceiling = state.nofile_limit.rlim_max;

    /* Keep the shell alive on Ctrl-C; the child restores the default disposition. */
    struct sigaction ignore_action;
    memset(&ignore_action, 0, sizeof(ignore_action));
    ignore_action.sa_handler = SIG_IGN;
    sigemptyset(&ignore_action.sa_mask);
    if (sigaction(SIGINT, &ignore_action, NULL) == -1) {
        perror("vtsh: sigaction(SIGINT)");
        return 1;
    }

    const int interactive = isatty(STDIN_FILENO);
    char *line_buffer = NULL;
    size_t line_capacity = 0;

    while (!state.should_exit) {
        if (interactive) {
            fputs("vtsh$ ", stdout);
            fflush(stdout);
        }

        errno = 0;
        ssize_t length = getline(&line_buffer, &line_capacity, stdin);
        if (length < 0) {
            if (feof(stdin)) {
                break;
            }
            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }
            perror("vtsh: getline");
            state.last_status = 1;
            break;
        }

        CommandLine parsed = {0};
        char error[256] = {0};
        if (parse_line(line_buffer, &parsed, error, sizeof(error)) != 0) {
            fprintf(stderr, "vtsh: синтаксическая ошибка: %s\n", error);
            state.last_status = 2;
            continue;
        }

        execute_line(&parsed, &state);
        free_command_line(&parsed);
        if (fflush(stdout) == EOF) {
            perror("vtsh: fflush(stdout)");
            state.last_status = 1;
            break;
        }
    }

    free(line_buffer);
    return state.should_exit ? state.exit_status : state.last_status;
}
