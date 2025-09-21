#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
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

static const char* basename_const(const char* p) {
    const char* slash = strrchr(p, '/');
    return slash ? slash + 1 : p;
}

static bool is_directory(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool same_inode(const struct stat* a, const struct stat* b) {
    return a->st_ino == b->st_ino && a->st_dev == b->st_dev;
}

static void usage(const char* prog) {
    fprintf(stderr,
            "cp: invalid option %s\n", prog);
}

static bool confirm_overwrite(const char *dst) {
    fprintf(stderr, "cp: overwrite '%s'? ", dst);
    fflush(stderr);

    int c = EOF;
    c = getchar();
    while (c != '\n' && c != EOF) {
        int last = c;
        c = getchar();
        if (c == '\n' || c == EOF) { c = last; break; }
    }
    return c == 'y' || c == 'Y';
}

static int copy_regular(const char *src, const char *dst, const Options *opt) {
    struct stat sst;
    if (stat(src, &sst) < 0) {
        eperror("cannot stat '%s'", src);
        return -1;
    }
    if (!S_ISREG(sst.st_mode)) {
        eprintf("omitting non-regular file '%s'", src);
        return -1;
    }

    struct stat dstst;
    bool dst_exists = (stat(dst, &dstst) == 0);

    if (dst_exists && S_ISDIR(dstst.st_mode)) {
        eprintf("cannot overwrite directory '%s' with non-directory", dst);
        return -1;
    }

    if (dst_exists && same_inode(&sst, &dstst)) {
        eprintf("'%s' and '%s' are the same file", src, dst);
        return -1;
    }

    if (dst_exists) {
        if (opt->interactive && !confirm_overwrite(dst)) {
            return 0;
        }
        if (opt->force) {
            (void)unlink(dst);
        }
    }

    int sfd = open(src, O_RDONLY);
    if (sfd < 0) {
        eperror("cannot open '%s' for reading", src);
        return -1;
    }

    mode_t mode = sst.st_mode & 0777;
    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (dfd < 0) {
        int saved = errno;
        close(sfd);
        errno = saved;
        eperror("cannot create regular file '%s'", dst);
        return -1;
    }

    char buf[128 * 1024];
    int rc = 0;

    while (1) {
        ssize_t n = read(sfd, buf, sizeof buf);
        if (n == 0) break;
        if (n < 0) { eperror("error reading '%s'", src); rc = -1; break; }

        ssize_t off = 0;
        while (off < n) {
            ssize_t m = write(dfd, buf + off, (size_t)(n - off));
            if (m <= 0) { eperror("error writing '%s'", dst); rc = -1; off = n; break; }
            off += m;
        }
        if (rc != 0) break;
    }

    int err_close_src = close(sfd);
    int err_close_dst = close(dfd);
    if (rc == 0 && err_close_dst != 0) {
        eperror("failed to close '%s'", dst);
        rc = -1;
    }
    (void)err_close_src;

    if (rc == 0 && opt->verbose) {
        printf("'%s' -> '%s'\n", src, dst);
        fflush(stdout);
    }
    return rc;
}

static void build_target_in_dir(char out[PATH_MAX], const char* dir, const char* src) {
    const char* name = basename_const(src);
    size_t len = snprintf(out, PATH_MAX, "%s/%s", dir, name);
    if (len >= PATH_MAX) {
        out[PATH_MAX - 1] = '\0';
    }
}

int main(int argc, char* argv[]) {
    Options opt = {0};

    int idx = 1;
    for (; idx < argc; ++idx) {
        const char* a = argv[idx];
        if (strcmp(a, "--") == 0) { ++idx; break; }
        if (a[0] != '-') break;

        if (strcmp(a, "-v") == 0 || strcmp(a, "--verbose") == 0) {
            opt.verbose = true;
        } else if (strcmp(a, "-i") == 0 || strcmp(a, "--interactive") == 0) {
            opt.interactive = true;  
            opt.force = false;
        } else if (strcmp(a, "-f") == 0 || strcmp(a, "--force") == 0) {
            opt.force = true;
            opt.interactive = false;
        } else {
            usage(a);
            return 1;
        }
    }

    int n_paths = argc - idx;
    if (n_paths < 2) {
        usage(argv[0]);
        return 1;
    }

    int exit_code = 0;

    if (n_paths == 2) {
        const char *src = argv[idx];
        const char *dst = argv[idx + 1];

        if (is_directory(dst)) {
            char full[PATH_MAX];
            build_target_in_dir(full, dst, src);
            if (copy_regular(src, full, &opt) != 0) exit_code = 1;
        } else {
            if (copy_regular(src, dst, &opt) != 0) exit_code = 1;
        }
    } else {
        const char *dir = argv[argc - 1];
        if (!is_directory(dir)) {
            eprintf("target '%s' is not a directory", dir);
            return 1;
        }

        for (int i = idx; i < argc - 1; ++i) {
            char full[PATH_MAX];
            build_target_in_dir(full, dir, argv[i]);
            if (copy_regular(argv[i], full, &opt) != 0) exit_code = 1;
        }
    }

    return exit_code;
}