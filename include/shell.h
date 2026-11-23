#ifndef VTSH_SHELL_H
#define VTSH_SHELL_H

#include <stdbool.h>
#include <sys/resource.h>

typedef struct {
    struct rlimit nofile_limit;
    rlim_t nofile_hard_ceiling;
    int last_status;
    bool should_exit;
    int exit_status;
} ShellState;

#endif
