#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#define MAX_SINGERS (128)

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t  signal;
    const char     *text;
    size_t          cursor;
    int             slot_id;
} chorus_ctx_t;

static void *singer_routine(void *arg);

int main(int argc, const char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "%s: usage: %s \"string to print\"\n", argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    chorus_ctx_t ctx;
    ctx.slot_id = 1;
    ctx.cursor  = 0;
    ctx.text    = argv[1];

    pthread_mutex_init(&ctx.lock, NULL);
    pthread_cond_init(&ctx.signal, NULL);

    pthread_t workers[MAX_SINGERS];

    for (size_t i = 0; i < MAX_SINGERS; ++i) {
        if (pthread_create(&workers[i], NULL, singer_routine, &ctx)) {
            return EXIT_FAILURE;
        }
    }

    for (size_t i = 0; i < MAX_SINGERS; ++i) {
        pthread_join(workers[i], NULL);
    }

    pthread_mutex_destroy(&ctx.lock);
    pthread_cond_destroy(&ctx.signal);

    putchar('\n');
    return EXIT_SUCCESS;
}

static void *singer_routine(void *arg)
{
    chorus_ctx_t *ctx = (chorus_ctx_t *)arg;

    pthread_mutex_lock(&ctx->lock);
    int my_slot = ctx->slot_id;
    ctx->slot_id++;
    if (my_slot == MAX_SINGERS) {
        pthread_cond_broadcast(&ctx->signal);
    }
    pthread_mutex_unlock(&ctx->lock);

    for (;;) {
        pthread_mutex_lock(&ctx->lock);

        char ch = ctx->text[ctx->cursor];

        if (ch == '\0') {
            pthread_cond_broadcast(&ctx->signal);
            pthread_mutex_unlock(&ctx->lock);
            return NULL;
        }

        if (ch != my_slot) {
            pthread_cond_wait(&ctx->signal, &ctx->lock);
        } else {
            putchar(my_slot);
            ctx->cursor++;
            pthread_cond_broadcast(&ctx->signal);
        }

        pthread_mutex_unlock(&ctx->lock);
    }
}
