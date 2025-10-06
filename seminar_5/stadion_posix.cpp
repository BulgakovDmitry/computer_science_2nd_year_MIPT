#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>

static inline bool is_child_process(pid_t p) { return p == 0; }

static pid_t Fork(void) {
    pid_t r = fork();
    if (r == -1) { perror("fork"); _exit(1); }
    return r;
}

enum stadium_status_t {
    STADIUM_STATUS_SPAWNED = 1,
    STADIUM_STATUS_ADVANCE = 2,
    STADIUM_STATUS_READY   = 3,
};

struct msg_t {
    int runner_idx;              
    enum stadium_status_t status;
};

static int runner(int total_runners, int runner_idx, mqd_t q_checkin, mqd_t *baton);
static int judge (int total_runners, mqd_t q_checkin, mqd_t *baton);

static mqd_t mq_create_open(const char *name, long maxmsg, long msgsize) {
    struct mq_attr attr = {0};
    attr.mq_maxmsg = maxmsg;
    attr.mq_msgsize = msgsize;

    mqd_t q = mq_open(name, O_CREAT | O_RDWR, 0666, &attr);
    if (q == (mqd_t)-1) {
        fprintf(stderr, "mq_open('%s'): %s\n", name, strerror(errno));
        _exit(1);
    }
    return q;
}

int main(int ac, char *av[]) {
    if (ac != 2) {
        fprintf(stderr, "ERROR, YOU SHOULD ENTER N RUNNERS\n");
        _exit(1);
    }
    int n = atoi(av[1]);
    if (n <= 0) {
        fprintf(stderr, "N must be positive\n");
        _exit(1);
    }

    pid_t mypid = getpid();
    char q_checkin_name[64];
    snprintf(q_checkin_name, sizeof(q_checkin_name), "/checkin_%ld", (long)mypid);

    char **baton_names = (char**)calloc((size_t)(n + 2), sizeof(char*));
    if (!baton_names) { perror("calloc baton_names"); _exit(1); }
    for (int i = 1; i <= n + 1; ++i) {
        baton_names[i] = (char*)calloc(64, sizeof(char));
        if (!baton_names[i]) { perror("calloc baton_names[i]"); _exit(1); }
        snprintf(baton_names[i], 64, "/baton_%ld_%d", (long)mypid, i);
    }

    const long MSGSZ = (long)sizeof(struct msg_t);

    mqd_t q_checkin = mq_create_open(q_checkin_name, (n > 10 ? n : 10), MSGSZ);

    mqd_t *baton = (mqd_t*)calloc((size_t)(n + 2), sizeof(mqd_t));
    if (!baton) { perror("calloc baton"); _exit(1); }
    for (int i = 1; i <= n + 1; ++i) {
        baton[i] = mq_create_open(baton_names[i], 10, MSGSZ);
    }

    pid_t arbiter = Fork();
    if (is_child_process(arbiter)) {
        return judge(n, q_checkin, baton);
    }

    for (int i = 1; i <= n; ++i) {
        pid_t r = Fork();
        if (is_child_process(r)) {
            return runner(n, i, q_checkin, baton);
        }
    }

    for (int i = 0; i < n + 1; ++i) {
        (void)wait(NULL);
    }

    mq_close(q_checkin);
    mq_unlink(q_checkin_name);
    for (int i = 1; i <= n + 1; ++i) {
        mq_close(baton[i]);
        mq_unlink(baton_names[i]);
        free(baton_names[i]);
    }
    free(baton_names);
    free(baton);

    return 0;
}

static int judge(int total_runners, mqd_t q_checkin, mqd_t *baton) {
    struct msg_t msg;
    printf("Judge >>> init\n");

    for (int i = 0; i < total_runners; ++i) {
        ssize_t got = mq_receive(q_checkin, (char*)&msg, sizeof(msg), NULL);
        if (got < 0) { perror("judge: mq_receive(checkin)"); _exit(1); }
        printf("Judge >>> check-in by runner idx=%d\n", msg.runner_idx);
    }
    puts("Judge >>> everyone is on the track");

    struct timespec t0, t1;
    if (clock_gettime(CLOCK_MONOTONIC, &t0) != 0) { perror("judge: clock_gettime(start)"); _exit(1); }

    msg.runner_idx = 0;
    msg.status = STADIUM_STATUS_ADVANCE;
    if (mq_send(baton[1], (const char*)&msg, sizeof(msg), 0) != 0) {
        perror("judge: mq_send(start-first)");
        _exit(1);
    }

    ssize_t got = mq_receive(baton[total_runners + 1], (char*)&msg, sizeof(msg), NULL);
    if (got < 0) { perror("judge: mq_receive(last-finish)"); _exit(1); }

    if (clock_gettime(CLOCK_MONOTONIC, &t1) != 0) { perror("judge: clock_gettime(end)"); _exit(1); }

    double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    printf("Judge >>> elapsed: %.3f ms\n", ms);

    return EXIT_SUCCESS;
}

static int runner(int total_runners, int runner_idx, mqd_t q_checkin, mqd_t *baton) {
    (void)total_runners;

    struct msg_t msg;
    msg.runner_idx = runner_idx;
    msg.status = STADIUM_STATUS_SPAWNED;
    printf("Runner %3d :: hello -> judge\n", runner_idx);
    if (mq_send(q_checkin, (const char*)&msg, sizeof(msg), 0) != 0) {
        perror("runner: mq_send(hello)");
        _exit(1);
    }

    ssize_t got = mq_receive(baton[runner_idx], (char*)&msg, sizeof(msg), NULL);
    if (got < 0) { perror("runner: mq_receive(wait-start)"); _exit(1); }
    if (msg.status != STADIUM_STATUS_ADVANCE) {
        fprintf(stderr, "Runner %d :: unexpected status = %d\n", runner_idx, msg.status);
        _exit(1);
    }

    printf("Runner %3d :: GO\n", runner_idx);
    printf("Runner %3d :: DONE\n", runner_idx);

    msg.runner_idx = runner_idx;
    msg.status = STADIUM_STATUS_ADVANCE;
    if (mq_send(baton[runner_idx + 1], (const char*)&msg, sizeof(msg), 0) != 0) {
        perror("runner: mq_send(next)");
        _exit(1);
    }

    return EXIT_SUCCESS;
}
