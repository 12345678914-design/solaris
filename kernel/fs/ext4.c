
#include "ext4.h"

#define EXT4_SUPER_OFFSET 1024
#define EXT4_SUPER_SIZE 1024
#define EXT4_MAGIC 0xEF53

#define EXT4_FEATURE_INCOMPAT_64BIT 0x0080

static ext4_info_t informacion;
static int ext4_disponible = 0;

static ext4_read_bytes_fn ext4_leer_disco = 0;
static uint64_t ext4_inicio = 0;

static uint16_t leer_u16(const uint8_t *p)
{
    return (uint16_t)p[0]
         | ((uint16_t)p[1] << 8);
}

static uint32_t leer_u32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static int ext4_leer_absoluto(
    uint64_t offset,
    void *buffer,
    uint32_t size
)
{
    if (!ext4_leer_disco)
        return -1;

    return ext4_leer_disco(
        ext4_inicio + offset,
        buffer,
        size
    );
}

int ext4_montar(
    ext4_read_bytes_fn leer,
    uint64_t inicio_particion
)
{
    uint8_t super[EXT4_SUPER_SIZE];

    ext4_disponible = 0;
    ext4_leer_disco = 0;

    if (!leer)
        return -1;

    if (leer(
            inicio_particion + EXT4_SUPER_OFFSET,
            super,
            EXT4_SUPER_SIZE
        ) != 0) {
        return -2;
    }

    if (leer_u16(&super[0x38]) != EXT4_MAGIC)
        return -3;

    uint32_t log_bloque = leer_u32(&super[0x18]);

    if (log_bloque > 6)
        return -4;

    informacion.total_inodos =
        leer_u32(&super[0x00]);

    informacion.bloques_bajos =
        leer_u32(&super[0x04]);

    informacion.primer_bloque_datos =
        leer_u32(&super[0x14]);

    informacion.bloques_por_grupo =
        leer_u32(&super[0x20]);

    informacion.inodos_por_grupo =
        leer_u32(&super[0x28]);

    informacion.tamano_bloque = 1024U << log_bloque;

    informacion.tamano_inodo =
        leer_u16(&super[0x58]);

    informacion.caracteristicas_incompatibles =
        leer_u32(&super[0x60]);

    informacion.caracteristicas_solo_lectura =
        leer_u32(&super[0x64]);

    /*
     * Los descriptores suelen medir 32 bytes.
     * Con la característica 64BIT pueden medir 64 bytes
     * o más, según la versión del formato.
     */
    if (informacion.caracteristicas_incompatibles
            & EXT4_FEATURE_INCOMPAT_64BIT) {
        informacion.tamano_descriptor_grupo =
            leer_u16(&super[0xFE]);

        if (informacion.tamano_descriptor_grupo < 64 ||
            informacion.tamano_descriptor_grupo > 64) {
            return -6;
        }
    } else {
        informacion.tamano_descriptor_grupo = 32;
    }

    if (informacion.total_inodos == 0 ||
        informacion.bloques_bajos == 0 ||
        informacion.bloques_por_grupo == 0 ||
        informacion.inodos_por_grupo == 0 ||
        informacion.tamano_inodo < 128 ||
        informacion.tamano_inodo > informacion.tamano_bloque) {
        return -5;
    }

    ext4_inicio = inicio_particion;
    ext4_leer_disco = leer;
    ext4_disponible = 1;

    return 0;
}

const ext4_info_t *ext4_obtener_info(void)
{
    if (!ext4_disponible)
        return 0;

    return &informacion;
}

int ext4_leer_inodo(
    uint32_t numero_inodo,
    ext4_inodo_t *inodo
)
{
    uint8_t descriptor[64];
    uint8_t datos_inodo[256];

    if (!ext4_disponible || !inodo)
        return -1;

    if (numero_inodo == 0 ||
        numero_inodo > informacion.total_inodos)
        return -2;

    if (informacion.tamano_inodo > sizeof(datos_inodo))
        return -3;

    uint32_t indice = numero_inodo - 1;

    uint32_t grupo =
        indice / informacion.inodos_por_grupo;

    uint32_t indice_grupo =
        indice % informacion.inodos_por_grupo;

    /*
     * La tabla de descriptores comienza después
     * del superbloque dentro del área de datos.
     */
    uint64_t offset_gdt =
        (uint64_t)(informacion.primer_bloque_datos + 1)
        * informacion.tamano_bloque;

    uint64_t offset_descriptor =
        offset_gdt +
        (uint64_t)grupo *
        informacion.tamano_descriptor_grupo;

    if (ext4_leer_absoluto(
            offset_descriptor,
            descriptor,
            informacion.tamano_descriptor_grupo
        ) != 0) {
        return -4;
    }

    /* bg_inode_table: desplazamiento 0x08 */
    uint64_t bloque_tabla = leer_u32(&descriptor[0x08]);

    if (informacion.caracteristicas_incompatibles
            & EXT4_FEATURE_INCOMPAT_64BIT) {
        bloque_tabla |=
            (uint64_t)leer_u32(&descriptor[0x28]) << 32;
    }

    uint64_t offset_inodo =
        bloque_tabla * informacion.tamano_bloque +
        (uint64_t)indice_grupo * informacion.tamano_inodo;

    if (ext4_leer_absoluto(
            offset_inodo,
            datos_inodo,
            informacion.tamano_inodo
        ) != 0) {
        return -5;
    }

    inodo->modo = leer_u16(&datos_inodo[0]);
    inodo->tamano = leer_u32(&datos_inodo[4]);
    inodo->tamano_alto = 0;
    inodo->flags = leer_u32(&datos_inodo[32]);

    /*
     * i_size_high está en 0x6C en los inodos ext4
     * convencionales de al menos 112 bytes.
     */
    if (informacion.tamano_inodo >= 112) {
        inodo->tamano_alto = leer_u32(&datos_inodo[108]);
    }

    for (uint32_t i = 0; i < 15; i++) {
        inodo->bloques[i] =
            leer_u32(&datos_inodo[40 + i * 4]);
    }

    return 0;
}
