#ifndef VTSH_PROCESS_H
#define VTSH_PROCESS_H

#include <sys/resource.h>

int run_external_command(char *const argv[], const struct rlimit *nofile_limit);

#endif
