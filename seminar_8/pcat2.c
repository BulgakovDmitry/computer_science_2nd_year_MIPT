#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>

// #define DEBUG

#ifdef DEBUG
    #define ON_DEBUG(...) __VA_ARGS__
#else
    #define ON_DEBUG(...)
#endif

static char buf[4096];

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t can_write;   
    pthread_cond_t can_read;    
    bool buffer_ready;         
    bool writer_done;         
    char* data;
    size_t size;
} HoareMonitor;

static void safe_copy(HoareMonitor* mon, char* buf, int fd) {
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        pthread_mutex_lock(&mon->mutex);
        while (mon->buffer_ready) {
            pthread_cond_wait(&mon->can_write, &mon->mutex);
        }
        mon->data = (char*)calloc(n, sizeof(char));
        memcpy(mon->data, buf, n);
        mon->size = n;
        mon->buffer_ready = true;
        pthread_cond_signal(&mon->can_read);
        pthread_mutex_unlock(&mon->mutex);
    }
}

void monitor_init(HoareMonitor* m) {
    pthread_mutex_init(&m->mutex, NULL);
    pthread_cond_init(&m->can_write, NULL);
    pthread_cond_init(&m->can_read, NULL);
    m->buffer_ready = false;
    m->writer_done = false;
    m->data = NULL;
    m->size = 0;
}

void monitor_destroy(HoareMonitor* m) {
    pthread_mutex_destroy(&m->mutex);
    pthread_cond_destroy(&m->can_write);
    pthread_cond_destroy(&m->can_read);
}

void* writer_thread(void* arg) {
    char** args = (char**)arg;
    int argc = *(int*)args[0];
    char** argv = (char**)args[1];
    HoareMonitor* mon = (HoareMonitor*)args[2];

    if (argc == 1) {
        ssize_t n;
        safe_copy(mon, buf, STDIN_FILENO);
    } else {
        for (int i = 1; i < argc; ++i) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "%s: %s\n", argv[i], strerror(errno));
                exit(1);
            }
            safe_copy(mon, buf, fd);
            close(fd);
        }
    }

    pthread_mutex_lock(&mon->mutex);
    mon->writer_done = true;
    pthread_cond_signal(&mon->can_read);
    pthread_mutex_unlock(&mon->mutex);

    return NULL;
}

void* reader_thread(void* arg) {
    HoareMonitor* mon = (HoareMonitor*)arg;

    while (1) {
        pthread_mutex_lock(&mon->mutex);
        while (!mon->buffer_ready && !mon->writer_done) {
            pthread_cond_wait(&mon->can_read, &mon->mutex);
        }
        if (!mon->buffer_ready && mon->writer_done) {
            pthread_mutex_unlock(&mon->mutex);
            break;
        }
        write(STDOUT_FILENO, mon->data, mon->size);
        free(mon->data);
        mon->buffer_ready = false;
        pthread_cond_signal(&mon->can_write);
        pthread_mutex_unlock(&mon->mutex);
    }
    return NULL;
}

int main(int argc, char* argv[]) {
    HoareMonitor mon;
    monitor_init(&mon);

    void* writer_args[3];
    writer_args[0] = &argc;
    writer_args[1] = argv;
    writer_args[2] = &mon;

    pthread_t writer, reader;
    pthread_create(&writer, NULL, writer_thread, writer_args);
    pthread_create(&reader, NULL, reader_thread, &mon);

    pthread_join(writer, NULL);
    pthread_join(reader, NULL);

    monitor_destroy(&mon);

    return 0;
}