#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

// #define DEBUG

#ifdef DEBUG
    #define ON_DEBUG(...) __VA_ARGS__
#else
    #define ON_DEBUG(...)
#endif

static char buf[4096];

void errorPrint(char* file) {
    fprintf(stderr, "%s: %s\n", file, strerror(errno));
}

void myCat(int fd) {
    ON_DEBUG(printf("\tfd = %d\n", fd);)

    ssize_t n = 1;
    while (n != 0) {
        n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            perror("read < 0");
            _exit(1);
        }

        ON_DEBUG(printf("\n\tread = %d\n", n);)

        ssize_t written = 0;
        while (written < n) {
            ssize_t m = write(1, buf + written, n);
            if (m <= 0) {
                perror("write < 0");
                _exit(1);
            }
            written += m;
            ON_DEBUG(printf("\n\tm write = %d\n", m);)
        }
    }

    n = close(fd);
    if (n < 0) {
        perror("close < 0");
        _exit(1);
    }

    if (fd != 0) write(1, "\n", 1);
    ON_DEBUG(printf("\n\tclose = %d\n", n);)
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        myCat(0);
        return 0;
    } else {
        for (int i = 1; i < argc; ++i) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                errorPrint(argv[i]);
                _exit(1);
            }

            myCat(fd);
        }
    }

    ON_DEBUG(printf("\n\tотработал\n");)
    return 0;
}
