#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <sys/mman.h>
#include <semaphore.h>

#define MAX_HUNTERS 32

static bool is_child_process(pid_t p) { return !p; }

pid_t my_fork(void) {
    pid_t fork_res = fork();
    if (fork_res == -1) {
        perror("Ошибка fork");
        _exit(1);
    }
    return fork_res;
}

typedef struct SharedMemory {
    int N;                                // всего охотников
    int M;                                // количество дней
    int day;                              // текущий день
    int alive_count;                      // сколько охотников живо
    int pot_meat;                         // куски мяса в котле
    int hunters_back;                     // сколько охотников вернулось с охоты
    int terminate;                        // флаг завершения моделирования

    int hunter_alive[MAX_HUNTERS];        // 1 — жив, 0 — убит
    int hunter_success_today[MAX_HUNTERS];// 1 — удачная охота
    int hunter_ate_today[MAX_HUNTERS];    // 1 — поел
    int hunter_forced_fail[MAX_HUNTERS];  // 1 — гарантированно провалит охоту

    sem_t mutex;                          // мьютекс для доступа к общей памяти
    sem_t start_hunt;                     // сигнал начала охоты
    sem_t all_hunters_back;               // сигнал, что все вернулись
    sem_t dinner_done;                    // сигнал окончания ужина
} SharedMemory;

static void hunter_process(int id, SharedMemory *shared_memory) {
    srand((unsigned int)(time(NULL) ^ getpid()));

    while (1) {
        // Ждём начала дня (сигнал от повара)
        sem_wait(&shared_memory->start_hunt);

        int alive = shared_memory->hunter_alive[id];
        int day = shared_memory->day;

        // Если моделирование закончено или охотник уже мёртв — выходим
        if (shared_memory->terminate || !alive) {
            break;
        }

        printf("День %d: охотник %d отправился на охоту\n", day, id);
        fflush(stdout);

        /*  тута охота  */

        // Определяем, была ли охота удачной

        int success = 0;
        if (shared_memory->hunter_forced_fail[id] /* меняет повар когда хантеры не на охоте */) {
            success = 0;
        } else {
            success = rand() % 2; 
        }

        sem_wait(&shared_memory->mutex);
        shared_memory->hunter_success_today[id] = success;
        if (success) {
            shared_memory->pot_meat++;
            printf("День %d: охотник %d удачно поохотился и принёс мясо. В котле теперь %d кусков\n",
                   day, id, shared_memory->pot_meat);
        } else {
            printf("День %d: охотник %d вернулся без добычи\n", day, id);
        }

        shared_memory->hunters_back++;
        int back = shared_memory->hunters_back;         // запоминаем, сколько уже вернулось
        int alive_count = shared_memory->alive_count;   // запоминаем, сколько всего живых
        sem_post(&shared_memory->mutex);

        // Последний вернувшийся будит повара
        if (back == alive_count) {
            sem_post(&shared_memory->all_hunters_back);
        }

        // Ждём, пока повар закончит раздачу еды
        sem_wait(&shared_memory->dinner_done);

        int ate = shared_memory->hunter_ate_today[id];
        alive = shared_memory->hunter_alive[id];
        day = shared_memory->day;

        if (!alive) {
            printf("День %d: охотник %d был разделан поваром. Покойся с миром.\n", day, id);
            fflush(stdout);
            break;
        } else if (ate) {
            printf("День %d: охотник %d поел и завтра будет охотиться обычно.\n",
                   day, id);
        } else {
            printf("День %d: охотник %d остался голодным и завтра точно будет неудачлив.\n",
                   day, id);
        }
        fflush(stdout);
    }

    fflush(stdout);
}

static void cook_process(SharedMemory *shared_memory) {
    for (int day = 1; day <= shared_memory->M; ++day) {
        sem_wait(&shared_memory->mutex);
        if (shared_memory->alive_count == 0) {
            shared_memory->terminate = 1;
            sem_post(&shared_memory->mutex);
            printf("День %d: охотников больше не осталось, племя вымерло.\n", day);
            fflush(stdout);
            break;
        }

        shared_memory->day = day;
        shared_memory->pot_meat = 0;
        shared_memory->hunters_back = 0;

        for (int i = 0; i < shared_memory->N; ++i) {
            shared_memory->hunter_success_today[i] = 0;
            shared_memory->hunter_ate_today[i] = 0;
        }

        int alive = shared_memory->alive_count;
        printf("\n===== День %d начинается. Живых охотников: %d =====\n", day, alive);
        fflush(stdout);
        sem_post(&shared_memory->mutex);

        // Запускаем всех живых охотников на охоту
        for (int i = 0; i < alive; ++i) {
            sem_post(&shared_memory->start_hunt);
        }

        // Ждём, пока все вернутся
        sem_wait(&shared_memory->all_hunters_back);

        sem_wait(&shared_memory->mutex); 
        int meat = shared_memory->pot_meat;
        alive = shared_memory->alive_count;
        printf("День %d: все охотники вернулись. В котле %d кусков мяса\n", day, meat);

        // Повар ест первым
        if (meat > 0) {
            meat--;
            printf("День %d: повар взял свою порцию. В котле осталось %d кусков\n", day, meat);
        } else {
            printf("День %d: повар остался голодным!\n", day);
        }

        // Кормим охотников
        int hungry_indices[MAX_HUNTERS]; // массив индексов голодных охотников в текущий вечер
        int hungry_count = 0;

        for (int i = 0; i < shared_memory->N; ++i) {
            if (!shared_memory->hunter_alive[i]) {
                continue;
            }
            if (meat > 0) {
                shared_memory->hunter_ate_today[i] = 1;
                meat--;
            } else {
                shared_memory->hunter_ate_today[i] = 0;
                hungry_indices[hungry_count++] = i;
            }
        }

        printf("День %d: после ужина в котле осталось %d кусков\n", day, meat);

        // Если кто-то голодный — повар злится и кого-то разделывает
        if (hungry_count > 0) {
            printf("День %d: еды не хватило на всех, повар злой...\n", day);
            // Пытаемся выбрать жертвой неудачливых охотников
            for (int h = 0; h < hungry_count; ++h) {
                int loshara_index = -1;
                for (int i = 0; i < shared_memory->N; ++i) {
                    if (shared_memory->hunter_alive[i] && !shared_memory->hunter_success_today[i]) {
                        loshara_index = i;
                        break;
                    }
                }
                if (loshara_index == -1) {
                    break; // не осталось подходящих жертв
                }
                shared_memory->hunter_alive[loshara_index] = 0;
                shared_memory->alive_count--;
                printf("День %d: повар разделывает неудачливого охотника %d. Живых осталось: %d\n",
                       day, loshara_index, shared_memory->alive_count);
            }
        }

        // Настраиваем неудачу на следующий день
        for (int i = 0; i < shared_memory->N; ++i) {
            if (!shared_memory->hunter_alive[i]) {
                continue;
            }
            if (shared_memory->hunter_ate_today[i]) {
                shared_memory->hunter_forced_fail[i] = 0; // сытый — обычный шанс
            } else {
                shared_memory->hunter_forced_fail[i] = 1; // голодный — завтра гарантированный провал
            }
        }

        printf("===== День %d закончился. Живых охотников: %d =====\n\n", day, shared_memory->alive_count);
        fflush(stdout);

        sem_post(&shared_memory->mutex);

        // Сообщаем охотникам, что ужин закончен
        for (int i = 0; i < alive; ++i) {
            sem_post(&shared_memory->dinner_done);
        }
    }

    // Завершаем моделирование
    shared_memory->terminate = 1;
    int alive = shared_memory->alive_count;

    // На случай, если какие-то процессы ещё ждут семафоры — отпустим их
    for (int i = 0; i < alive + 4; ++i) {
        sem_post(&shared_memory->start_hunt);
        sem_post(&shared_memory->dinner_done);
    }

    printf("Повар завершает моделирование.\n");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "требуется ввести: N - число охотников (<= %d), M - число дней\n", MAX_HUNTERS);
        return EXIT_FAILURE;
    }

    const int N = atoi(argv[1]);   // количество охотников
    const int M = atoi(argv[2]);   // количество дней 

    if (N < 0 || N > MAX_HUNTERS) {
        fprintf(stderr, "Некорректное N (должно быть от 1 до %d).\n", MAX_HUNTERS);
        return EXIT_FAILURE;
    }
    if (N == 0) {
        fprintf(stderr, "племя обречено\n");
        return EXIT_FAILURE;
    }
    if (M <= 0) {
        fprintf(stderr, "Количество дней M должно быть положительным.\n");
        return EXIT_FAILURE;
    }

    // Создаём разделяемую память
    SharedMemory *shared_memory = mmap(NULL, sizeof(SharedMemory),
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_ANONYMOUS,
                           -1, 0);
    if (shared_memory == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }

    shared_memory->N = N;
    shared_memory->M = M;
    shared_memory->day = 0;
    shared_memory->alive_count = N;
    shared_memory->pot_meat = 0;
    shared_memory->hunters_back = 0;
    shared_memory->terminate = 0;

    for (int i = 0; i < MAX_HUNTERS; ++i) {
        shared_memory->hunter_alive[i] = (i < N) ? 1 : 0;
        shared_memory->hunter_success_today[i] = 0;
        shared_memory->hunter_ate_today[i] = 0;
        shared_memory->hunter_forced_fail[i] = 0;
    }

    // Инициализируем семафоры
    if (sem_init(&shared_memory->mutex, 1, 1) == -1 ||
        sem_init(&shared_memory->start_hunt, 1, 0) == -1 ||
        sem_init(&shared_memory->all_hunters_back, 1, 0) == -1 ||
        sem_init(&shared_memory->dinner_done, 1, 0) == -1) {
        perror("sem_init");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < N; ++i) {
        pid_t pid = my_fork();
        if (is_child_process(pid)) {
            hunter_process(i, shared_memory);
            _exit(0);
        }
    }

    cook_process(shared_memory);
    
    for (int i = 0; i < N; ++i) {
        wait(NULL);
    }

    // Освобождаем ресурсы
    sem_destroy(&shared_memory->mutex);
    sem_destroy(&shared_memory->start_hunt);
    sem_destroy(&shared_memory->all_hunters_back);
    sem_destroy(&shared_memory->dinner_done);

    munmap(shared_memory, sizeof(SharedMemory));

    return EXIT_SUCCESS;
}
