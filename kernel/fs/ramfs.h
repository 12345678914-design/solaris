
#ifndef RAMFS_H
#define RAMFS_H

#include <stddef.h>

#define RAMFS_MAX_FILES 32
#define RAMFS_NAME_MAX 32
#define RAMFS_DATA_MAX 256

void ramfs_init(void);

int ramfs_create(const char *name);
int ramfs_write(const char *name, const char *data);
int ramfs_read(const char *name, char *buffer, size_t capacity);
int ramfs_list(char *buffer, size_t capacity);

#endif
