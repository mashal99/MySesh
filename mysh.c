#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <glob.h>
#include <libgen.h>
#include <fcntl.h>

#define MAX_LINE 80
#define MAX_TOKENS 20

int last_command_status = 0;

void free_tokens(char **tokens) {
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
}

char **tokenize(char *command) {
    char **tokens = malloc(MAX_TOKENS * sizeof(char*));
    char *token = strtok(command, " \t\r\n");
    int position = 0;

    while (token != NULL) {
        tokens[position] = strdup(token);  // Copy each token
        position++;
        token = strtok(NULL, " \t\r\n");
    }
    tokens[position] = NULL;

    return tokens;
}

char **expand_tokens(char **tokens) {
    char **expanded_tokens = malloc(MAX_TOKENS * sizeof(char*));
    int position = 0;

    for (int i = 0; tokens[i] != NULL; i++) {
        if (strchr(tokens[i], '*') != NULL) {  // Check for wildcard
            glob_t glob_result;
            memset(&glob_result, 0, sizeof(glob_result));
            glob(tokens[i], GLOB_TILDE | GLOB_NOCHECK, NULL, &glob_result);

            if(glob_result.gl_pathc == 0) {
                // No matches found, retain the original token
                expanded_tokens[position++] = strdup(tokens[i]);
            }
            else{
                for (unsigned j = 0; j < glob_result.gl_pathc; j++) {
                    expanded_tokens[position++] = strdup(glob_result.gl_pathv[j]);
                    if (position >= MAX_TOKENS - 1) break;  // Prevent overflow
                }
            }
            globfree(&glob_result);
        } else {
            expanded_tokens[position++] = strdup(tokens[i]);
        }
        if (position >= MAX_TOKENS - 1) break;  // Prevent overflow
    }
    expanded_tokens[position] = NULL;

    free_tokens(tokens);  // Free the original tokens
    return expanded_tokens;
}

void execute_command(char **args) {
    pid_t pid;
    int status;
    int in_redirect = -1, out_redirect = -1, pipe_pos = -1;
    int pipefd[2];

    // Check for redirection and pipes in args
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "<") == 0) {
            args[i] = NULL;
            in_redirect = i + 1;
        } else if (strcmp(args[i], ">") == 0) {
            args[i] = NULL;
            out_redirect = i + 1;
        } else if (strcmp(args[i], "|") == 0) {
            args[i] = NULL;
            pipe_pos = i + 1;
            break;
        }
    }

    if (pipe_pos != -1) {
        pipe(pipefd);
    }

    pid = fork();
    if (pid == 0) {
        // Child process
        if (in_redirect != -1) {
            int fd0 = open(args[in_redirect], O_RDONLY);
            dup2(fd0, STDIN_FILENO);
            close(fd0);
        }
        if (out_redirect != -1) {
            int fd1 = open(args[out_redirect], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            dup2(fd1, STDOUT_FILENO);
            close(fd1);
        }

        if (pipe_pos != -1) {
            // First part of the pipe
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[0]);
            execvp(args[0], args);
            exit(EXIT_FAILURE);
        } else {
            execvp(args[0], args);
            exit(EXIT_FAILURE);
        }
    } else if (pid < 0) {
        // Error forking
        perror("mysh");
    } else {
        if (pipe_pos != -1) {
            // Second part of the pipe
            if (fork() == 0) {
                dup2(pipefd[0], STDIN_FILENO);
                close(pipefd[1]);
                execvp(args[pipe_pos], &args[pipe_pos]);
                exit(EXIT_FAILURE);
            }
            close(pipefd[0]);
            close(pipefd[1]);
        }
        // Wait for the child process or both parts of the pipe
        waitpid(pid, &status, WUNTRACED);
        last_command_status = WIFEXITED(status) ? WEXITSTATUS(status) : 1;

        if (pipe_pos != -1) {
            wait(NULL); // Wait for the second part of the pipe
        }
    }
}

void parse_command(char *command) {
    // Tokenize the command
    char **tokens = tokenize(command);

    // Expand the tokens
    tokens = expand_tokens(tokens);

    //Handling 'then' and 'else' conditionals
    if (tokens[0] != NULL && (strcmp(tokens[0], "then") == 0 || strcmp(tokens[0], "else") == 0)) {
        int should_execute = 0;
        if (strcmp(tokens[0], "then") == 0 && last_command_status == 0) {
            should_execute = 1; // Execute if the last command succeeded
        } else if (strcmp(tokens[0], "else") == 0 && last_command_status != 0) {
            should_execute = 1; // Execute if the last command failed
        }

        if (!should_execute) {
            free_tokens(tokens); // Skip execution if condition not met
            return;
        }

        // Remove 'then' or 'else' from the tokens
        int i = 0;
        for (; tokens[i + 1] != NULL; i++) {
            tokens[i] = tokens[i + 1];
        }
        tokens[i] = NULL;
    }

    // Execute the command
    execute_command(tokens);

    // Free the allocated memory
    free_tokens(tokens);
}

int main() {
    char command[MAX_LINE];

    while (1) {
        printf("mysh> ");
        if (!fgets(command, MAX_LINE, stdin)) {
            // Handle error or EOF
            if (feof(stdin)) break;  // Exit on EOF
            perror("fgets error");
            continue;
        }

        if (strncmp(command, "exit", 4) == 0) {
            break;
        }

        parse_command(command);
    }

    return 0;
}