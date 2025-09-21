#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

bool isChildProcess(pid_t p) { return !p; }

int main(int argc, char* argv[]) {
    struct timeval t1 = {};
    struct timeval t2 = {};
    void* timezone = NULL;

    pid_t p = fork();
    if (isChildProcess(p)) {
        gettimeofday(&t1, timezone);
        execv(argv[1], &argv[1]);
        perror("Error");
        return 0;
    }

    int status = 0;
    wait(&status);

    gettimeofday(&t2, timezone);

    double usec = (double)t2.tv_usec - (double)t1.tv_usec;
    double sec  = (double)t2.tv_sec  - (double)t1.tv_sec;
    double time = 1e6 * sec + usec;  

    printf("Delta (usec) = %lg\n", time);
    return 0;
}
