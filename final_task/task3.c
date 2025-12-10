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

    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; 

    if (sigaction(SIGALRM, &sa, NULL) == -1) {
        perror("sigaction");
        return EXIT_FAILURE;
    }

    char c = 'A';
    size_t total = 0;

    alarm(1);

    while (true) {
        ssize_t n = write(fd[1], &c, 1);
        if (n == 1) {
            total++;                  
        } else if (n == -1 && errno == EINTR && stop) {
            fprintf(stdout, "write прервали\n");
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