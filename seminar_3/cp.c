#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    bool verbose;
    bool interactive;
    bool force;
} Options;

static void eprintf(const char *fmt, ...) {
    va_list ap;
    fputs("cp: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}
static void eperror(const char *fmt, ...) {
    va_list ap;
    fputs("cp: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, ": %s\n", strerror(errno));
}

static inline const char* base(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}
static inline bool is_dir(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}
static inline bool same_file(const struct stat *a, const struct stat *b) {
    return a->st_ino == b->st_ino && a->st_dev == b->st_dev;
}

static bool ask_overwrite(const char *dst){
    fprintf(stderr, "cp: overwrite '%s'? ", dst);
    fflush(stderr);
    int c = getchar();
    int d; while ((d = getchar()) != '\n' && d != EOF) {}
    return c=='y' || c=='Y';
}

static int copy1(const char *src, const char *dst, const Options *opt) {
    struct stat ss, ds;
    if (stat(src, &ss) < 0) { eperror("cannot stat '%s'", src); return -1; }
    if (!S_ISREG(ss.st_mode)) { eprintf("omitting non-regular file '%s'", src); return -1; }

    bool dst_exists = (stat(dst, &ds) == 0);
    if (dst_exists && S_ISDIR(ds.st_mode)) { eprintf("cannot overwrite directory '%s' with non-directory", dst); return -1; }
    if (dst_exists && same_file(&ss, &ds)) { eprintf("'%s' and '%s' are the same file", src, dst); return -1; }

    if (dst_exists) {
        if (opt->interactive && !ask_overwrite(dst)) return 0;  
        if (opt->force) (void)unlink(dst);                      
    }

    int in  = open(src, O_RDONLY);
    if (in  < 0) { eperror("cannot open '%s' for reading", src); return -1; }
    int out = open(dst, O_WRONLY|O_CREAT|O_TRUNC, ss.st_mode & 0777);
    if (out < 0) { int e=errno; close(in); errno=e; eperror("cannot create regular file '%s'", dst); return -1; }

    char buf[128*1024];
    int rc = 0;
    while (1) {
        ssize_t n = read(in, buf, sizeof buf);
        if (n == 0) break;
        if (n < 0) { eperror("error reading '%s'", src); rc=-1; break; }
        for (ssize_t off=0; off<n; ) {
            ssize_t m = write(out, buf+off, (size_t)(n-off));
            if (m <= 0) { eperror("error writing '%s'", dst); rc=-1; break; }
            off += m;
        }
        if (rc) break;
    }

    if (close(in)  != 0) rc = -1;
    if (close(out) != 0) rc = -1;

    if (!rc && opt->verbose) printf("'%s' -> '%s'\n", src, dst);
    return rc;
}

static void usage(const char *prog){
    fprintf(stderr,
        "Usage: %s [OPTION]... SOURCE DEST\n"
        "  or:  %s [OPTION]... SOURCE... DIRECTORY\n"
        "Options:\n"
        "  -v, --verbose       explain what is being done\n"
        "  -i, --interactive   prompt before overwrite\n"
        "  -f, --force         remove destination files before copying\n",
        prog, prog);
}

int main(int argc, char **argv) {
    Options opt = {0};

    static const struct option longopts[] = {
        {"verbose",     no_argument, 0, 'v'},
        {"interactive", no_argument, 0, 'i'},
        {"force",       no_argument, 0, 'f'},
        {0,0,0,0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "vif", longopts, NULL)) != -1) {
        switch (c) {
            case 'v': opt.verbose = true; break;
            case 'i': opt.interactive = true; opt.force = false; break;
            case 'f': opt.force = true;       opt.interactive = false; break;
            case '?': usage(argv[0]); return 1;
        }
    }

    int n_paths = argc - optind;
    if (n_paths < 2) { usage(argv[0]); return 1; }

    int rc = 0;
    if (n_paths == 2) {
        const char *src = argv[optind];
        const char *dst = argv[optind + 1];
        if (is_dir(dst)) {
            char to[PATH_MAX]; snprintf(to, sizeof to, "%s/%s", dst, base(src));
            if (copy1(src, to, &opt) != 0) rc = 1;
        } else {
            if (copy1(src, dst, &opt) != 0) rc = 1;
        }
    } else {
        const char *dir = argv[argc - 1];
        if (!is_dir(dir)) { eprintf("target '%s' is not a directory", dir); return 1; }
        for (int i = optind; i < argc - 1; ++i) {
            char to[PATH_MAX]; snprintf(to, sizeof to, "%s/%s", dir, base(argv[i]));
            if (copy1(argv[i], to, &opt) != 0) rc = 1;
        }
    }
    return rc;
}
