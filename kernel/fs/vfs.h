
#ifndef VFS_H
#define VFS_H

#include <stddef.h>
#include <stdint.h>

#define VFS_NAME_MAX 256
#define VFS_PATH_MAX 512
#define VFS_MAX_FILES 64

typedef enum {
    VFS_TIPO_DESCONOCIDO,
    VFS_TIPO_ARCHIVO,
    VFS_TIPO_CARPETA
} vfs_tipo_t;

typedef struct {
    char nombre[VFS_NAME_MAX];
    vfs_tipo_t tipo;
    uint32_t tamano;
    uint32_t inodo;
} vfs_entrada_t;

void vfs_init(void);
int vfs_montar_ext4(void);

const char *vfs_get_cwd(void);

int vfs_cd(const char *ruta);

int vfs_ls(
    const char *ruta,
    vfs_entrada_t *entradas,
    int max
);

int vfs_cat(
    const char *ruta,
    char *buffer,
    size_t capacidad
);

int vfs_mkdir(const char *ruta);
int vfs_touch(const char *ruta);

int vfs_write(const char *ruta, const char *datos);
int vfs_crear_archivo(const char *ruta);

int vfs_rm(const char *ruta);
int vfs_rrm(const char *ruta);
int vfs_rmdir(const char *ruta);

#endif
