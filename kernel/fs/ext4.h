
#ifndef EXT4_H
#define EXT4_H

#include <stdint.h>

typedef int (*ext4_read_bytes_fn)(
    uint64_t offset,
    void *buffer,
    uint32_t size
);

typedef struct {
    uint32_t total_inodos;
    uint32_t bloques_bajos;
    uint32_t bloques_por_grupo;
    uint32_t inodos_por_grupo;
    uint32_t tamano_bloque;
    uint16_t tamano_inodo;
    uint32_t caracteristicas_incompatibles;
    uint32_t caracteristicas_solo_lectura;

    uint32_t primer_bloque_datos;
    uint16_t tamano_descriptor_grupo;
} ext4_info_t;

typedef struct {
    uint16_t modo;
    uint32_t tamano;
    uint32_t tamano_alto;
    uint32_t flags;
    uint8_t bloque_raw[60];
} ext4_inodo_t;

int ext4_montar(
    ext4_read_bytes_fn leer,
    uint64_t inicio_particion
);

const ext4_info_t *ext4_obtener_info(void);

int ext4_leer_inodo(
    uint32_t numero_inodo,
    ext4_inodo_t *inodo
);

#endif
