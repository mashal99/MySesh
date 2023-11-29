#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <glob.h>

#define MAX_INPUT_SIZE 1024
#define MAX_ARG_SIZE 64

void execute_command(char **args) {
    pid_t pid, wpid;
    int status;

    pid = fork();
    if (pid == 0) {
        // Child process
        if (execvp(args[0], args) == -1) {
            perror("mysh");
            exit(EXIT_FAILURE);
        }
    } else if (pid < 0) {
        perror("mysh");
    } else {
        // Parent process
        do {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }
}

void execute_pipeline(char **args1, char **args2) {
    int pipe_fd[2];
    pid_t pid1, pid2;
    int status;

    if (pipe(pipe_fd) == -1) {
        perror("mysh");
        return;
    }

    pid1 = fork();
    if (pid1 == 0) {
        // Child process 1
        close(pipe_fd[0]); // Close read end of the pipe
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[1]);

        if (execvp(args1[0], args1) == -1) {
            perror("mysh");
            exit(EXIT_FAILURE);
        }
    } else if (pid1 < 0) {
        perror("mysh");
        return;
    }

    pid2 = fork();
    if (pid2 == 0) {
        // Child process 2
        close(pipe_fd[1]); // Close write end of the pipe
        dup2(pipe_fd[0], STDIN_FILENO);
        close(pipe_fd[0]);

        if (execvp(args2[0], args2) == -1) {
            perror("mysh");
            exit(EXIT_FAILURE);
        }
    } else if (pid2 < 0) {
        perror("mysh");
        return;
    }

    // Parent process
    close(pipe_fd[0]);
    close(pipe_fd[1]);

    // Wait for both child processes to finish
    waitpid(pid1, NULL, 0);
    waitpid(pid2, &status, 0);
}

void execute_command_with_redirection(char **args, char *input_file, char *output_file) {
    pid_t pid;
    int status;

    pid = fork();
    if (pid == 0) {
        // Child process

        // Handle input redirection
        if (input_file != NULL) {
            int input_fd = open(input_file, O_RDONLY);
            if (input_fd == -1) {
                perror("mysh");
                exit(EXIT_FAILURE);
            }
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }

        // Handle output redirection
        if (output_file != NULL) {
            int output_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0640);
            if (output_fd == -1) {
                perror("mysh");
                exit(EXIT_FAILURE);
            }
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }

        // Execute the command
        if (execvp(args[0], args) == -1) {
            perror("mysh");
            exit(EXIT_FAILURE);
        }
    } else if (pid < 0) {
        perror("mysh");
    } else {
        // Parent process
        waitpid(pid, &status, 0);
    }
}

void execute_command_with_wildcard(char **args) {
    glob_t glob_result;
    int i, status;

    // Initialize the glob structure
    glob(args[0], GLOB_TILDE, NULL, &glob_result);

    // Execute the command for each matched file
    for (i = 0; i < glob_result.gl_pathc; i++) {
        args[0] = glob_result.gl_pathv[i];
        execute_command(args);
    }

    // Free the glob structure
    globfree(&glob_result);
}

int main(int argc, char *argv[]) {
    char input[MAX_INPUT_SIZE];
    char *args[MAX_ARG_SIZE];
    char *input_file = NULL;
    char *output_file = NULL;

    if (argc == 2) {
        // Batch mode
        FILE *batch_file = fopen(argv[1], "r");
        if (batch_file == NULL) {
            perror("mysh");
            exit(EXIT_FAILURE);
        }

        while (fgets(input, sizeof(input), batch_file) != NULL) {
            // Process each line in batch mode
            // (similar to the interactive mode loop)
        }

        fclose(batch_file);
    } else if (argc == 1) {
        // Interactive mode
        printf("Welcome to my shell!\n");

        while (1) {
            printf("mysh> ");
            if (fgets(input, sizeof(input), stdin) == NULL) {
                break; // Exit on EOF (Ctrl+D)
            }

            // Remove newline character from input
            input[strcspn(input, "\n")] = '\0';

            // Tokenize input
            char *token = strtok(input, " ");
            int i = 0;
            while (token != NULL && i < MAX_ARG_SIZE - 1) {
                args[i++] = token;
                token = strtok(NULL, " ");
            }
            args[i] = NULL; // Null-terminate the argument list

            // Handle redirection
            input_file = NULL;
            output_file = NULL;

            for (int j = 0; args[j] != NULL; j++) {
                if (strcmp(args[j], "<") == 0) {
                    if (args[j + 1] != NULL) {
                        input_file = args[j + 1];
                        args[j] = NULL;
                    }
                } else if (strcmp(args[j], ">") == 0) {
                    if (args[j + 1] != NULL) {
                        output_file = args[j + 1];
                        args[j] = NULL;
                    }
                }
            }

            // Handle built-in commands
            if (strcmp(args[0], "cd") == 0) {
                if (args[1] == NULL) {
                    fprintf(stderr, "mysh: cd: missing argument\n");
                } else {
                    if (chdir(args[1]) != 0) {
                        perror("mysh");
                    }
                }
            } else if (strcmp(args[0], "pwd") == 0) {
                char cwd[1024];
                if (getcwd(cwd, sizeof(cwd)) != NULL) {
                    printf("%s\n", cwd);
                } else {
                    perror("mysh");
                }
            } else if (strcmp(args[0], "which") == 0) {
                if (args[1] != NULL) {
                    // Implement the 'which' command
                } else {
                    fprintf(stderr, "mysh: which: missing argument\n");
                }
            } else if (strcmp(args[0], "exit") == 0) {
                printf("mysh: exiting\n");
                break;
            } else {
                // Check for pipelines
                int pipe_position = -1;
                for (int j = 0; args[j] != NULL; j++) {
                    if (strcmp(args[j], "|") == 0) {
                        pipe_position = j;
                        args[j] = NULL;
                        break;
                    }
                }

                // Execute the command(s)
                if (pipe_position != -1) {
                    // Split the command into two parts for pipeline
                    char *args1[MAX_ARG_SIZE];
                    char *args2[MAX_ARG_SIZE];

                    for (int j = 0; j < pipe_position; j++) {
                        args1[j] = args[j];
                    }
                    args1[pipe_position] = NULL;

                    int k = 0;
                    for (int j = pipe_position + 1; args[j] != NULL; j++) {
                        args2[k++] = args[j];
                    }
                    args2[k] = NULL;

                    execute_pipeline(args1, args2);
                } else {
                    // Handle wildcards
                    int wildcard_position = -1;
                    for (int j = 0; args[j] != NULL; j++) {
                        if (strchr(args[j], '*') != NULL) {
                            wildcard_position = j;
                            break;
                        }
                    }

                    if (wildcard_position != -1) {
                        execute_command_with_wildcard(args);
                    } else {
                        // Execute the command with redirection
                        execute_command_with_redirection(args, input_file, output_file);
                    }
                }
            }
        }
    } else {
        fprintf(stderr, "Usage: %s [batch_file]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    return 0;
}
