#include "process.h"

#include <errno.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHILD_STACK_SIZE (1024U * 1024U)

extern char **environ;

typedef struct {
    char *const *argv;
    struct rlimit nofile_limit;
} ChildContext;

/*
 * Search PATH without execvp()/execvpe(): execve() is used directly so an
 * executable text file without a shebang is reported as ENOEXEC instead of
 * being silently interpreted by /bin/sh.
 */
static void exec_search_path(char *const argv[])
{
    const char *name = argv[0];
    if (strchr(name, '/') != NULL) {
        execve(name, argv, environ);
        return;
    }

    const char *path = getenv("PATH");
    if (path == NULL) {
        path = "/usr/local/bin:/usr/bin:/bin";
    }

    int saw_eacces = 0;
    int saw_enametoolong = 0;
    const char *directory = path;
    for (;;) {
        const char *end = strchr(directory, ':');
        size_t length = end != NULL ? (size_t)(end - directory) : strlen(directory);
        char candidate[PATH_MAX];
        int written = snprintf(candidate, sizeof(candidate), "%.*s%s%s",
                               (int)length, directory,
                               length > 0 ? "/" : "", name);

        if (written < 0 || (size_t)written >= sizeof(candidate)) {
            saw_enametoolong = 1;
        } else {
            execve(candidate, argv, environ);
            if (errno == EACCES) {
                saw_eacces = 1;
            } else if (errno == ENAMETOOLONG) {
                saw_enametoolong = 1;
            } else if (errno != ENOENT && errno != ENOTDIR) {
                return;
            }
        }

        if (end == NULL) {
            break;
        }
        directory = end + 1;
    }

    if (saw_eacces) {
        errno = EACCES;
    } else if (saw_enametoolong) {
        errno = ENAMETOOLONG;
    } else {
        errno = ENOENT;
    }
}

static int child_entry(void *argument)
{
    ChildContext *context = argument;

    /* The shell ignores Ctrl-C; foreground commands should receive the default action. */
    struct sigaction default_action;
    memset(&default_action, 0, sizeof(default_action));
    default_action.sa_handler = SIG_DFL;
    sigemptyset(&default_action.sa_mask);
    (void)sigaction(SIGINT, &default_action, NULL);
    (void)sigaction(SIGQUIT, &default_action, NULL);

    if (setrlimit(RLIMIT_NOFILE, &context->nofile_limit) == -1) {
        dprintf(STDERR_FILENO, "vtsh: setrlimit(RLIMIT_NOFILE): %s\n", strerror(errno));
        _exit(125);
    }

    exec_search_path((char *const *)context->argv);

    int saved_errno = errno;
    dprintf(STDERR_FILENO, "vtsh: не удалось выполнить '%s': %s\n",
            context->argv[0], strerror(saved_errno));
    _exit(saved_errno == ENOENT || saved_errno == ENOTDIR ? 127 : 126);
}

static int status_from_wait(int wait_status)
{
    if (WIFEXITED(wait_status)) {
        return WEXITSTATUS(wait_status);
    }
    if (WIFSIGNALED(wait_status)) {
        return 128 + WTERMSIG(wait_status);
    }
    return 125;
}

int run_external_command(char *const argv[], const struct rlimit *nofile_limit)
{
    /* Preserve the order of builtin output and child-process output when stdout is piped. */
    if (fflush(NULL) == EOF) {
        perror("vtsh: fflush");
        return 125;
    }

    void *stack = malloc(CHILD_STACK_SIZE);
    if (stack == NULL) {
        perror("vtsh: malloc для стека clone");
        return 125;
    }

    ChildContext context = {
        .argv = argv,
        .nofile_limit = *nofile_limit
    };

    uintptr_t stack_top = (uintptr_t)stack + CHILD_STACK_SIZE;
    stack_top &= ~(uintptr_t)0x0f;

    pid_t child_pid = clone(child_entry, (void *)stack_top, SIGCHLD, &context);
    if (child_pid == -1) {
        int saved_errno = errno;
        free(stack);
        errno = saved_errno;
        perror("vtsh: clone");
        return 125;
    }

    /*
     * SIGCHLD makes this a waitable child for waitpid(..., 0).
     * No CLONE_VM is used, so the child has a copy-on-write address space;
     * freeing the parent's stack mapping cannot invalidate the child's stack.
     */
    free(stack);

    int wait_status = 0;
    for (;;) {
        pid_t waited = waitpid(child_pid, &wait_status, 0);
        if (waited == child_pid) {
            break;
        }
        if (waited == -1 && errno == EINTR) {
            continue;
        }
        if (waited == -1) {
            perror("vtsh: waitpid");
            return 125;
        }
    }

    return status_from_wait(wait_status);
}
