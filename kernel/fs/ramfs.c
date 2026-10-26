
#include "ramfs.h"

typedef struct {
    int usado;
    char nombre[RAMFS_NAME_MAX];
    char datos[RAMFS_DATA_MAX];
    size_t longitud;
} ramfs_file_t;

static ramfs_file_t archivos[RAMFS_MAX_FILES];

static size_t copiar(char *destino, const char *origen, size_t capacidad) {
    size_t i = 0;

    if (capacidad == 0)
        return 0;

    while (i + 1 < capacidad && origen[i] != '\0') {
        destino[i] = origen[i];
        i++;
    }

    destino[i] = '\0';
    return i;
}

static int buscar(const char *nombre) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!archivos[i].usado)
            continue;

        size_t j = 0;

        while (nombre[j] != '\0' && archivos[i].nombre[j] != '\0' && nombre[j] == archivos[i].nombre[j]) {
            j++;
        }

        if (nombre[j] == '\0' && archivos[i].nombre[j] == '\0') {
            return i;
        }
    }

    return -1;
}

void ramfs_init(void) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        archivos[i].usado = 0;
        archivos[i].longitud = 0;
        archivos[i].nombre[0] = '\0';
        archivos[i].datos[0] = '\0';
    }
}

int ramfs_create(const char *nombre) {
    if (nombre == NULL || nombre[0] == '\0')
        return 0;

    size_t longitud = 0;

    while (nombre[longitud] != '\0') {
        if (longitud >= RAMFS_NAME_MAX - 1)
            return 0;
        longitud++;
    }

    if (buscar(nombre) >= 0)
        return 0;

    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!archivos[i].usado) {
            copiar(archivos[i].nombre, nombre, RAMFS_NAME_MAX);
            archivos[i].datos[0] = '\0';
            archivos[i].longitud = 0;
            archivos[i].usado = 1;
            return 1;
        }
    }

    return 0;
}

int ramfs_write(const char *nombre, const char *datos) {
    if (nombre == NULL || datos == NULL)
        return 0;

    int indice = buscar(nombre);

    if (indice < 0)
        return 0;

    size_t longitud = 0;

    while (datos[longitud] != '\0') {
        if (longitud >= RAMFS_DATA_MAX - 1)
            return 0;
        longitud++;
    }

    copiar(archivos[indice].datos, datos, RAMFS_DATA_MAX);
    archivos[indice].longitud = longitud;

    return 1;
}

int ramfs_read(const char *nombre, char *buffer, size_t capacidad) {
    if (nombre == NULL || buffer == NULL || capacidad == 0)
        return 0;

    int indice = buscar(nombre);

    if (indice < 0)
        return 0;

    if (archivos[indice].longitud >= capacidad)
        return 0;

    copiar(buffer, archivos[indice].datos, capacidad);

    return 1;
}

int ramfs_list(char *buffer, size_t capacidad) {
    if (buffer == NULL || capacidad == 0)
        return 0;

    size_t posicion = 0;
    buffer[0] = '\0';

    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!archivos[i].usado)
            continue;

        size_t longitud = 0;

        while (archivos[i].nombre[longitud] != '\0')
            longitud++;

        if (posicion + longitud + 2 > capacidad)
            return 0;

        for (size_t j = 0; j < longitud; j++)
            buffer[posicion++] = archivos[i].nombre[j];

        buffer[posicion++] = '\n';
    }

    buffer[posicion] = '\0';
    return 1;
}
