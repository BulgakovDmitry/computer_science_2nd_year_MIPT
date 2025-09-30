#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

bool is_child_process(pid_t p) { return !p; }

int main(int argc, char* argv[]) {
    if (argc < 2) {
        return 1;
    }

    int fds[2];
    if (pipe(fds) == -1) {
        perror("pipe");
        return 1;
    }

    pid_t p = fork();
    if (is_child_process(p)) {        
        close(fds[0]);
        dup2(fds[1], STDOUT_FILENO);
        close(fds[1]);

        execvp(argv[1], &argv[1]);
        perror("Error");
        return 1;
    }
    
    close(fds[1]);

    long bits    = 0;
    long words   = 0;
    long lines   = 0;
    int  in_word = 0;
    char buffer[4096];
    ssize_t bytes_read;

    while ((bytes_read = read(fds[0], buffer, sizeof(buffer))) > 0) {
        bits += bytes_read * 8;
        
        for (ssize_t i = 0; i < bytes_read; i++) {
            char c = buffer[i];
            
            if (c == '\n') {
                lines++;
            }
            
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                if (in_word) {
                    words++;
                    in_word = 0;
                }
            } else {
                in_word = 1;
            }
        }
    }
    
    if (in_word) {
        words++;
    }

    close(fds[0]);

    int status = 0;
    wait(&status);

    printf("Bits:\t%ld\n", bits);
    printf("Words:\t%ld\n", words);
    printf("Lines:\t%ld\n", lines);
    
    return 0;
}