#include <stdio.h>
#include <unistd.h>
#include <sys/msg.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/time.h>

bool is_child_process(pid_t proc_id) { return !proc_id; }

pid_t Fork() {
    pid_t fork_res = fork();
    if (fork_res == -1) {
        perror("Fork error:");
        _exit(1);
    }
    return fork_res;
}

static int Msgget() {
    int queue_id = msgget(IPC_PRIVATE, IPC_EXCL | IPC_CREAT | 0666);
    if (queue_id < 0) {
        perror("msgget error");
        _exit(1);
    }
    return queue_id;
}

static int runner(int total_runners, int runner_idx, int queue_id);
static int judge(int total_runners, int queue_id);

enum stadium_status_t {
    STADIUM_STATUS_SPAWNED = 1,
    STADIUM_STATUS_ADVANCE = 2,
    STADIUM_STATUS_READY   = 3,
};

struct msg_t {
    long mtype;
    stadium_status_t status;
};

int main(int ac, const char *av[]) {
    if (ac != 2) {
        fprintf(stderr, "ERROR, YOU SHOULD ENTER N RUNNERS\n");
        _exit(1);
    }
    int runner_count = atoi(av[1]); 

    int queue_id_main = Msgget();

    pid_t arbiter_pid = Fork();
    if (is_child_process(arbiter_pid)) {
        return judge(runner_count, queue_id_main);
    }
    
    for (int idx = 1; idx <= runner_count; ++idx) {
        pid_t sprinter_pid = Fork();
        if (is_child_process(sprinter_pid)) {
            return runner(runner_count, idx, queue_id_main);
        }
    }

    for (int wait_idx = 0; wait_idx < runner_count + 1; ++wait_idx) {
        wait(NULL);
    }

    if (msgctl(queue_id_main, IPC_RMID, 0) == -1) {
        perror("DELETE ERROR");
        _exit(1);
    }
}

static int judge(int total_runners, int queue_id) {
    msg_t packet;
    printf("Judge >>> init\n");

    for (int arrive_cnt = 0; arrive_cnt++ < total_runners; ) {
        if (msgrcv(queue_id, &packet, sizeof(packet), 0, 0) == -1) {
            perror("judge: msgrcv");
            _exit(1);
        }
        printf("Judge >>> check-in by runner type=%ld\n", (long)packet.mtype);
    }
    puts("Judge >>> everyone is on the track");

    packet.mtype  = 1;
    packet.status = STADIUM_STATUS_READY;
    if (msgsnd(queue_id, &packet, sizeof(packet), 0) == -1) {
        perror("judge: msgsnd(all-created)");
        _exit(1);
    }

    struct timeval t_begin;
    if (gettimeofday(&t_begin, NULL) != 0) {
        perror("judge: gettimeofday(start)");
        _exit(1);
    }

    {
        const long offset_base = total_runners + 1;
        packet.mtype  = offset_base + 2;
        packet.status = STADIUM_STATUS_ADVANCE;
        if (msgsnd(queue_id, &packet, sizeof(packet), 0) == -1) {
            perror("judge: msgsnd(start-first)");
            _exit(1);
        }
    }

    {
        const long offset_base = total_runners + 1;
        const long final_mtype = offset_base + total_runners + 2;
        if (msgrcv(queue_id, &packet, sizeof(packet), final_mtype, 0) == -1) {
            perror("judge: msgrcv(last-finish)");
            _exit(1);
        }
    }

    struct timeval t_end;
    if (gettimeofday(&t_end, NULL) != 0) {
        perror("judge: gettimeofday(end)");
        _exit(1);
    }

    double ms_end   = (double)t_end.tv_sec   * 1.0e3 + (double)t_end.tv_usec   / 1.0e3;
    double ms_start = (double)t_begin.tv_sec * 1.0e3 + (double)t_begin.tv_usec / 1.0e3;
    printf("Judge >>> elapsed: %.3f ms\n", ms_end - ms_start);

    return EXIT_SUCCESS;
}

static int runner(int total_runners, int runner_idx, int queue_id) {
    msg_t packet;
    packet.mtype  = runner_idx + 1;
    packet.status = STADIUM_STATUS_SPAWNED;

    printf("Runner %3d :: hello -> judge\n", runner_idx);
    if (msgsnd(queue_id, &packet, sizeof(packet), 0) == -1) {
        perror("runner: msgsnd(hello)");
        _exit(1);
    }

    {
        const long offset_base = total_runners + 1;
        const long target_type = offset_base + runner_idx + 1;
        if (msgrcv(queue_id, &packet, sizeof(packet), target_type, 0) == -1) {
            perror("runner: msgrcv(wait-start)");
            _exit(1);
        }
    }

    if (packet.status != STADIUM_STATUS_ADVANCE) {
        fprintf(stderr, "Runner %d :: unexpected status = %d\n", runner_idx, packet.status);
        _exit(1);
    }

    printf("Runner %3d :: GO\n", runner_idx);
    printf("Runner %3d :: DONE\n", runner_idx);

    {
        const long offset_base = total_runners + 1;
        packet.mtype  = offset_base + runner_idx + 2;
        packet.status = STADIUM_STATUS_ADVANCE;
        if (msgsnd(queue_id, &packet, sizeof(stadium_status_t), 0) == -1) {
            perror("runner: msgsnd(next)");
            _exit(1);
        }
    }

    return EXIT_SUCCESS;
}