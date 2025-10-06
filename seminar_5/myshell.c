#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// --------------------------- Константы и настройки ---------------------------
#define GREEN "\033[1;32m"                  // esc (зеленый)
#define RESET "\033[0m"                     // esc (завершение)
#define SHELL_LINE_CAP   1024               // Максимальная длина вводимой строки
#define SHELL_ARG_MAX    64                 // Максимум аргументов в одной команде
#define SHELL_PIPE_MAX   16                 // Максимум команд в конвейере
#define SHELL_PROMPT     GREEN "$ " RESET   // Приглашение

// ------------------------------ Коды ошибок ---------------------------------

typedef enum {
    SH_OK = 0,
    SH_ERR_SYNTAX,
    SH_ERR_OPEN,
    SH_ERR_PIPE,
    SH_ERR_ARGV_LIMIT,
    SH_ERR_EXEC,
    SH_ERR_PIPELEN_LIMIT,
    SH_ERR_INPUT_READ
} sh_status_t;

// -------------------------- Вспомогательные макросы -------------------------

#define UNUSED(x) (void)(x)

static inline void close_if_open(int *fd) {
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

// -------------------------- Прототипы функций -------------------------------

static void         extract_redirections(char *segment,
                                         const char **in_path,
                                         const char **out_path);

static sh_status_t  run_pipeline(char **segments,
                                 size_t seg_count,
                                 const char *in_path,
                                 const char *out_path);

static sh_status_t  tokenize_command(char *segment, char **argv);

// --------------------------------- main -------------------------------------

int main(int argc, const char *argv[]) {
    UNUSED(argc); UNUSED(argv);

    char   line[SHELL_LINE_CAP];
    char  *segments[SHELL_PIPE_MAX];

    for (;;) {
        fputs(SHELL_PROMPT, stdout);
        fflush(stdout);

        if (!fgets(line, sizeof line, stdin)) {
            return (int)SH_ERR_INPUT_READ;
        }

        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '\0') {
            continue;
        }

        if (strcmp(line, "exit") == 0) {
            break;
        }

        size_t seg_count = 0;
        char  *scan      = line;
        segments[seg_count++] = scan; 

        while (*scan) {
            if (*scan == '|') {
                *scan = '\0'; 
                if (seg_count >= SHELL_PIPE_MAX) {
                    fprintf(stderr,
                            "Sorry, this shell supports only %d commands in conveyor.\n",
                            SHELL_PIPE_MAX);
                    return (int)SH_ERR_PIPELEN_LIMIT;
                }
                segments[seg_count++] = scan + 1;
            }
            ++scan;
        }

        const char *in_path  = NULL;
        const char *out_path = NULL;

        for (size_t i = 0; i < seg_count; ++i) {
            const char *in  = NULL;
            const char *out = NULL;
            extract_redirections(segments[i], &in, &out);

            if (in) {
                if (i != 0) {
                    fprintf(stderr, "Only first command can have input redirection.\n");
                    return (int)SH_ERR_SYNTAX;
                }
                if (in_path) {
                    fprintf(stderr, "Only one input redirection is allowed.\n");
                    return (int)SH_ERR_SYNTAX;
                }
                in_path = in;
            }

            if (out) {
                if (i + 1 != seg_count) {
                    fprintf(stderr, "Only last command can have output redirection.\n");
                    return (int)SH_ERR_SYNTAX;
                }
                if (out_path) {
                    fprintf(stderr, "Only one output redirection is allowed.\n");
                    return (int)SH_ERR_SYNTAX;
                }
                out_path = out;
            }
        }

        sh_status_t st = run_pipeline(segments, seg_count, in_path, out_path);
        if (st != SH_OK) {
            return (int)st; 
        }
    }

    return EXIT_SUCCESS;
}

static void extract_redirections(char *segment,
                                 const char **in_path,
                                 const char **out_path) {
    while (*segment) {
        if (*segment == '<') {
            *segment = '\0';
            ++segment;
            while (isspace((unsigned char)*segment)) {
                *segment = '\0';
                ++segment;
            }
            *in_path = segment;
            while (*segment && !isspace((unsigned char)*segment) && *segment != '>') {
                ++segment;
            }
            if (*segment != '>') {
                *segment = '\0';
                ++segment;
            }
            continue;
        }

        if (*segment == '>') {
            *segment = '\0';
            ++segment;
            while (isspace((unsigned char)*segment)) {
                *segment = '\0';
                ++segment;
            }
            *out_path = segment;
            while (*segment && !isspace((unsigned char)*segment) && *segment != '<') {
                ++segment;
            }
            if (*segment != '<') {
                *segment = '\0';
                ++segment;
            }
            continue;
        }

        ++segment;
    }
}

static sh_status_t run_pipeline(char **segments,
                                size_t seg_count,
                                const char *in_path,
                                const char *out_path) {
    int    prev_read_fd  = -1;  
    int    pipe_fds[2]   = {-1, -1};
    size_t spawned       = 0;

    for (size_t i = 0; i < seg_count; ++i) {
        char *argv[SHELL_ARG_MAX];
        sh_status_t tok = tokenize_command(segments[i], argv);
        if (tok != SH_OK) {
            close_if_open(&prev_read_fd);
            return tok;
        }

        if (i + 1 != seg_count) {
            if (pipe(pipe_fds) != 0) {
                perror("Pipe error");
                return SH_ERR_PIPE;
            }
        }

        pid_t pid = fork();
        if (pid == 0) { 
            if (i == 0 && in_path) {
                int fd_in = open(in_path, O_RDONLY);
                if (fd_in < 0) {
                    perror(in_path);
                    _exit((int)SH_ERR_OPEN);
                }
                dup2(fd_in, STDIN_FILENO);
                close(fd_in);
            }

            if (i + 1 == seg_count && out_path) {
                int fd_out = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0664);
                if (fd_out < 0) {
                    perror(out_path);
                    _exit((int)SH_ERR_OPEN);
                }
                dup2(fd_out, STDOUT_FILENO);
                close(fd_out);
            }

            if (prev_read_fd >= 0) {
                dup2(prev_read_fd, STDIN_FILENO);
                close(prev_read_fd);
            }

            if (i + 1 != seg_count) {
                dup2(pipe_fds[1], STDOUT_FILENO);
                close(pipe_fds[0]);
                close(pipe_fds[1]);
            }

            execvp(argv[0], argv);
            perror(argv[0]);
            _exit((int)SH_ERR_EXEC);
        }

        if (pid < 0) {
            perror("fork");
            close_if_open(&prev_read_fd);
            if (i + 1 != seg_count) {
                close_if_open(&pipe_fds[0]);
                close_if_open(&pipe_fds[1]);
            }
            return SH_ERR_EXEC; 
        }

        ++spawned;

        if (prev_read_fd >= 0) {
            close(prev_read_fd);
            prev_read_fd = -1;
        }
        if (i + 1 != seg_count) {
            prev_read_fd = pipe_fds[0];
            close(pipe_fds[1]);
            pipe_fds[0] = pipe_fds[1] = -1;
        }
    }

    for (size_t n = 0; n < spawned; ++n) {
        (void)wait(NULL);
    }

    return SH_OK;
}

static sh_status_t tokenize_command(char *segment, char **argv) {
    bool   in_word = false;
    size_t argcnt  = 0;

    for ( ; *segment; ++segment) {
        if (!isspace((unsigned char)*segment)) {
            if (!in_word) {
                if (argcnt >= SHELL_ARG_MAX) {
                    fprintf(stderr, "Unsupported argv size amount.\n");
                    return SH_ERR_ARGV_LIMIT;
                }
                argv[argcnt++] = segment;
                in_word = true;
            }
        } else {
            *segment = '\0';
            in_word  = false;
        }
    }

    argv[argcnt] = NULL;
    return SH_OK;
}
