#include <sys/sem.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

#define CHECK(expr, msg) \
    do { \
        if ((long long)(expr) < 0) { \
            perror(msg); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

enum { SEM_A = 0, SEM_B = 1, SEM_TOTAL = 2, SEM_MUTEX = 3 };
constexpr int N_SEMS = 4;

struct Context {
    int semid;
    int n;
};

int acquire(int id, int idx) {
    struct sembuf op = {static_cast<unsigned short>(idx), -1, 0};
    return semop(id, &op, 1);
}

int release(int id, int idx) {
    struct sembuf op = {static_cast<unsigned short>(idx), 1, 0};
    return semop(id, &op, 1);
}

int modify(int id, int idx, short delta) {
    struct sembuf op = {static_cast<unsigned short>(idx), delta, 0};
    return semop(id, &op, 1);
}

int getval(int id, int idx) {
    return semctl(id, idx, GETVAL);
}

[[noreturn]] void wrench(Context ctx) {
    while (true) {
        if (acquire(ctx.semid, SEM_A) < 0) break;
        fprintf(stderr, "Wrench\n");

        acquire(ctx.semid, SEM_TOTAL);

        acquire(ctx.semid, SEM_MUTEX);
        if (getval(ctx.semid, SEM_TOTAL) == 0) {
            modify(ctx.semid, SEM_A, 2);
            modify(ctx.semid, SEM_B, 1);
            modify(ctx.semid, SEM_TOTAL, 3);
        }
        release(ctx.semid, SEM_MUTEX);
    }
    fprintf(stderr, "Wrench done\n");
    exit(0);
}

[[noreturn]] void screw(Context ctx) {
    while (ctx.n-- > 0) {
        if (acquire(ctx.semid, SEM_B) < 0) break;
        fprintf(stderr, "Screw\n");

        acquire(ctx.semid, SEM_TOTAL);

        acquire(ctx.semid, SEM_MUTEX);
        if (getval(ctx.semid, SEM_TOTAL) == 0) {
            modify(ctx.semid, SEM_A, 2);
            modify(ctx.semid, SEM_B, 1);
            modify(ctx.semid, SEM_TOTAL, 3);
        }
        release(ctx.semid, SEM_MUTEX);
    }
    fprintf(stderr, "Screw done\n");
    semctl(ctx.semid, N_SEMS, IPC_RMID);
    exit(0);
}

int main() {
    unsigned short init[N_SEMS] = {2, 1, 3, 1};
    int semid = semget(IPC_PRIVATE, N_SEMS, 0666 | IPC_CREAT);
    CHECK(semid, "semget");
    CHECK(semctl(semid, N_SEMS, SETALL, init), "semctl");

    Context ctx{semid, 10};

    if (fork() == 0) screw(ctx);
    if (fork() == 0) screw(ctx);
    if (fork() == 0) wrench(ctx);

    while (wait(nullptr) > 0);
    return 0;
}