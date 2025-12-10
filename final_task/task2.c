#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <stdbool.h>

volatile sig_atomic_t stop = 0;

void handler(int sig) {
    stop = 1;   
}

int main(void) {
    int fd[2];

    if (pipe(fd) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
    }

    if (signal(SIGALRM, handler) == SIG_ERR) {
        perror("signal");
        return EXIT_FAILURE;
    }
    
    alarm(1);

    char c = 'A';
    size_t total = 0;

    while (true) {
        ssize_t n = write(fd[1], &c, 1);
        if (n == 1) {
            total++;                  
        } else if (n == -1 && errno == EINTR && stop) {
            fprintf(stdout, "write прервали и он вернул -1\n");
            fflush(stdout);
            break;
        } else if (n == -1) {
            perror("write");
            return EXIT_FAILURE;
        } else {
            perror("непонятно почему вернулось 0, странная инопланетная ошибка");
            break;
        }
    }

    fprintf(stdout, "Итого: %zu\n", total);
    fflush(stdout);

    close(fd[0]);
    close(fd[1]);

    return EXIT_SUCCESS;
}