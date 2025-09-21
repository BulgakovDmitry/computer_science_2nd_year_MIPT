#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define KOEF 1000

static bool isChildProcess(pid_t p) { return !p; }

void sleepsort(int N, char** arr) {
    for (int i = 1; i < N; ++i) {
        pid_t p = fork();
        if (isChildProcess(p)) {
            usleep(atoi(arr[i]) * KOEF);
            printf("%s ", arr[i]);
            return;
        }
    }

    int status = 0;
    for (int i = 0; i < N; ++i) {
        wait(&status);
    }

    putchar('\n');
}

int main(int argc, char* argv[]) {
    sleepsort(argc, argv);
    return 0;
}
