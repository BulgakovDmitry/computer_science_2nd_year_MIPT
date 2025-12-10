#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

int main(void) {
    int fd[2];                 
    if (pipe(fd) == -1) {       
        perror("pipe");         
        return EXIT_FAILURE;   
    }                          

    int flags = fcntl(fd[1], F_GETFL);
    if (flags == -1) {
        perror("fcntl(F_GETFL)");
        return EXIT_FAILURE;
    }
    if (fcntl(fd[1], F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(fd[1], F_SETFL, flags | O_NONBLOCK)");
        return EXIT_FAILURE;
    }

    const size_t chunk = 1; 
    char* buf = (char*)calloc(chunk, sizeof(char));
    if (!buf) {
        perror("bad allocation");
        return EXIT_FAILURE;
    }
    memset(buf, 'A', chunk);

    size_t total = 0;

    while (true) {
        ssize_t n = write(fd[1], buf, chunk);
        if (n > 0) {
            total += (size_t)n;
        } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            fprintf(stdout, "буффер заполнен\n");
            fflush(stdout);
            break;
        } else if (n == -1) {
            perror("write: ошибка");
            break;
        } else {
            perror("непонятно почему вернулось 0, странная инопланетная ошибка");
            break;
        }
    }

    fprintf(stdout, "Итого: %zu\n", total);
    fflush(stdout);

    free(buf);
    buf = NULL;
    close(fd[0]);
    close(fd[1]);

    return EXIT_SUCCESS;
}
