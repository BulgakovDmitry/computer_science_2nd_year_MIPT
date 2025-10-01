#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdbool.h>

// #define DEBUG

#ifdef DEBUG
    #define ON_DEBUG(...) __VA_ARGS__
#else
    #define ON_DEBUG(...)
#endif

static char buf[4096];

static bool is_child_process(pid_t p) { return !p; }

void errorPrint(char* file) {
    fprintf(stderr, "%s: %s\n", file, strerror(errno));
}

void safe_write(int fd, const char* data, size_t size) {
    ssize_t written = 0;
    while (written < size) {
        ssize_t m = write(fd, data + written, size - written);
        if (m <= 0) {
            perror("write failed");
            _exit(1);
        }
        written += m;
        ON_DEBUG(printf("\n\tm write = %zd\n", m);)
    }
}

void copy_data(int from_fd, int to_fd) {
    ssize_t n = 1;
    while (n != 0) {
        n = read(from_fd, buf, sizeof(buf));
        if (n < 0) {
            perror("read failed");
            _exit(1);
        }
        
        if (n > 0) {
            safe_write(to_fd, buf, n);
        }
    }
}

void reader_process(int fds[2], int argc, char* argv[]) {
    close(fds[0]); 
    
    if (argc == 1) {
        copy_data(0, fds[1]);
    } else {
        for (int i = 1; i < argc; ++i) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                errorPrint(argv[i]);
                _exit(1);
            }

            copy_data(fd, fds[1]);
            close(fd);
            
          
        }
    }
    
    close(fds[1]); 
    _exit(0);
}

void writer_process(int fds[2]) {
    close(fds[1]); 
    copy_data(fds[0], 1);
    close(fds[0]); 
    _exit(0);
}

int main(int argc, char* argv[]) {
    int fds[2];
    
    if (pipe(fds) == -1) {
        perror("pipe creation failed");
        return 1;
    }
    
    ON_DEBUG(printf("Pipe created: read=%d, write=%d\n", fds[0], fds[1]);)

    pid_t reader_pid = fork();
    if (reader_pid == -1) {
        perror("fork for reader failed");
        return 1;
    }
    
    if (is_child_process(reader_pid)) 
        reader_process(fds, argc, argv);
    
    pid_t writer_pid = fork();
    if (writer_pid == -1) {
        perror("fork for writer failed");
        return 1;
    }
    
    if (is_child_process(writer_pid)) 
        writer_process(fds);
    
    close(fds[0]);
    close(fds[1]);
    
    int status;
    waitpid(reader_pid, &status, 0);
    waitpid(writer_pid, &status, 0);
    
    ON_DEBUG(printf("\n\tотработал\n");)
    return 0;
}