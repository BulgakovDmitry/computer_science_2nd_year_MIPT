#include <cerrno>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>

#define RESET   "\033[0m"
#define RED     "\033[1;31m"
#define MANG    "\033[1;35m"
#define GREEN   "\033[1;32m"
#define BLUE    "\033[1;34m"

enum {
    S_ARRIVAL = 0,   // счётчик "подошёл к душевой кабине" (барьер ожидания всех)
    S_START   = 1,   // стартовый барьер — родитель "открывает душ"
    S_ROOM    = 2,   // бинарный: 1 — душ свободен (никого нет), 0 — занят каким-то полом
    S_SLOTS   = 3,   // счётный: свободные места (N)
    S_MEN_MTX = 4,   // мьютекс счётчика мужчин
    S_WOM_MTX = 5,   // мьютекс счётчика женщин
    S_COUNT
};

union semun {
    int val;
    struct sem_id_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};

static pid_t Fork() {
    pid_t fork_res = fork();
    if (fork_res == -1) {
        perror("Fork error:");
        _exit(1);
    }
    return fork_res;
}

static bool is_child_process(pid_t proc_id) { return !proc_id; }

static void die(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
    std::exit(EXIT_FAILURE);
}

static void sem_change(int sem_id, unsigned short idx, short delta) {
    sembuf op{ idx, delta, 0 };
    while (semop(sem_id, &op, 1) == -1) {
        if (errno == EINTR) continue;
        die("semop failed (idx=%hu, delta=%d): %s", idx, (int)delta, strerror(errno));
    }
}
static inline void P(int sem_id, unsigned short idx) { sem_change(sem_id, idx, -1); }
static inline void V(int sem_id, unsigned short idx) { sem_change(sem_id, idx, +1); }

struct Shared {
    int men_in;    
    int women_in;  
};

static int create_semset(int N) {
    int sem_id = semget(IPC_PRIVATE, S_COUNT, IPC_CREAT | 0600);
    if (sem_id == -1) die("semget failed: %s", std::strerror(errno));
    union semun arg{};
    arg.val = 0; if (semctl(sem_id, S_ARRIVAL, SETVAL, arg) == -1) die("semctl ARRIVAL: %s", std::strerror(errno));
    arg.val = 0; if (semctl(sem_id, S_START,   SETVAL, arg) == -1) die("semctl START: %s",   std::strerror(errno));
    arg.val = 1; if (semctl(sem_id, S_ROOM,    SETVAL, arg) == -1) die("semctl ROOM: %s",    std::strerror(errno));
    arg.val = N; if (semctl(sem_id, S_SLOTS,   SETVAL, arg) == -1) die("semctl SLOTS: %s",   std::strerror(errno));
    arg.val = 1; if (semctl(sem_id, S_MEN_MTX, SETVAL, arg) == -1) die("semctl MEN_MTX: %s", std::strerror(errno));
    arg.val = 1; if (semctl(sem_id, S_WOM_MTX, SETVAL, arg) == -1) die("semctl WOM_MTX: %s", std::strerror(errno));
    return sem_id;
}

static int create_shm(Shared** out) {
    int shared_memory_id = shmget(IPC_PRIVATE, sizeof(Shared), IPC_CREAT | 0600);
    if (shared_memory_id == -1) die("shmget failed: %s", strerror(errno));
    void* p = shmat(shared_memory_id, nullptr, 0);
    if (p == (void*)-1) die("shmat failed: %s", strerror(errno));
    *out = (Shared*)p;
    (*out)->men_in = 0;
    (*out)->women_in = 0;
    return shared_memory_id;
}

static void cleanup_ipc(int sem_id, int shared_memory_id, Shared* sh) {
    if (sh && sh != (void*)-1)  shmdt(sh);
    if (shared_memory_id != -1) shmctl(shared_memory_id, IPC_RMID, nullptr);
    if (sem_id != -1)           semctl(sem_id, 0, IPC_RMID);
}

static void man_enter(int sem_id, Shared* sh, int id) {
    // первый мужчина закрывает комнату
    P(sem_id, S_MEN_MTX);

    // create critical section ..

    sh->men_in++;
    if (sh->men_in == 1) {
        P(sem_id, S_ROOM); // заняли душ для мужчин
    }

    // exit of section ..

    V(sem_id, S_MEN_MTX);

    // дождаться свободного места
    P(sem_id, S_SLOTS);
    printf(BLUE "МУЖЧИНА НОМЕР %d ЗАШЕЛ В ДУШ\n" RESET, id);
    fflush(stdout);
}

static void man_leave(int sem_id, Shared* sh, int id) {
    printf(BLUE "МУЖЧИНА НОМЕР %d ВЫШЕЛ ИЗ ДУША\n" RESET, id);
    fflush(stdout);

    V(sem_id, S_SLOTS);

    P(sem_id, S_MEN_MTX);
    sh->men_in--;
    if (sh->men_in == 0) {
        V(sem_id, S_ROOM); // открыли душ для другого пола
    }
    V(sem_id, S_MEN_MTX);
}

static void woman_enter(int sem_id, Shared* sh, int id) {
    P(sem_id, S_WOM_MTX);
    sh->women_in++;
    if (sh->women_in == 1) {
        P(sem_id, S_ROOM);
    }
    V(sem_id, S_WOM_MTX);

    P(sem_id, S_SLOTS);
    printf(MANG "ЖЕНЩИНА НОМЕР %d ЗАШЛА В ДУШ\n" RESET, id);
    fflush(stdout);
}

static void woman_leave(int sem_id, Shared* sh, int id) {
    printf(MANG "ЖЕНЩИНА НОМЕР %d ВЫШЛА ИЗ ДУША\n" RESET, id);
    fflush(stdout);

    V(sem_id, S_SLOTS);

    P(sem_id, S_WOM_MTX);
    sh->women_in--;
    if (sh->women_in == 0) {
        V(sem_id, S_ROOM);
    }
    V(sem_id, S_WOM_MTX);
}

static void man_process(int sem_id, Shared* sh, int id) {
    printf(BLUE "МУЖЧИНА %d СТОИТ ПЕРЕД ДУШЕМ\n" RESET, id);
    fflush(stdout);

    V(sem_id, S_ARRIVAL); //TODO rename wait & sygnal
    P(sem_id, S_START);

    man_enter(sem_id, sh, id);
    man_leave(sem_id, sh, id);
    _exit(0);
}

static void woman_process(int sem_id, Shared* sh, int id) {
    printf(MANG "ЖЕНЩИНА НОМЕР %d СТОИТ ПЕРЕД ДУШЕМ\n" RESET, id);
    fflush(stdout);

    V(sem_id, S_ARRIVAL);
    P(sem_id, S_START);

    woman_enter(sem_id, sh, id);
    woman_leave(sem_id, sh, id);
    _exit(0);
}

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, RED "expected 3 numbers in command lile\n" RESET);
        return EXIT_FAILURE;
    }

    const int N = atoi(argv[1]);
    const int M = atoi(argv[2]);
    const int W = atoi(argv[3]);

    printf(GREEN "\t-----ДУШЕВАЯ КОМНАТА НА %d МЕСТ-----\n" RESET, N);
    fflush(stdout);
    
    int sem_id = create_semset(N);
    Shared* sh = nullptr;
    int shared_memory_id = create_shm(&sh);

    for (int i = 1; i <= M; ++i) {
        pid_t man_pid = Fork();
        if (is_child_process(man_pid)) 
            man_process(sem_id, sh, i);
    }
    for (int i = 1; i <= W; ++i) {
        pid_t wom_pid = Fork();
        if (is_child_process(wom_pid)) 
            woman_process(sem_id, sh, i);
    }

    for (int i = 0; i < M + W; ++i) P(sem_id, S_ARRIVAL);

    printf(GREEN "\t-----ДУШЕВАЯ КОМНАТА ОТКРЫВАЕТСЯ-----\n" RESET);
    fflush(stdout);

    for (int i = 0; i < M + W; ++i) V(sem_id, S_START);

    int status;
    while (wait(&status) > 0) {}

    cleanup_ipc(sem_id, shared_memory_id, sh);

    printf(GREEN "\t-----ДУШЕВАЯ КОМНАТА ПУСТА-----\n" RESET);
    fflush(stdout);
    return 0;
}