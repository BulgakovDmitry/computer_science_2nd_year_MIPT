#include <sys/mman.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

static void die(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

long get_file_size(int fd) {
    struct stat file_stat;
    
    if (fstat(fd, &file_stat) == -1) {
        die("fstat error: %s", strerror(errno));
    }
    
    return file_stat.st_size;
}

static int my_open(const char* f, int of, int mode) {
    int fd = open(f, of, mode);
    if (fd < 0) {
        die("open error for %s: %s", f, strerror(errno));
    }
    return fd;
}

static void my_ftruncate(int fd, off_t size) {
    if (ftruncate(fd, size) != 0) {
        die("ftruncate error: %s", strerror(errno));
    }
}

static void* my_mmap(void* addr, off_t size, int prot, int flags, int fd, off_t offset) {
    void* result = mmap(addr, size, prot, flags, fd, offset);
    if (result == MAP_FAILED) {
        die("mmap error: %s", strerror(errno));
    }
    return result;
}

static void my_munmap(void* addr, off_t size) {
    if (munmap(addr, size) != 0) {
        die("munmap error: %s", strerror(errno));
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "error, expected 3 param\n");
        return EXIT_FAILURE;
    }

    int fd_from = my_open(argv[1], O_RDONLY, 0);
    int fd_to   = my_open(argv[2], O_RDWR | O_CREAT | O_TRUNC, 0644);

    long fd_from_size = get_file_size(fd_from);
    my_ftruncate(fd_to, fd_from_size);

    void* mapped_from = my_mmap(NULL, fd_from_size, PROT_READ, MAP_PRIVATE, fd_from, 0);
    void* mapped_to   = my_mmap(NULL, fd_from_size, PROT_WRITE, MAP_SHARED, fd_to, 0);

    memcpy(mapped_to, mapped_from, fd_from_size);

    my_munmap(mapped_from, fd_from_size);
    my_munmap(mapped_to,   fd_from_size);

    close(fd_from);
    close(fd_to);

    return EXIT_SUCCESS;

}