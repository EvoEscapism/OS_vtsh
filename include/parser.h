#ifndef VTSH_PARSER_H
#define VTSH_PARSER_H

#include <stddef.h>

typedef enum {
    CONNECT_AND,
    CONNECT_OR,
    CONNECT_SEQUENCE
} Connector;

typedef struct {
    char **argv;
    size_t argc;
} Command;

typedef struct {
    Command *commands;
    Connector *connectors;
    size_t command_count;
} CommandLine;

/* Returns 0 on success (including an empty line), -1 on syntax/allocation error. */
int parse_line(const char *input, CommandLine *result, char *error, size_t error_size);
void free_command_line(CommandLine *line);

#endif
