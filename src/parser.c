#include "parser.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TOKEN_WORD,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_SEQUENCE,
    TOKEN_PIPE,
    TOKEN_BACKGROUND
} TokenType;

typedef struct {
    TokenType type;
    char *text;
} Token;

typedef struct {
    Token *items;
    size_t count;
} TokenList;

static int set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
    return -1;
}

static void free_tokens(TokenList *tokens)
{
    if (tokens == NULL) {
        return;
    }
    for (size_t i = 0; i < tokens->count; ++i) {
        free(tokens->items[i].text);
    }
    free(tokens->items);
    tokens->items = NULL;
    tokens->count = 0;
}

static int push_token(TokenList *tokens, TokenType type, const char *start, size_t length)
{
    Token *new_items = realloc(tokens->items, (tokens->count + 1) * sizeof(*new_items));
    if (new_items == NULL) {
        return -1;
    }
    tokens->items = new_items;

    char *text = NULL;
    if (type == TOKEN_WORD) {
        text = malloc(length + 1);
        if (text == NULL) {
            return -1;
        }
        memcpy(text, start, length);
        text[length] = '\0';
    }

    tokens->items[tokens->count].type = type;
    tokens->items[tokens->count].text = text;
    tokens->count++;
    return 0;
}

static int tokenize(const char *input, TokenList *tokens, char *error, size_t error_size)
{
    const char *cursor = input;

    while (*cursor != '\0') {
        if (isspace((unsigned char)*cursor)) {
            ++cursor;
            continue;
        }

        if (*cursor == '&') {
            if (cursor[1] == '&') {
                if (push_token(tokens, TOKEN_AND, NULL, 0) != 0) {
                    return set_error(error, error_size, "не удалось выделить память для токенов");
                }
                cursor += 2;
            } else {
                if (push_token(tokens, TOKEN_BACKGROUND, NULL, 0) != 0) {
                    return set_error(error, error_size, "не удалось выделить память для токенов");
                }
                ++cursor;
            }
            continue;
        }

        if (*cursor == '|') {
            if (cursor[1] == '|') {
                if (push_token(tokens, TOKEN_OR, NULL, 0) != 0) {
                    return set_error(error, error_size, "не удалось выделить память для токенов");
                }
                cursor += 2;
            } else {
                if (push_token(tokens, TOKEN_PIPE, NULL, 0) != 0) {
                    return set_error(error, error_size, "не удалось выделить память для токенов");
                }
                ++cursor;
            }
            continue;
        }

        if (*cursor == ';') {
            if (push_token(tokens, TOKEN_SEQUENCE, NULL, 0) != 0) {
                return set_error(error, error_size, "не удалось выделить память для токенов");
            }
            ++cursor;
            continue;
        }

        const char *word_start = cursor;
        while (*cursor != '\0' && !isspace((unsigned char)*cursor) &&
               *cursor != '&' && *cursor != '|' && *cursor != ';') {
            ++cursor;
        }
        if (push_token(tokens, TOKEN_WORD, word_start, (size_t)(cursor - word_start)) != 0) {
            return set_error(error, error_size, "не удалось выделить память для аргумента");
        }
    }

    return 0;
}

static int append_command(CommandLine *line, Command *command)
{
    Command *new_commands = realloc(line->commands,
                                    (line->command_count + 1) * sizeof(*new_commands));
    if (new_commands == NULL) {
        return -1;
    }
    line->commands = new_commands;
    line->commands[line->command_count] = *command;
    line->command_count++;
    command->argv = NULL;
    command->argc = 0;
    return 0;
}

static int append_connector(CommandLine *line, Connector connector)
{
    Connector *new_connectors = realloc(line->connectors,
                                        line->command_count * sizeof(*new_connectors));
    if (new_connectors == NULL) {
        return -1;
    }
    line->connectors = new_connectors;
    line->connectors[line->command_count - 1] = connector;
    return 0;
}

static void free_command(Command *command)
{
    if (command == NULL || command->argv == NULL) {
        return;
    }
    for (size_t i = 0; i < command->argc; ++i) {
        free(command->argv[i]);
    }
    free(command->argv);
    command->argv = NULL;
    command->argc = 0;
}

void free_command_line(CommandLine *line)
{
    if (line == NULL) {
        return;
    }
    for (size_t i = 0; i < line->command_count; ++i) {
        free_command(&line->commands[i]);
    }
    free(line->commands);
    free(line->connectors);
    line->commands = NULL;
    line->connectors = NULL;
    line->command_count = 0;
}

int parse_line(const char *input, CommandLine *result, char *error, size_t error_size)
{
    TokenList tokens = {0};
    result->commands = NULL;
    result->connectors = NULL;
    result->command_count = 0;

    if (tokenize(input, &tokens, error, error_size) != 0) {
        free_tokens(&tokens);
        return -1;
    }

    if (tokens.count == 0) {
        free_tokens(&tokens);
        return 0;
    }

    size_t index = 0;
    while (index < tokens.count) {
        if (tokens.items[index].type != TOKEN_WORD) {
            free_tokens(&tokens);
            free_command_line(result);
            return set_error(error, error_size, "ожидалась команда перед оператором");
        }

        Command command = {0};
        while (index < tokens.count && tokens.items[index].type == TOKEN_WORD) {
            char **new_argv = realloc(command.argv, (command.argc + 2) * sizeof(*new_argv));
            if (new_argv == NULL) {
                free_command(&command);
                free_tokens(&tokens);
                free_command_line(result);
                return set_error(error, error_size, "не удалось выделить память для аргументов");
            }
            command.argv = new_argv;
            command.argv[command.argc] = strdup(tokens.items[index].text);
            if (command.argv[command.argc] == NULL) {
                free_command(&command);
                free_tokens(&tokens);
                free_command_line(result);
                return set_error(error, error_size, "не удалось скопировать аргумент");
            }
            command.argc++;
            command.argv[command.argc] = NULL;
            ++index;
        }

        if (append_command(result, &command) != 0) {
            free_command(&command);
            free_tokens(&tokens);
            free_command_line(result);
            return set_error(error, error_size, "не удалось добавить команду");
        }

        if (index == tokens.count) {
            break;
        }

        TokenType operator_type = tokens.items[index].type;
        Connector connector;
        if (operator_type == TOKEN_AND) {
            connector = CONNECT_AND;
        } else if (operator_type == TOKEN_OR) {
            connector = CONNECT_OR;
        } else if (operator_type == TOKEN_SEQUENCE) {
            connector = CONNECT_SEQUENCE;
        } else if (operator_type == TOKEN_PIPE) {
            free_tokens(&tokens);
            free_command_line(result);
            return set_error(error, error_size,
                             "конвейеры '|' не входят в вариант этой лабораторной работы");
        } else {
            free_tokens(&tokens);
            free_command_line(result);
            return set_error(error, error_size,
                             "фоновый оператор '&' не входит в вариант этой лабораторной работы");
        }

        /* A trailing semicolon is a harmless command terminator, as in Bash. */
        if (index + 1 == tokens.count && operator_type == TOKEN_SEQUENCE) {
            ++index;
            break;
        }

        if (append_connector(result, connector) != 0) {
            free_tokens(&tokens);
            free_command_line(result);
            return set_error(error, error_size, "не удалось добавить оператор");
        }
        ++index;
        if (index == tokens.count) {
            free_tokens(&tokens);
            free_command_line(result);
            return set_error(error, error_size, "после оператора ожидалась команда");
        }
    }

    free_tokens(&tokens);
    return 0;
}
