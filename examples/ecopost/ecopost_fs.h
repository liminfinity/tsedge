#ifndef ECOPOST_FS_H
#define ECOPOST_FS_H

#include <stddef.h>

/* Creates a directory when it does not already exist. */
int make_dir(const char* path);

/* Removes a demo directory tree before a reset. */
int remove_tree(const char* path);

/* Sleeps for the requested number of milliseconds in the live loop. */
void sleep_ms(int ms);

/* Builds a path from two components into a caller-provided buffer. */
void path_join(char* out, size_t out_size, const char* a, const char* b);

#endif
