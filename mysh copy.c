#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <glob.h>
#include <libgen.h>
#include <fcntl.h>

#define MAX_LINE 1024  // Max length of a command

// Function prototypes
char **parse_command(char *cmd);
void execute_command(char **args);

int main() {
    char line[MAX_LINE];
    char **args;
    int should_run = 1;

    while (should_run) {
        printf("mysh> ");
        fflush(stdout);

        if (!fgets(line, MAX_LINE, stdin)) {
            if (feof(stdin)) {
                should_run = 0;
            } else {
                perror("mysh: fgets error");
            }
            continue;
        }

        line[strcspn(line, "\n")] = 0;  // Remove the newline character
        args = parse_command(line);

        if (args[0] == NULL) {
            free(args);
            continue;
        }

        if (strcmp(args[0], "exit") == 0) {
            should_run = 0;
        } else {
            execute_command(args);
        }

        free(args);
    }

    return 0;
}

char **parse_command(char *cmd) {
    int bufsize = 64, position = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token;

    if (!tokens) {
        fprintf(stderr, "mysh: allocation error\n");
        exit(EXIT_FAILURE);
    }

    token = strtok(cmd, " \t\r\n\a");
    while (token != NULL) {
        tokens[position] = token;
        position++;

        if (position >= bufsize) {
            bufsize += 64;
            tokens = realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) {
                fprintf(stderr, "mysh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }

        token = strtok(NULL, " \t\r\n\a");
    }
    tokens[position] = NULL;

    // Wildcard expansion
    for (int i = 0; tokens[i] != NULL; i++) {
        if (strchr(tokens[i], '*') != NULL) {  // Check for wildcard
            glob_t glob_result;
            memset(&glob_result, 0, sizeof(glob_result));

            char *pattern_dup = strdup(tokens[i]);
            char *dir = dirname(pattern_dup);  // Duplicate since dirname may modify the string
            char *base_pattern = basename(strdup(tokens[i]));

            // Construct the full pattern to search
            char full_pattern[MAX_LINE];
            snprintf(full_pattern, MAX_LINE, "%s/%s", dir, base_pattern);

            if (glob(full_pattern, GLOB_TILDE | GLOB_NOCHECK, NULL, &glob_result) == 0) {
                // Replace the original token with the first match
                free(tokens[i]);
                tokens[i] = strdup(glob_result.gl_pathv[0]);

                // Add remaining matches
                for (unsigned j = 1; j < glob_result.gl_pathc; j++) {
                    tokens = realloc(tokens, (++position) * sizeof(char*));
                    tokens[position - 1] = strdup(glob_result.gl_pathv[j]);
                }
            }
            globfree(&glob_result);
            free(pattern_dup);
            free(dir); //Free the duplicated strings
            free(base_pattern);
        }
    }
    tokens[position] = NULL;

    return tokens;
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
        waitpid(pid, &status, WUNTRACED);
        if (pipe_pos != -1) {
            wait(NULL); // Wait for the second part of the pipe
        }
    }
}
