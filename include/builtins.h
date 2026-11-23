#ifndef VTSH_BUILTINS_H
#define VTSH_BUILTINS_H

#include "parser.h"
#include "shell.h"

/* Sets *handled to true for a shell built-in; returns its status. */
int run_builtin(const Command *command, ShellState *state, int *handled);

#endif
