#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "color.h"
#include <unistd.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>

struct flags {
    bool recursive_flag;
    bool long_flag;
    bool all_flag;
    bool inode_flag;
};

void flags_ctor(struct flags* fl) {
    fl->recursive_flag = false;
    fl->long_flag = false; 
    fl->all_flag = false; 
    fl->inode_flag = false;
}

static void my_stat(struct dirent* entry, struct stat* file_stat) {
    if (stat(entry->d_name, file_stat)) {
        perror("anable to stat");
        exit(1);
    }
}

static void print_list(struct dirent* entry, struct stat file_stat, bool long_format) {
    if (!long_format) {
        if (S_ISDIR(file_stat.st_mode)) {
            printf(BLUE "%s  " RESET, entry->d_name);
        } else if (S_ISREG(file_stat.st_mode) && (file_stat.st_mode & 0111)) {
            printf(GREEN "%s  " RESET, entry->d_name);
        } else if (S_ISREG(file_stat.st_mode)) {
            printf("%s  ", entry->d_name);
        } else {
            printf(RED "%s  " RESET, entry->d_name); 
        }
        return;
    }

    char mode[11];
    strcpy(mode, "----------");
    if (S_ISDIR(file_stat.st_mode)) mode[0] = 'd';
    else if (S_ISLNK(file_stat.st_mode)) mode[0] = 'l';
    else if (S_ISFIFO(file_stat.st_mode)) mode[0] = 'p';
    else if (S_ISSOCK(file_stat.st_mode)) mode[0] = 's';
    else if (S_ISCHR(file_stat.st_mode)) mode[0] = 'c';
    else if (S_ISBLK(file_stat.st_mode)) mode[0] = 'b';

    if (file_stat.st_mode & S_IRUSR) mode[1] = 'r';
    if (file_stat.st_mode & S_IWUSR) mode[2] = 'w';
    if (file_stat.st_mode & S_IXUSR) mode[3] = 'x';
    if (file_stat.st_mode & S_IRGRP) mode[4] = 'r';
    if (file_stat.st_mode & S_IWGRP) mode[5] = 'w';
    if (file_stat.st_mode & S_IXGRP) mode[6] = 'x';
    if (file_stat.st_mode & S_IROTH) mode[7] = 'r';
    if (file_stat.st_mode & S_IWOTH) mode[8] = 'w';
    if (file_stat.st_mode & S_IXOTH) mode[9] = 'x';

    struct passwd *pw = getpwuid(file_stat.st_uid);
    struct group  *gr = getgrgid(file_stat.st_gid);
    const char *owner = pw ? pw->pw_name : "unknown";
    const char *group = gr ? gr->gr_name : "unknown";

    long long size = (long long)file_stat.st_size;

    char time_buf[20];
    struct tm *tm = localtime(&file_stat.st_mtime);
    if (tm) {
        strftime(time_buf, sizeof(time_buf), "%b %d %H:%M", tm);
    } else {
        strcpy(time_buf, "??? ?? ??:??");
    }

    printf("%s %3ld %8s %8s %8lld %s ", 
           mode,
           (long)file_stat.st_nlink,
           owner,
           group,
           size,
           time_buf);

    if (S_ISDIR(file_stat.st_mode)) {
        printf(BLUE "%s" RESET, entry->d_name);
    } else if (S_ISREG(file_stat.st_mode) && (file_stat.st_mode & 0111)) {
        printf(GREEN "%s" RESET, entry->d_name);
    } else {
        printf("%s", entry->d_name);
    }
    putchar('\n');
}

static void get_options(struct flags* fl, int argc, char* argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "Ralh")) != -1) {
        switch (opt) {
            case 'R': {
                fl->recursive_flag = true;
                break;
            }
            case 'l': {
                fl->long_flag = true;
                break;
            }
            case 'a': {
                fl->all_flag = true;
                break;
            }
            case 'i': {
                fl->inode_flag = true; 
                break;
            }
            default:
                perror("error in get_options");
                exit(1);
        }
    }
}

static void open_dir_and_print_list(const char* const path, char subdirs[][256], size_t* subdir_count, const struct flags* fl) {
    DIR* directory = opendir(path);
    if (!directory) {
        perror("unable to open directory");
        exit(1);
    }

    if (subdir_count) {
        *subdir_count = 0;
    }

    struct dirent *entry;
    struct stat file_stat;

    while ((entry = readdir(directory))) {
        if (!fl->all_flag && entry->d_name[0] == '.') continue;

        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        if (stat(full_path, &file_stat) != 0) {
            continue;
        }

        print_list(entry, file_stat, fl->long_flag);

        if (subdirs && subdir_count && S_ISDIR(file_stat.st_mode)) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            if (*subdir_count < 1024) {
                strncpy(subdirs[*subdir_count], entry->d_name, 255);
                subdirs[*subdir_count][255] = '\0';
                (*subdir_count)++;
            }
            }
        }
    }
    putchar('\n');
    closedir(directory);
}

static void recursive_ls(const char* path, int depth, const struct flags* fl) {
    char subdirs[1024][256]; 
    size_t subdir_count = 0;

    open_dir_and_print_list(path, subdirs, &subdir_count, fl);

    for (size_t i = 0; i < subdir_count; i++) {
        char full_path[4096];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, subdirs[i]);

        if (depth == 0) {
            printf("\n%s:\n", full_path);
        } else {
            printf("%s:\n", full_path);
        }

        recursive_ls(full_path, depth + 1, fl);
    }
}

static void run(const struct flags* fl) {
    if (!fl->recursive_flag) {
        open_dir_and_print_list(".", NULL, NULL, fl);
    } else {
        printf(".:\n");
        recursive_ls(".", 0, fl);
    }
}

int main(int argc, char* argv[]) {
    struct flags fl = {};
    flags_ctor(&fl);
    get_options(&fl, argc, argv);
    run(&fl);
    return EXIT_SUCCESS;
}