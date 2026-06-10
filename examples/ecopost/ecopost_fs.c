#define _POSIX_C_SOURCE 200809L

#include "ecopost_fs.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

int make_dir(const char* path) {
    if (mkdir(path, 0777) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

int remove_tree(const char* path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        return errno == ENOENT ? 0 : -1;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(path);
        if (!dir) {
            return -1;
        }
        struct dirent* entry = NULL;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            char child[1024];
            int n = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
            if (n <= 0 || (size_t)n >= sizeof(child) || remove_tree(child) != 0) {
                closedir(dir);
                return -1;
            }
        }
        if (closedir(dir) != 0) {
            return -1;
        }
        return rmdir(path);
    }

    return unlink(path);
}

void sleep_ms(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

void path_join(char* out, size_t out_size, const char* a, const char* b) {
    snprintf(out, out_size, "%s/%s", a, b);
}
