#ifndef EXT4_H
#define EXT4_H

#include <stddef.h>
#include <stdint.h>

#ifndef EXT4_NAME_MAX
#define EXT4_NAME_MAX 255
#endif

#define EXT4_BLOCK_MAX 65536U

typedef int (*ext4_read_bytes_fn)(uint64_t offset, void *buffer, uint32_t size);

typedef int (*ext4_write_bytes_fn)(uint64_t offset, const void *buffer, uint32_t size);

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
    uint64_t offset_tabla_descriptores;

    uint64_t total_bloques;
    uint32_t total_grupos;
    uint32_t primer_inodo;
    uint32_t caracteristicas_compat;
    uint32_t bloques_libres;
    uint32_t inodos_libres;
    uint32_t semilla_checksum;
    uint8_t uuid[16];
} ext4_info_t;

typedef struct {
    uint16_t modo;
    uint16_t enlaces;
    uint32_t tamano;
    uint32_t tamano_alto;
    uint32_t flags;
    uint32_t sectores;
    uint8_t i_block[60];
} ext4_inodo_t;

typedef struct {
    uint32_t inodo;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char nombre[EXT4_NAME_MAX + 1];
} ext4_dir_entry_t;

int ext4_montar(ext4_read_bytes_fn leer, uint64_t inicio_particion);
void ext4_set_escribir(ext4_write_bytes_fn escribir);
const ext4_info_t *ext4_obtener_info(void);

int ext4_leer_inodo(uint32_t numero_inodo, ext4_inodo_t *inodo);

int ext4_listar_directorio(uint32_t numero_inodo, ext4_dir_entry_t *entradas, int max_entradas);

int ext4_leer_archivo(uint32_t numero_inodo, char *buffer, uint32_t capacidad, uint32_t *leido);

int ext4_buscar_en(uint32_t directorio, const char *nombre, ext4_dir_entry_t *salida);

int ext4_inodo_es_directorio(uint32_t numero_inodo);

int ext4_crear(uint32_t directorio, const char *nombre, int es_directorio, uint32_t *nuevo_inodo);

int ext4_escribir_archivo(uint32_t numero_inodo, const char *datos, uint32_t longitud);

int ext4_directorio_vacio(uint32_t numero_inodo);

int ext4_eliminar(uint32_t directorio, const char *nombre);
int ext4_eliminar_recursivo(uint32_t directorio, const char *nombre);

int ext4_crear_archivo(const char *nombre);
int ext4_crear_directorio(const char *nombre);
int ext4_eliminar_archivo(const char *nombre);
int ext4_eliminar_directorio(const char *nombre);
int ext4_buscar_nombre(const char *nombre, uint32_t *inodo_out);

#endif
