#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>

const int ASCII_SINGER_COUNT = 128;      // Количество "богатырей" — по одному на каждый ASCII-символ
const int MAX_SONG_BUFFER_SIZE = 1024;   // Максимальная длина строки (включая '\0')

static const char *const SONG_FILE_NAME = "slova.txt";

typedef struct Monitor {
    pthread_mutex_t mutex;              // Монопольный доступ к общей памяти
    pthread_cond_t  character_changed;  // Уведомление: текущий символ песни изменился

    size_t next_singer_id;              // Какой ID (код символа) выдать следующему потоку

    char   song_text[MAX_SONG_BUFFER_SIZE]; // Текст песни 
    size_t song_length;                     // strlen(song_text)

    size_t next_char_index;             // Индекс следующего символа, который нужно подать в "текущий"
    char   current_character;           // Текущий символ, который кто-то из богатырей должен "петь"
} Monitor;

static void monitor_ctor(Monitor *monitor) {
    assert(monitor != NULL);

    pthread_mutex_init(&monitor->mutex, NULL);
    pthread_cond_init(&monitor->character_changed, NULL);

    monitor->next_singer_id = 0;

    monitor->song_text[0] = '\0';
    monitor->song_length = 0;

    monitor->next_char_index = 0;
    monitor->current_character = '\0';
}

static void monitor_dtor(Monitor *monitor) {
    assert(monitor != NULL);

    pthread_cond_destroy(&monitor->character_changed);
    pthread_mutex_destroy(&monitor->mutex);
}


static void sing_character(char character_to_print) {
    putchar(character_to_print);
    fflush(stdout);
}

// Переход к следующему символу строки
static void move_to_next_character_locked(Monitor *monitor) {
    assert(monitor != NULL);

    if (monitor->next_char_index <= monitor->song_length) {
        monitor->current_character =
            monitor->song_text[monitor->next_char_index];
        monitor->next_char_index++;
    } else {
        // За пределами строки всегда считаем, что песня уже кончилась
        monitor->current_character = '\0';
    }
}

static int read_song_from_file(Monitor *monitor,
                               const char *file_name) {
    assert(monitor != NULL);
    assert(file_name != NULL);

    FILE *file = fopen(file_name, "r");
    if (file == NULL) {
        perror("Не удалось открыть файл slova.txt");
        return -1;
    }

    size_t bytes_read = fread(monitor->song_text,
                              1,
                              MAX_SONG_BUFFER_SIZE - 1,
                              file);
    if (ferror(file)) {
        perror("Ошибка чтения из файла slova.txt");
        fclose(file);
        return -1;
    }

    fclose(file);

    monitor->song_text[bytes_read] = '\0';  // Гарантируем завершение строки
    monitor->song_length = strlen(monitor->song_text);

    if (monitor->song_length == 0) {
        fprintf(stderr,
                "Файл '%s' пуст\n",
                file_name);
        return -1;
    }

    return 0;
}

static char register_singer_and_get_own_character(Monitor *monitor) {
    assert(monitor != NULL);

    pthread_mutex_lock(&monitor->mutex);
    size_t my_id = monitor->next_singer_id++;
    pthread_mutex_unlock(&monitor->mutex);

    return (char)my_id;
}

static void * singer_thread_routine(void *argument) {
    Monitor *monitor = (Monitor *)argument;
    assert(monitor != NULL);

    /* Каждый поток сразу же узнаёт, какой символ он должен "петь" всю жизнь */
    char my_character_to_sing = register_singer_and_get_own_character(monitor);

    while(1) {
        pthread_mutex_lock(&monitor->mutex);

        // Ждём, пока:
        //   1) песня не закончилась (current_character != '\0')
        //   2) текущий символ не станет "нашим" символом
        while (monitor->current_character != '\0' &&
               monitor->current_character != my_character_to_sing) {
            pthread_cond_wait(&monitor->character_changed,
                              &monitor->mutex);
        }

        // Если песня закончилась — выходим
        if (monitor->current_character == '\0') {
            pthread_mutex_unlock(&monitor->mutex);
            break;
        }

        // Наш черёд петь
        sing_character(my_character_to_sing);

        // Продвигаем песню и сообщаем всем, что символ поменялся
        move_to_next_character_locked(monitor);
        pthread_cond_broadcast(&monitor->character_changed);

        pthread_mutex_unlock(&monitor->mutex);
    }

    return NULL;
}

int main(void) {
    Monitor monitor;
    monitor_ctor(&monitor);

    // Читаем песню из файла slova.txt
    if (read_song_from_file(&monitor, SONG_FILE_NAME) != 0) {
        monitor_dtor(&monitor);
        return EXIT_FAILURE;
    }

    // Подготавливаем первый символ до запуска потоков
    pthread_mutex_lock(&monitor.mutex);
    monitor.next_char_index = 0;
    move_to_next_character_locked(&monitor);
    pthread_mutex_unlock(&monitor.mutex);

    // Создаём по потоку на каждый ASCII-символ
    pthread_t singers[ASCII_SINGER_COUNT];
    for (size_t i = 0; i < ASCII_SINGER_COUNT; ++i) {
        int create_result = pthread_create(&singers[i],
                                           NULL,
                                           singer_thread_routine,
                                           &monitor);
        if (create_result != 0) {
            fprintf(stderr,
                    "Не удалось создать поток %zu, код ошибки %d\n",
                    i,
                    create_result);

            // Сообщаем уже запущенным потокам, что песня закончилась,
            // чтобы они не висели в ожидании.
            pthread_mutex_lock(&monitor.mutex);
            monitor.current_character = '\0';
            pthread_cond_broadcast(&monitor.character_changed);
            pthread_mutex_unlock(&monitor.mutex);

            // Ждём завершения уже созданных потоков
            for (size_t j = 0; j < i; ++j) {
                pthread_join(singers[j], NULL);
            }

            monitor_dtor(&monitor);
            return EXIT_FAILURE;
        }
    }

    // Дожидаемся окончания всех потоков
    for (size_t i = 0; i < ASCII_SINGER_COUNT; ++i) {
        pthread_join(singers[i], NULL);
    }

    printf("\nВсе символы из песни были спеты.\n");

    monitor_dtor(&monitor);
    return EXIT_SUCCESS;
}
