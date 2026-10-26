#include "ext4.h"

/* ------------------------------------------------------------------ */
/* Constantes del formato ext4                                        */
/* ------------------------------------------------------------------ */

#define EXT4_SUPER_OFFSET 1024
#define EXT4_SUPER_SIZE 1024
#define EXT4_MAGIC 0xEF53
#define EXT4_EXT_MAGIC 0xF30A
#define EXT4_EXT_MAX_ENTRIES 4

#define EXT4_FEATURE_INCOMPAT_FILETYPE 0x0002
#define EXT4_FEATURE_INCOMPAT_META_BG 0x0010
#define EXT4_FEATURE_INCOMPAT_EXTENTS 0x0040
#define EXT4_FEATURE_INCOMPAT_64BIT 0x0080
#define EXT4_FEATURE_INCOMPAT_CSUM_SEED 0x2000
#define EXT4_FEATURE_INCOMPAT_LARGEDIR 0x4000
#define EXT4_FEATURE_INCOMPAT_INLINE_DATA 0x8000

#define EXT4_FEATURE_RO_COMPAT_METADATA_CSUM 0x0400

#define EXT4_EXTENTS_FL 0x00080000
#define EXT4_INDEX_FL 0x00001000
#define EXT4_INLINE_DATA_FL 0x10000000

#define EXT4_S_IFMT 0xF000
#define EXT4_S_IFDIR 0x4000
#define EXT4_S_IFREG 0x8000
#define EXT4_ROOT_INO 2
#define EXT4_GOOD_OLD_INODE_SIZE 128

#define EXT4_BG_INODE_UNINIT 0x0001
#define EXT4_BG_BLOCK_UNINIT 0x0002

/* ------------------------------------------------------------------ */
/* Estado interno                                                     */
/* ------------------------------------------------------------------ */

static ext4_info_t informacion;
static int ext4_disponible = 0;
static int ext4_escritura = 0;
static int ext4_metadata_csum = 0;
static ext4_read_bytes_fn ext4_leer_disco = 0;
static ext4_write_bytes_fn ext4_escribir_disco = 0;
static uint64_t ext4_inicio_particion = 0;

static uint8_t superbloque[EXT4_SUPER_SIZE];

static uint8_t buffer_a[EXT4_BLOCK_MAX];
static uint8_t buffer_b[EXT4_BLOCK_MAX];
static uint8_t buffer_bitmap[EXT4_BLOCK_MAX];

/* ------------------------------------------------------------------ */
/* Utilidades de bytes                                                */
/* ------------------------------------------------------------------ */

static uint16_t leer_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t leer_u32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void escribir_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void escribir_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void escribir_u64(uint8_t *p, uint64_t v)
{
    escribir_u32(p, (uint32_t)(v & 0xFFFFFFFFu));
    escribir_u32(p + 4, (uint32_t)(v >> 32));
}

static void copiar_bytes(void *destino, const void *origen, uint32_t cantidad)
{
    uint8_t *d = (uint8_t *)destino;
    const uint8_t *o = (const uint8_t *)origen;
    for (uint32_t i = 0; i < cantidad; i++)
        d[i] = o[i];
}

static void llenar_ceros(void *destino, uint32_t cantidad)
{
    uint8_t *d = (uint8_t *)destino;
    for (uint32_t i = 0; i < cantidad; i++)
        d[i] = 0;
}

static uint32_t longitud_nombre(const char *s)
{
    uint32_t n = 0;
    if (s == 0)
        return 0;
    while (s[n] != '\0' && n < (EXT4_NAME_MAX + 1))
        n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* CRC32C (variante usada por e2fsprogs/ext4: sin complemento)        */
/* ------------------------------------------------------------------ */

static uint32_t tabla_crc32c[256];
static int tabla_crc_lista = 0;

static void crc32c_iniciar_tabla(void)
{
    if (tabla_crc_lista)
        return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            if (c & 1)
                c = (c >> 1) ^ 0x82F63B78u;
            else
                c >>= 1;
        }
        tabla_crc32c[i] = c;
    }
    tabla_crc_lista = 1;
}

static uint32_t crc32c(uint32_t crc, const void *datos, uint32_t cantidad)
{
    const uint8_t *p = (const uint8_t *)datos;
    crc32c_iniciar_tabla();
    while (cantidad--)
        crc = tabla_crc32c[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return crc;
}

/* ------------------------------------------------------------------ */
/* Acceso al disco                                                    */
/* ------------------------------------------------------------------ */

static int leer_absoluto(uint64_t offset, void *buffer, uint32_t size)
{
    if (!ext4_leer_disco || !buffer)
        return -1;
    return ext4_leer_disco(ext4_inicio_particion + offset, buffer, size);
}

static int escribir_absoluto(uint64_t offset, const void *buffer, uint32_t size)
{
    if (!ext4_escribir_disco || !ext4_escritura || !buffer)
        return -1;
    return ext4_escribir_disco(ext4_inicio_particion + offset, buffer, size);
}

static int leer_bloque(uint64_t bloque, void *buffer)
{
    return leer_absoluto(bloque * (uint64_t)informacion.tamano_bloque,
                         buffer,
                         informacion.tamano_bloque);
}

static int escribir_bloque(uint64_t bloque, const void *buffer)
{
    return escribir_absoluto(bloque * (uint64_t)informacion.tamano_bloque,
                             buffer,
                             informacion.tamano_bloque);
}

/* ------------------------------------------------------------------ */
/* Superbloque                                                        */
/* ------------------------------------------------------------------ */

static void superbloque_actualizar_checksum(void)
{
    if (!ext4_metadata_csum)
        return;
    escribir_u32(&superbloque[0x3FC],
                 crc32c(0xFFFFFFFFu, superbloque, 0x3FC));
}

static int superbloque_guardar(void)
{
    if (!ext4_escritura)
        return -1;
    superbloque_actualizar_checksum();
    return escribir_absoluto(EXT4_SUPER_OFFSET, superbloque, EXT4_SUPER_SIZE);
}

static void superbloque_sumar_bloques(int32_t delta)
{
    uint64_t libres = (uint64_t)leer_u32(&superbloque[0x0C]);
    if (informacion.caracteristicas_incompatibles & EXT4_FEATURE_INCOMPAT_64BIT)
        libres |= ((uint64_t)leer_u32(&superbloque[0x158]) << 32);

    if (delta < 0) {
        uint64_t resta = (uint64_t)(-delta);
        libres = (libres > resta) ? (libres - resta) : 0;
    } else {
        libres += (uint64_t)delta;
    }

    escribir_u32(&superbloque[0x0C], (uint32_t)(libres & 0xFFFFFFFFu));
    if (informacion.caracteristicas_incompatibles & EXT4_FEATURE_INCOMPAT_64BIT)
        escribir_u32(&superbloque[0x158], (uint32_t)(libres >> 32));

    informacion.bloques_libres = (uint32_t)libres;
}

static void superbloque_sumar_inodos(int32_t delta)
{
    uint32_t libres = leer_u32(&superbloque[0x10]);
    if (delta < 0) {
        uint32_t resta = (uint32_t)(-delta);
        libres = (libres > resta) ? (libres - resta) : 0;
    } else {
        libres += (uint32_t)delta;
    }
    escribir_u32(&superbloque[0x10], libres);
    informacion.inodos_libres = libres;
}

/* ------------------------------------------------------------------ */
/* Descriptores de grupo                                              */
/* ------------------------------------------------------------------ */

static uint64_t desc_obtener_u64(const uint8_t *desc, uint32_t off_lo, uint32_t off_hi)
{
    uint64_t v = (uint64_t)leer_u32(desc + off_lo);
    if (informacion.tamano_descriptor_grupo >= 64)
        v |= ((uint64_t)leer_u32(desc + off_hi) << 32);
    return v;
}

static int desc_leer(uint32_t grupo, uint8_t *desc)
{
    uint64_t off = informacion.offset_tabla_descriptores +
                   (uint64_t)grupo * (uint64_t)informacion.tamano_descriptor_grupo;
    llenar_ceros(desc, 64);
    return leer_absoluto(off, desc, informacion.tamano_descriptor_grupo);
}

static uint16_t desc_calcular_checksum(uint32_t grupo, const uint8_t *desc)
{
    uint8_t copia[64];
    uint32_t semilla = informacion.semilla_checksum;
    uint8_t numero[4];

    copiar_bytes(copia, desc, informacion.tamano_descriptor_grupo);
    escribir_u16(&copia[0x1E], 0);
    escribir_u32(numero, grupo);

    uint32_t c = crc32c(semilla, numero, 4);
    c = crc32c(c, copia, informacion.tamano_descriptor_grupo);
    return (uint16_t)(c & 0xFFFF);
}

static int desc_escribir(uint32_t grupo, uint8_t *desc)
{
    if (!ext4_escritura)
        return -1;
    if (ext4_metadata_csum)
        escribir_u16(&desc[0x1E], desc_calcular_checksum(grupo, desc));
    uint64_t off = informacion.offset_tabla_descriptores +
                   (uint64_t)grupo * (uint64_t)informacion.tamano_descriptor_grupo;
    return escribir_absoluto(off, desc, informacion.tamano_descriptor_grupo);
}

static void desc_actualizar_csum_bitmap_bloques(uint8_t *desc, const uint8_t *bitmap)
{
    if (!ext4_metadata_csum)
        return;
    uint32_t c = crc32c(informacion.semilla_checksum, bitmap, informacion.tamano_bloque);
    escribir_u16(&desc[0x18], (uint16_t)(c & 0xFFFF));
    if (informacion.tamano_descriptor_grupo >= 64)
        escribir_u16(&desc[0x38], (uint16_t)(c >> 16));
}

static void desc_actualizar_csum_bitmap_inodos(uint8_t *desc, const uint8_t *bitmap)
{
    if (!ext4_metadata_csum)
        return;
    uint32_t bytes = informacion.inodos_por_grupo / 8;
    uint32_t c = crc32c(informacion.semilla_checksum, bitmap, bytes);
    escribir_u16(&desc[0x1A], (uint16_t)(c & 0xFFFF));
    if (informacion.tamano_descriptor_grupo >= 64)
        escribir_u16(&desc[0x3A], (uint16_t)(c >> 16));
}

static void desc_sumar_libres(uint8_t *desc, int32_t delta_bloques, int32_t delta_inodos, int32_t delta_dirs)
{
    uint32_t v;

    v = leer_u16(&desc[0x0C]);
    if (informacion.tamano_descriptor_grupo >= 64)
        v |= ((uint32_t)leer_u16(&desc[0x2C]) << 16);
    if (delta_bloques < 0) {
        uint32_t r = (uint32_t)(-delta_bloques);
        v = (v > r) ? (v - r) : 0;
    } else {
        v += (uint32_t)delta_bloques;
    }
    escribir_u16(&desc[0x0C], (uint16_t)(v & 0xFFFF));
    if (informacion.tamano_descriptor_grupo >= 64)
        escribir_u16(&desc[0x2C], (uint16_t)(v >> 16));

    v = leer_u16(&desc[0x0E]);
    if (informacion.tamano_descriptor_grupo >= 64)
        v |= ((uint32_t)leer_u16(&desc[0x2E]) << 16);
    if (delta_inodos < 0) {
        uint32_t r = (uint32_t)(-delta_inodos);
        v = (v > r) ? (v - r) : 0;
    } else {
        v += (uint32_t)delta_inodos;
    }
    escribir_u16(&desc[0x0E], (uint16_t)(v & 0xFFFF));
    if (informacion.tamano_descriptor_grupo >= 64)
        escribir_u16(&desc[0x2E], (uint16_t)(v >> 16));

    v = leer_u16(&desc[0x10]);
    if (informacion.tamano_descriptor_grupo >= 64)
        v |= ((uint32_t)leer_u16(&desc[0x30]) << 16);
    if (delta_dirs < 0) {
        uint32_t r = (uint32_t)(-delta_dirs);
        v = (v > r) ? (v - r) : 0;
    } else {
        v += (uint32_t)delta_dirs;
    }
    escribir_u16(&desc[0x10], (uint16_t)(v & 0xFFFF));
    if (informacion.tamano_descriptor_grupo >= 64)
        escribir_u16(&desc[0x30], (uint16_t)(v >> 16));
}

/* ------------------------------------------------------------------ */
/* Inodos                                                             */
/* ------------------------------------------------------------------ */

static int inodo_ubicacion(uint32_t numero, uint32_t *grupo, uint32_t *indice)
{
    if (numero == 0 || numero > informacion.total_inodos)
        return -1;
    uint32_t i = numero - 1;
    *grupo = i / informacion.inodos_por_grupo;
    *indice = i % informacion.inodos_por_grupo;
    return 0;
}

static int inodo_leer_raw(uint32_t numero, uint8_t *raw)
{
    uint32_t grupo, indice;
    uint8_t desc[64];

    if (!ext4_disponible || !raw)
        return -1;
    if (inodo_ubicacion(numero, &grupo, &indice) != 0)
        return -2;
    if (desc_leer(grupo, desc) != 0)
        return -3;

    uint64_t tabla = desc_obtener_u64(desc, 0x08, 0x28);
    uint64_t off = tabla * (uint64_t)informacion.tamano_bloque +
                   (uint64_t)indice * (uint64_t)informacion.tamano_inodo;

    llenar_ceros(raw, 256);
    return leer_absoluto(off, raw, informacion.tamano_inodo);
}

static uint32_t inodo_checksum(uint32_t numero, const uint8_t *raw)
{
    uint8_t copia[256];
    uint8_t numero_le[8];
    uint8_t ceros[2] = { 0, 0 };
    uint32_t total = informacion.tamano_inodo;

    copiar_bytes(copia, raw, total);
    escribir_u16(&copia[0x7C], 0);
    escribir_u16(&copia[0x82], 0);

    escribir_u64(numero_le, numero);
    uint32_t c = crc32c(informacion.semilla_checksum, numero_le, 8);
    c = crc32c(c, copia, 0x7C);
    c = crc32c(c, ceros, 2);
    c = crc32c(c, copia + 0x7E, 0x80 - 0x7E);

    if (total > EXT4_GOOD_OLD_INODE_SIZE) {
        uint16_t extra = leer_u16(&copia[0x80]);
        c = crc32c(c, copia + 0x80, 0x82 - 0x80);
        if (extra >= 4) {
            c = crc32c(c, ceros, 2);
            c = crc32c(c, copia + 0x84, total - 0x84);
        } else {
            c = crc32c(c, copia + 0x82, total - 0x82);
        }
    }
    return c;
}

static void inodo_sellar(uint32_t numero, uint8_t *raw)
{
    if (!ext4_metadata_csum)
        return;
    uint32_t c = inodo_checksum(numero, raw);
    escribir_u16(&raw[0x7C], (uint16_t)(c & 0xFFFF));
    if (informacion.tamano_inodo > EXT4_GOOD_OLD_INODE_SIZE) {
        uint16_t extra = leer_u16(&raw[0x80]);
        if (extra >= 4)
            escribir_u16(&raw[0x82], (uint16_t)(c >> 16));
        else
            escribir_u16(&raw[0x82], 0);
    }
}

static int inodo_escribir_raw(uint32_t numero, uint8_t *raw)
{
    uint32_t grupo, indice;
    uint8_t desc[64];

    if (!ext4_disponible || !ext4_escritura || !raw)
        return -1;
    if (inodo_ubicacion(numero, &grupo, &indice) != 0)
        return -2;
    if (desc_leer(grupo, desc) != 0)
        return -3;

    uint64_t tabla = desc_obtener_u64(desc, 0x08, 0x28);
    uint64_t off = tabla * (uint64_t)informacion.tamano_bloque +
                   (uint64_t)indice * (uint64_t)informacion.tamano_inodo;

    inodo_sellar(numero, raw);
    return escribir_absoluto(off, raw, informacion.tamano_inodo);
}

static uint64_t inodo_tamano(const uint8_t *raw)
{
    uint64_t bajo = (uint64_t)leer_u32(raw + 0x04);
    uint64_t alto = 0;
    if (informacion.tamano_inodo > EXT4_GOOD_OLD_INODE_SIZE)
        alto = (uint64_t)leer_u32(raw + 0x6C);
    return bajo | (alto << 32);
}

static void inodo_poner_tamano(uint8_t *raw, uint64_t tamano)
{
    escribir_u32(raw + 0x04, (uint32_t)(tamano & 0xFFFFFFFFu));
    if (informacion.tamano_inodo > EXT4_GOOD_OLD_INODE_SIZE)
        escribir_u32(raw + 0x6C, (uint32_t)(tamano >> 32));
}

static int inodo_mapa(const uint8_t *raw, uint32_t logico, uint32_t *fisico);

static int mapa_extents(const uint8_t *iblock, uint32_t logico, uint32_t *fisico)
{
    const uint8_t *cabecera = iblock;
    int niveles = 0;

    if (leer_u16(cabecera) != EXT4_EXT_MAGIC)
        return -1;

    uint16_t entradas = leer_u16(cabecera + 2);
    uint8_t profundidad = cabecera[6];

    while (profundidad > 0 && niveles < 8) {
        const uint8_t *indice = cabecera + 12;
        uint32_t hoja = 0;
        int encontrado = 0;

        for (uint16_t i = 0; i < entradas; i++) {
            const uint8_t *e = indice + (uint32_t)i * 12;
            uint32_t bloque_logico = leer_u32(e);
            if (bloque_logico <= logico) {
                uint32_t alto = (uint32_t)leer_u16(e + 8);
                if (alto != 0)
                    return -1;
                hoja = leer_u32(e + 4);
                encontrado = 1;
            } else {
                break;
            }
        }
        if (!encontrado)
            return 1; /* hueco */

        if (leer_bloque(hoja, buffer_b) != 0)
            return -1;
        cabecera = buffer_b;
        if (leer_u16(cabecera) != EXT4_EXT_MAGIC)
            return -1;
        entradas = leer_u16(cabecera + 2);
        profundidad = cabecera[6];
        niveles++;
    }

    if (profundidad != 0)
        return -1;

    const uint8_t *ext = cabecera + 12;
    for (uint16_t i = 0; i < entradas; i++) {
        const uint8_t *e = ext + (uint32_t)i * 12;
        uint32_t bloque_logico = leer_u32(e);
        uint16_t bruto = leer_u16(e + 4);
        uint32_t largo = (bruto <= 32768) ? bruto : (uint32_t)(bruto - 32768);
        uint32_t alto = (uint32_t)leer_u16(e + 6);
        uint32_t inicio = leer_u32(e + 8);

        if (largo == 0)
            continue;
        if (logico >= bloque_logico && logico < bloque_logico + largo) {
            if (alto != 0)
                return -1;
            *fisico = inicio + (logico - bloque_logico);
            return (bruto > 32768) ? 1 : 0;
        }
    }
    return 1; /* hueco */
}

static int mapa_clasico(const uint8_t *raw, uint32_t logico, uint32_t *fisico)
{
    uint32_t por_bloque = informacion.tamano_bloque / 4;
    const uint8_t *ptr = raw + 0x28;

    if (logico < 12) {
        uint32_t b = leer_u32(ptr + logico * 4);
        if (b == 0)
            return 1;
        *fisico = b;
        return 0;
    }

    logico -= 12;
    if (logico < por_bloque) {
        uint32_t ind = leer_u32(ptr + 12 * 4);
        if (ind == 0)
            return 1;
        if (leer_bloque(ind, buffer_b) != 0)
            return -1;
        uint32_t b = leer_u32(buffer_b + logico * 4);
        if (b == 0)
            return 1;
        *fisico = b;
        return 0;
    }

    logico -= por_bloque;
    if (logico < por_bloque * por_bloque) {
        uint32_t dind = leer_u32(ptr + 13 * 4);
        if (dind == 0)
            return 1;
        if (leer_bloque(dind, buffer_b) != 0)
            return -1;
        uint32_t idx1 = logico / por_bloque;
        uint32_t idx2 = logico % por_bloque;
        uint32_t ind = leer_u32(buffer_b + idx1 * 4);
        if (ind == 0)
            return 1;
        if (leer_bloque(ind, buffer_b) != 0)
            return -1;
        uint32_t b = leer_u32(buffer_b + idx2 * 4);
        if (b == 0)
            return 1;
        *fisico = b;
        return 0;
    }

    logico -= por_bloque * por_bloque;
    if (logico < por_bloque * por_bloque * por_bloque) {
        uint32_t tind = leer_u32(ptr + 14 * 4);
        if (tind == 0)
            return 1;
        if (leer_bloque(tind, buffer_b) != 0)
            return -1;
        uint32_t idx1 = logico / (por_bloque * por_bloque);
        uint32_t resto = logico % (por_bloque * por_bloque);
        uint32_t idx2 = resto / por_bloque;
        uint32_t idx3 = resto % por_bloque;
        uint32_t dind = leer_u32(buffer_b + idx1 * 4);
        if (dind == 0)
            return 1;
        if (leer_bloque(dind, buffer_b) != 0)
            return -1;
        uint32_t ind = leer_u32(buffer_b + idx2 * 4);
        if (ind == 0)
            return 1;
        if (leer_bloque(ind, buffer_b) != 0)
            return -1;
        uint32_t b = leer_u32(buffer_b + idx3 * 4);
        if (b == 0)
            return 1;
        *fisico = b;
        return 0;
    }

    return 1;
}

static int inodo_mapa(const uint8_t *raw, uint32_t logico, uint32_t *fisico)
{
    uint32_t flags = leer_u32(raw + 0x20);
    if (flags & EXT4_EXTENTS_FL)
        return mapa_extents(raw + 0x28, logico, fisico);
    return mapa_clasico(raw, logico, fisico);
}

static void inodo_iniciar_extents(uint8_t *raw)
{
    escribir_u16(raw + 0x28, EXT4_EXT_MAGIC);
    escribir_u16(raw + 0x2A, 0);
    escribir_u16(raw + 0x2C, EXT4_EXT_MAX_ENTRIES);
    escribir_u16(raw + 0x2E, 0);
    escribir_u32(raw + 0x30, 0);
}

static int inodo_agregar_extento(uint8_t *raw, uint32_t logico, uint32_t fisico, uint32_t largo)
{
    uint32_t flags = leer_u32(raw + 0x20);
    uint8_t *iblock = raw + 0x28;

    if (!(flags & EXT4_EXTENTS_FL)) {
        if (logico >= 12 || largo != 1)
            return -1;
        escribir_u32(iblock + logico * 4, fisico);
        return 0;
    }

    if (leer_u16(iblock) != EXT4_EXT_MAGIC)
        return -1;
    uint16_t entradas = leer_u16(iblock + 2);
    uint16_t maximo = leer_u16(iblock + 4);
    uint8_t profundidad = iblock[6];

    if (profundidad != 0)
        return -1;

    if (entradas > 0) {
        uint8_t *ultima = iblock + 12 + (uint32_t)(entradas - 1) * 12;
        uint32_t bloque_logico = leer_u32(ultima);
        uint16_t bruto = leer_u16(ultima + 4);
        uint32_t largo_actual = (bruto <= 32768) ? bruto : (uint32_t)(bruto - 32768);
        uint32_t inicio = leer_u32(ultima + 8);
        uint32_t alto = (uint32_t)leer_u16(ultima + 6);

        if (alto == 0 &&
            bloque_logico + largo_actual == logico &&
            inicio + largo_actual == fisico &&
            largo_actual + largo <= 32768) {
            escribir_u16(ultima + 4, (uint16_t)(largo_actual + largo));
            return 0;
        }
    }

    if (entradas >= maximo || entradas >= EXT4_EXT_MAX_ENTRIES)
        return -1;

    uint8_t *nueva = iblock + 12 + (uint32_t)entradas * 12;
    escribir_u32(nueva, logico);
    escribir_u16(nueva + 4, (uint16_t)largo);
    escribir_u16(nueva + 6, (uint16_t)(fisico >> 16));
    escribir_u32(nueva + 8, fisico);
    escribir_u16(iblock + 2, (uint16_t)(entradas + 1));
    return 0;
}

/* ------------------------------------------------------------------ */
/* Asignacion y liberacion de recursos                                */
/* ------------------------------------------------------------------ */

static int asignar_inodo(int es_directorio, uint32_t *salida)
{
    if (!ext4_escritura || !salida)
        return -1;

    for (uint32_t g = 0; g < informacion.total_grupos; g++) {
        uint8_t desc[64];
        if (desc_leer(g, desc) != 0)
            continue;
        if (leer_u16(&desc[0x12]) & EXT4_BG_INODE_UNINIT)
            continue;

        uint64_t bitmap_bloque = desc_obtener_u64(desc, 0x04, 0x24);
        if (leer_bloque(bitmap_bloque, buffer_bitmap) != 0)
            continue;

        uint32_t inicio = 0;
        if (g == 0 && informacion.primer_inodo > 1)
            inicio = informacion.primer_inodo - 1;

        for (uint32_t i = inicio; i < informacion.inodos_por_grupo; i++) {
            uint32_t numero = g * informacion.inodos_por_grupo + i + 1;
            if (numero > informacion.total_inodos)
                break;
            uint32_t byte = i / 8;
            uint8_t mascara = (uint8_t)(1u << (i % 8));
            if (buffer_bitmap[byte] & mascara)
                continue;

            buffer_bitmap[byte] |= mascara;

            uint32_t ultimo = 0;
            int hay = 0;
            for (uint32_t k = informacion.inodos_por_grupo; k > 0; k--) {
                uint32_t idx = k - 1;
                if (buffer_bitmap[idx / 8] & (uint8_t)(1u << (idx % 8))) {
                    ultimo = idx;
                    hay = 1;
                    break;
                }
            }
            uint32_t no_usados = hay ? (informacion.inodos_por_grupo - 1 - ultimo)
                                     : informacion.inodos_por_grupo;
            escribir_u16(&desc[0x1C], (uint16_t)(no_usados & 0xFFFF));
            if (informacion.tamano_descriptor_grupo >= 64)
                escribir_u16(&desc[0x32], (uint16_t)(no_usados >> 16));

            if (escribir_bloque(bitmap_bloque, buffer_bitmap) != 0)
                return -1;

            desc_actualizar_csum_bitmap_inodos(desc, buffer_bitmap);
            desc_sumar_libres(desc, 0, -1, es_directorio ? 1 : 0);
            if (desc_escribir(g, desc) != 0)
                return -1;

            superbloque_sumar_inodos(-1);
            if (superbloque_guardar() != 0)
                return -1;

            uint8_t raw[256];
            llenar_ceros(raw, 256);
            if (informacion.tamano_inodo > EXT4_GOOD_OLD_INODE_SIZE) {
                uint16_t extra = leer_u16(&superbloque[0x15E]);
                if (extra < 32)
                    extra = 32;
                uint16_t maximo = (uint16_t)(informacion.tamano_inodo - EXT4_GOOD_OLD_INODE_SIZE);
                if (extra > maximo)
                    extra = maximo;
                escribir_u16(&raw[0x80], extra);
            }
            if (inodo_escribir_raw(numero, raw) != 0)
                return -1;

            *salida = numero;
            return 0;
        }
    }
    return -1;
}

static int asignar_bloques(uint32_t cantidad, uint32_t *salida)
{
    if (!ext4_escritura || !salida || cantidad == 0)
        return -1;

    for (uint32_t g = 0; g < informacion.total_grupos; g++) {
        uint8_t desc[64];
        if (desc_leer(g, desc) != 0)
            continue;
        if (leer_u16(&desc[0x12]) & EXT4_BG_BLOCK_UNINIT)
            continue;

        uint64_t bitmap_bloque = desc_obtener_u64(desc, 0x00, 0x20);
        if (leer_bloque(bitmap_bloque, buffer_bitmap) != 0)
            continue;

        uint32_t bits = informacion.tamano_bloque * 8;
        uint32_t corrida = 0;
        uint32_t inicio = 0;

        for (uint32_t i = 0; i < bits; i++) {
            uint64_t bloque = (uint64_t)informacion.primer_bloque_datos +
                              (uint64_t)g * informacion.bloques_por_grupo + i;
            if (bloque >= informacion.total_bloques)
                break;

            if (!(buffer_bitmap[i / 8] & (uint8_t)(1u << (i % 8)))) {
                if (corrida == 0)
                    inicio = i;
                corrida++;
                if (corrida == cantidad) {
                    for (uint32_t k = 0; k < cantidad; k++) {
                        uint32_t bit = inicio + k;
                        buffer_bitmap[bit / 8] |= (uint8_t)(1u << (bit % 8));
                    }
                    if (escribir_bloque(bitmap_bloque, buffer_bitmap) != 0)
                        return -1;

                    desc_actualizar_csum_bitmap_bloques(desc, buffer_bitmap);
                    desc_sumar_libres(desc, -(int32_t)cantidad, 0, 0);
                    if (desc_escribir(g, desc) != 0)
                        return -1;

                    superbloque_sumar_bloques(-(int32_t)cantidad);
                    if (superbloque_guardar() != 0)
                        return -1;

                    *salida = informacion.primer_bloque_datos +
                              g * informacion.bloques_por_grupo + inicio;
                    return 0;
                }
            } else {
                corrida = 0;
            }
        }
    }
    return -1;
}

static int liberar_bloques_grupo(uint32_t grupo, uint32_t base, uint32_t desde, uint32_t cantidad)
{
    uint8_t desc[64];
    if (desc_leer(grupo, desc) != 0)
        return -1;
    if (leer_u16(&desc[0x12]) & EXT4_BG_BLOCK_UNINIT)
        return 0;

    uint64_t bitmap_bloque = desc_obtener_u64(desc, 0x00, 0x20);
    if (leer_bloque(bitmap_bloque, buffer_bitmap) != 0)
        return -1;

    for (uint32_t k = 0; k < cantidad; k++) {
        uint64_t bloque = (uint64_t)base + desde + k;
        if (bloque >= informacion.total_bloques)
            continue;
        uint32_t bit = desde + k;
        buffer_bitmap[bit / 8] &= (uint8_t)~(1u << (bit % 8));
    }

    if (escribir_bloque(bitmap_bloque, buffer_bitmap) != 0)
        return -1;

    desc_actualizar_csum_bitmap_bloques(desc, buffer_bitmap);
    desc_sumar_libres(desc, (int32_t)cantidad, 0, 0);
    if (desc_escribir(grupo, desc) != 0)
        return -1;

    superbloque_sumar_bloques((int32_t)cantidad);
    return superbloque_guardar();
}

static int liberar_rango(uint32_t inicio, uint32_t cantidad)
{
    while (cantidad > 0) {
        if (inicio < informacion.primer_bloque_datos)
            return -1;
        uint32_t relativo = inicio - informacion.primer_bloque_datos;
        uint32_t grupo = relativo / informacion.bloques_por_grupo;
        uint32_t base = informacion.primer_bloque_datos +
                        grupo * informacion.bloques_por_grupo;
        uint32_t dentro = inicio - base;
        uint32_t restante_en_grupo = informacion.bloques_por_grupo - dentro;
        uint32_t lote = (cantidad < restante_en_grupo) ? cantidad : restante_en_grupo;

        if (liberar_bloques_grupo(grupo, base, dentro, lote) != 0)
            return -1;

        inicio += lote;
        cantidad -= lote;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Liberacion de bloques de un inodo                                  */
/* ------------------------------------------------------------------ */

static void liberar_indirecto(uint32_t bloque, int nivel)
{
    if (bloque == 0)
        return;
    if (nivel <= 0)
        return;

    uint32_t punteros[1024];
    if (informacion.tamano_bloque > sizeof(punteros))
        return;
    if (leer_bloque(bloque, punteros) != 0)
        return;

    uint32_t cantidad = informacion.tamano_bloque / 4;
    for (uint32_t i = 0; i < cantidad; i++) {
        if (punteros[i] == 0)
            continue;
        if (nivel == 1)
            liberar_rango(punteros[i], 1);
        else
            liberar_indirecto(punteros[i], nivel - 1);
    }
    liberar_rango(bloque, 1);
}

#define EXT4_EXT_MAX_NODOS 340

/* Libera recursivamente un nodo de extents. El nodo raiz puede ser
 * i_block (nivel = profundidad); los nodos internos son bloques. */
static int liberar_extents_nivel(uint8_t *nodo, int nivel)
{
    if (leer_u16(nodo) != EXT4_EXT_MAGIC)
        return -1;
    if ((int)nodo[6] != nivel)
        return -1;

    uint16_t entradas = leer_u16(nodo + 2);
    if (entradas > EXT4_EXT_MAX_NODOS)
        return -1;

    if (nivel == 0) {
        for (uint16_t i = 0; i < entradas; i++) {
            uint8_t *e = nodo + 12 + (uint32_t)i * 12;
            uint16_t bruto = leer_u16(e + 4);
            uint32_t largo = (bruto <= 32768) ? bruto : (uint32_t)(bruto - 32768);
            uint32_t alto = (uint32_t)leer_u16(e + 6);
            uint32_t inicio = leer_u32(e + 8);

            if (largo == 0)
                continue;
            if (alto != 0)
                return -1;
            if (liberar_rango(inicio, largo) != 0)
                return -1;
        }
        return 0;
    }

    uint32_t hijos[EXT4_EXT_MAX_NODOS];
    for (uint16_t i = 0; i < entradas; i++) {
        uint8_t *e = nodo + 12 + (uint32_t)i * 12;
        uint32_t alto = (uint32_t)leer_u16(e + 8);
        if (alto != 0)
            return -1;
        hijos[i] = leer_u32(e + 4);
    }

    for (uint16_t i = 0; i < entradas; i++) {
        if (leer_bloque(hijos[i], buffer_b) != 0)
            return -1;
        if (liberar_extents_nivel(buffer_b, nivel - 1) != 0)
            return -1;
        if (liberar_rango(hijos[i], 1) != 0)
            return -1;
    }
    return 0;
}

static int liberar_bloques_inodo(uint8_t *raw)
{
    uint32_t flags = leer_u32(raw + 0x20);
    uint8_t *iblock = raw + 0x28;

    if (flags & EXT4_EXTENTS_FL) {
        if (leer_u16(iblock) != EXT4_EXT_MAGIC)
            return -1;
        if (liberar_extents_nivel(iblock, (int)iblock[6]) != 0)
            return -2;
        inodo_iniciar_extents(raw);
        return 0;
    }

    for (uint32_t i = 0; i < 12; i++) {
        uint32_t b = leer_u32(iblock + i * 4);
        if (b != 0) {
            if (liberar_rango(b, 1) != 0)
                return -3;
            escribir_u32(iblock + i * 4, 0);
        }
    }
    uint32_t ind = leer_u32(iblock + 12 * 4);
    if (ind != 0) {
        liberar_indirecto(ind, 1);
        escribir_u32(iblock + 12 * 4, 0);
    }
    uint32_t dind = leer_u32(iblock + 13 * 4);
    if (dind != 0) {
        liberar_indirecto(dind, 2);
        escribir_u32(iblock + 13 * 4, 0);
    }
    uint32_t tind = leer_u32(iblock + 14 * 4);
    if (tind != 0) {
        liberar_indirecto(tind, 3);
        escribir_u32(iblock + 14 * 4, 0);
    }
    return 0;
}

static int liberar_inodo(uint32_t numero, int es_directorio)
{
    uint32_t grupo, indice;
    uint8_t desc[64];

    if (inodo_ubicacion(numero, &grupo, &indice) != 0)
        return -1;
    if (desc_leer(grupo, desc) != 0)
        return -2;

    uint64_t bitmap_bloque = desc_obtener_u64(desc, 0x04, 0x24);
    if (leer_bloque(bitmap_bloque, buffer_bitmap) != 0)
        return -3;

    buffer_bitmap[indice / 8] &= (uint8_t)~(1u << (indice % 8));

    uint32_t ultimo = 0;
    int hay = 0;
    for (uint32_t k = informacion.inodos_por_grupo; k > 0; k--) {
        uint32_t idx = k - 1;
        if (buffer_bitmap[idx / 8] & (uint8_t)(1u << (idx % 8))) {
            ultimo = idx;
            hay = 1;
            break;
        }
    }
    uint32_t no_usados = hay ? (informacion.inodos_por_grupo - 1 - ultimo)
                             : informacion.inodos_por_grupo;
    escribir_u16(&desc[0x1C], (uint16_t)(no_usados & 0xFFFF));
    if (informacion.tamano_descriptor_grupo >= 64)
        escribir_u16(&desc[0x32], (uint16_t)(no_usados >> 16));

    if (escribir_bloque(bitmap_bloque, buffer_bitmap) != 0)
        return -4;

    desc_actualizar_csum_bitmap_inodos(desc, buffer_bitmap);
    desc_sumar_libres(desc, 0, 1, es_directorio ? -1 : 0);
    if (desc_escribir(grupo, desc) != 0)
        return -5;

    superbloque_sumar_inodos(1);
    if (superbloque_guardar() != 0)
        return -6;

    uint8_t raw[256];
    llenar_ceros(raw, 256);
    if (informacion.tamano_inodo > EXT4_GOOD_OLD_INODE_SIZE)
        escribir_u16(&raw[0x80], leer_u16(&superbloque[0x15E]));
    return inodo_escribir_raw(numero, raw);
}

/* ------------------------------------------------------------------ */
/* Directorios                                                        */
/* ------------------------------------------------------------------ */

static uint32_t alinear4(uint32_t v)
{
    return (v + 3u) & ~3u;
}

static int nombre_valido(const char *nombre)
{
    uint32_t n = longitud_nombre(nombre);
    if (n == 0 || n > EXT4_NAME_MAX)
        return 0;
    for (uint32_t i = 0; i < n; i++) {
        if (nombre[i] == '/')
            return 0;
    }
    if (n == 1 && nombre[0] == '.')
        return 0;
    if (n == 2 && nombre[0] == '.' && nombre[1] == '.')
        return 0;
    return 1;
}

static int dir_localizar(
    uint32_t directorio,
    const char *nombre,
    ext4_dir_entry_t *salida,
    uint32_t *bloque_fisico,
    uint32_t *offset_entrada,
    uint32_t *offset_anterior)
{
    uint8_t raw[256];
    if (inodo_leer_raw(directorio, raw) != 0)
        return -1;

    uint64_t tamano = inodo_tamano(raw);
    uint32_t bloques = (uint32_t)(((uint32_t)tamano + informacion.tamano_bloque - 1) /
                                  informacion.tamano_bloque);

    for (uint32_t l = 0; l < bloques; l++) {
        uint32_t fisico = 0;
        int estado = inodo_mapa(raw, l, &fisico);
        if (estado <= 0) {
            if (estado < 0)
                continue;
            if (leer_bloque(fisico, buffer_a) != 0)
                continue;

            uint32_t pos = 0;
            uint32_t anterior = 0xFFFFFFFFu;
            while (pos + 8 <= informacion.tamano_bloque) {
                uint32_t inodo = leer_u32(buffer_a + pos);
                uint16_t rec_len = leer_u16(buffer_a + pos + 4);
                uint8_t name_len = buffer_a[pos + 6];
                uint8_t tipo = buffer_a[pos + 7];

                if (rec_len < 8 || (rec_len & 3) != 0 ||
                    pos + rec_len > informacion.tamano_bloque)
                    break;
                if (name_len > (uint8_t)(rec_len - 8))
                    break;

                if (inodo != 0 && name_len == longitud_nombre(nombre)) {
                    int igual = 1;
                    for (uint32_t k = 0; k < name_len; k++) {
                        if ((char)buffer_a[pos + 8 + k] != nombre[k]) {
                            igual = 0;
                            break;
                        }
                    }
                    if (igual && salida) {
                        salida->inodo = inodo;
                        salida->rec_len = rec_len;
                        salida->name_len = name_len;
                        salida->file_type = tipo;
                        for (uint32_t k = 0; k < name_len; k++)
                            salida->nombre[k] = (char)buffer_a[pos + 8 + k];
                        salida->nombre[name_len] = '\0';
                        if (bloque_fisico)
                            *bloque_fisico = fisico;
                        if (offset_entrada)
                            *offset_entrada = pos;
                        if (offset_anterior)
                            *offset_anterior = anterior;
                        return 0;
                    }
                }

                anterior = pos;
                pos += rec_len;
            }
        }
    }
    return -1;
}

int ext4_buscar_en(uint32_t directorio, const char *nombre, ext4_dir_entry_t *salida)
{
    if (!ext4_disponible || !nombre || !salida)
        return -1;
    return dir_localizar(directorio, nombre, salida, 0, 0, 0);
}

int ext4_listar_directorio(uint32_t numero_inodo, ext4_dir_entry_t *entradas, int max_entradas)
{
    uint8_t raw[256];

    if (!ext4_disponible || !entradas || max_entradas <= 0)
        return -1;
    if (inodo_leer_raw(numero_inodo, raw) != 0)
        return -2;
    if ((leer_u16(raw) & EXT4_S_IFMT) != EXT4_S_IFDIR)
        return -3;

    uint64_t tamano = inodo_tamano(raw);
    uint32_t bloques = (uint32_t)(((uint32_t)tamano + informacion.tamano_bloque - 1) /
                                  informacion.tamano_bloque);
    int contador = 0;

    for (uint32_t l = 0; l < bloques && contador < max_entradas; l++) {
        uint32_t fisico = 0;
        int estado = inodo_mapa(raw, l, &fisico);
        if (estado == 0) {
            if (leer_bloque(fisico, buffer_a) != 0)
                continue;
        } else {
            /* Hueco o bloque no inicializado: se trata como ceros. */
            llenar_ceros(buffer_a, informacion.tamano_bloque);
        }

        uint32_t pos = 0;
        while (pos + 8 <= informacion.tamano_bloque && contador < max_entradas) {
            uint32_t inodo = leer_u32(buffer_a + pos);
            uint16_t rec_len = leer_u16(buffer_a + pos + 4);
            uint8_t name_len = buffer_a[pos + 6];
            uint8_t tipo = buffer_a[pos + 7];

            if (rec_len < 8 || (rec_len & 3) != 0 ||
                pos + rec_len > informacion.tamano_bloque)
                break;
            if (name_len > (uint8_t)(rec_len - 8))
                break;

            if (inodo != 0 && name_len > 0) {
                entradas[contador].inodo = inodo;
                entradas[contador].rec_len = rec_len;
                entradas[contador].name_len = name_len;
                if (tipo == 0) {
                    uint8_t inodo_raw[256];
                    if (inodo_leer_raw(inodo, inodo_raw) == 0) {
                        uint16_t modo = leer_u16(inodo_raw);
                        if ((modo & EXT4_S_IFMT) == EXT4_S_IFDIR)
                            tipo = 2;
                        else if ((modo & EXT4_S_IFMT) == EXT4_S_IFREG)
                            tipo = 1;
                    }
                }
                entradas[contador].file_type = tipo;
                for (uint32_t k = 0; k < name_len; k++)
                    entradas[contador].nombre[k] = (char)buffer_a[pos + 8 + k];
                entradas[contador].nombre[name_len] = '\0';
                contador++;
            }
            pos += rec_len;
        }
    }
    return contador;
}

/* Con metadata_csum cada bloque de directorio termina con una cola de 12
 * bytes (ext4_dir_entry_tail). El CRC cubre solo el area de entradas. */
static uint32_t dir_limite_entradas(void)
{
    if (!ext4_metadata_csum)
        return informacion.tamano_bloque;
    return informacion.tamano_bloque - 12;
}

static uint32_t dir_semilla(uint32_t inodo)
{
    uint8_t numero[8];
    escribir_u64(numero, inodo);
    return crc32c(informacion.semilla_checksum, numero, 8);
}

static void dir_poner_cola(uint32_t inodo, uint8_t *bloque)
{
    uint32_t limite = informacion.tamano_bloque - 12;

    escribir_u32(bloque + limite, 0);
    escribir_u16(bloque + limite + 4, 12);
    bloque[limite + 6] = 0;
    bloque[limite + 7] = 0xDE;
    escribir_u32(bloque + limite + 8, 0);
    escribir_u32(bloque + limite + 8,
                 crc32c(dir_semilla(inodo), bloque, limite));
}

static void dir_actualizar_cola(uint32_t inodo, uint8_t *bloque)
{
    if (!ext4_metadata_csum)
        return;

    uint32_t limite = informacion.tamano_bloque - 12;
    escribir_u32(bloque + limite + 8, 0);
    escribir_u32(bloque + limite + 8,
                 crc32c(dir_semilla(inodo), bloque, limite));
}

static int dir_agregar(uint32_t directorio, const char *nombre, uint32_t objetivo, uint8_t tipo)
{
    uint8_t raw[256];
    uint32_t n = longitud_nombre(nombre);
    uint32_t necesidad = 8 + alinear4(n);
    uint32_t bloques;
    uint32_t limite = dir_limite_entradas();

    if (inodo_leer_raw(directorio, raw) != 0)
        return -1;

    uint64_t tamano = inodo_tamano(raw);
    bloques = (uint32_t)(((uint32_t)tamano + informacion.tamano_bloque - 1) /
                         informacion.tamano_bloque);

    for (uint32_t l = 0; l < bloques; l++) {
        uint32_t fisico = 0;
        if (inodo_mapa(raw, l, &fisico) != 0)
            continue;
        if (leer_bloque(fisico, buffer_a) != 0)
            continue;

        uint32_t pos = 0;
        while (pos + 8 <= limite) {
            uint32_t inodo = leer_u32(buffer_a + pos);
            uint16_t rec_len = leer_u16(buffer_a + pos + 4);
            uint8_t name_len = buffer_a[pos + 6];

            if (rec_len < 8 || (rec_len & 3) != 0 ||
                pos + rec_len > limite)
                break;

            if (inodo == 0) {
                if (rec_len >= necesidad && pos + rec_len <= limite) {
                    escribir_u32(buffer_a + pos, objetivo);
                    escribir_u16(buffer_a + pos + 4, rec_len);
                    buffer_a[pos + 6] = (uint8_t)n;
                    buffer_a[pos + 7] = tipo;
                    for (uint32_t k = 0; k < n; k++)
                        buffer_a[pos + 8 + k] = (uint8_t)nombre[k];
                    for (uint32_t k = n; k < (uint32_t)(rec_len - 8); k++)
                        buffer_a[pos + 8 + k] = 0;
                    dir_actualizar_cola(directorio, buffer_a);
                    return escribir_bloque(fisico, buffer_a);
                }
            } else {
                uint32_t usado = 8 + alinear4(name_len);
                if (rec_len >= usado + necesidad &&
                    pos + usado + necesidad <= limite) {
                    escribir_u16(buffer_a + pos + 4, (uint16_t)usado);
                    uint32_t nuevo = pos + usado;
                    uint16_t rec_nueva = (uint16_t)(rec_len - usado);
                    escribir_u32(buffer_a + nuevo, objetivo);
                    escribir_u16(buffer_a + nuevo + 4, rec_nueva);
                    buffer_a[nuevo + 6] = (uint8_t)n;
                    buffer_a[nuevo + 7] = tipo;
                    for (uint32_t k = 0; k < n; k++)
                        buffer_a[nuevo + 8 + k] = (uint8_t)nombre[k];
                    for (uint32_t k = n; k < (uint32_t)(rec_nueva - 8); k++)
                        buffer_a[nuevo + 8 + k] = 0;
                    dir_actualizar_cola(directorio, buffer_a);
                    return escribir_bloque(fisico, buffer_a);
                }
            }
            pos += rec_len;
        }
    }

    /* No hay espacio en los bloques existentes: crear uno nuevo. */
    uint32_t nuevo_bloque = 0;
    if (asignar_bloques(1, &nuevo_bloque) != 0)
        return -2;

    llenar_ceros(buffer_a, informacion.tamano_bloque);
    escribir_u32(buffer_a, objetivo);
    escribir_u16(buffer_a + 4, (uint16_t)limite);
    buffer_a[6] = (uint8_t)n;
    buffer_a[7] = tipo;
    for (uint32_t k = 0; k < n; k++)
        buffer_a[8 + k] = (uint8_t)nombre[k];
    if (ext4_metadata_csum)
        dir_poner_cola(directorio, buffer_a);

    if (escribir_bloque(nuevo_bloque, buffer_a) != 0) {
        liberar_rango(nuevo_bloque, 1);
        return -3;
    }

    if (inodo_agregar_extento(raw, bloques, nuevo_bloque, 1) != 0) {
        liberar_rango(nuevo_bloque, 1);
        return -4;
    }

    uint64_t nuevo_tamano = (uint64_t)(bloques + 1) * informacion.tamano_bloque;
    inodo_poner_tamano(raw, nuevo_tamano);
    escribir_u32(raw + 0x1C, leer_u32(raw + 0x1C) +
                  informacion.tamano_bloque / 512);

    if (inodo_escribir_raw(directorio, raw) != 0) {
        liberar_rango(nuevo_bloque, 1);
        return -5;
    }
    return 0;
}

static int dir_borrar_entrada(uint32_t directorio,
                              uint32_t bloque_fisico,
                              uint32_t offset_entrada,
                              uint32_t offset_anterior)
{
    if (leer_bloque(bloque_fisico, buffer_a) != 0)
        return -1;

    if (offset_anterior != 0xFFFFFFFFu) {
        uint16_t rec_ant = leer_u16(buffer_a + offset_anterior + 4);
        uint16_t rec_actual = leer_u16(buffer_a + offset_entrada + 4);
        escribir_u16(buffer_a + offset_anterior + 4,
                     (uint16_t)(rec_ant + rec_actual));
    } else {
        escribir_u32(buffer_a + offset_entrada, 0);
        escribir_u16(buffer_a + offset_entrada + 4,
                     leer_u16(buffer_a + offset_entrada + 4));
        buffer_a[offset_entrada + 6] = 0;
        buffer_a[offset_entrada + 7] = 0;
    }
    dir_actualizar_cola(directorio, buffer_a);
    return escribir_bloque(bloque_fisico, buffer_a);
}

/* ------------------------------------------------------------------ */
/* Archivos                                                           */
/* ------------------------------------------------------------------ */

static int escribir_datos_bloque(uint32_t fisico, const char *datos, uint32_t cantidad)
{
    llenar_ceros(buffer_a, informacion.tamano_bloque);
    for (uint32_t i = 0; i < cantidad; i++)
        buffer_a[i] = (uint8_t)datos[i];
    return escribir_bloque(fisico, buffer_a);
}

int ext4_leer_archivo(uint32_t numero_inodo, char *buffer, uint32_t capacidad, uint32_t *leido)
{
    uint8_t raw[256];

    if (!ext4_disponible || !buffer || capacidad == 0 || !leido)
        return -1;
    if (inodo_leer_raw(numero_inodo, raw) != 0)
        return -2;
    if ((leer_u16(raw) & EXT4_S_IFMT) != EXT4_S_IFREG)
        return -3;

    uint64_t tamano = inodo_tamano(raw);
    *leido = 0;
    buffer[0] = '\0';
    if (tamano == 0)
        return 0;

    uint64_t limite = tamano;
    if (limite > (uint64_t)(capacidad - 1))
        limite = (uint64_t)(capacidad - 1);

    uint32_t bloques = (uint32_t)(((uint32_t)limite + informacion.tamano_bloque - 1) /
                                  informacion.tamano_bloque);

    for (uint32_t l = 0; l < bloques && *leido < limite; l++) {
        uint32_t fisico = 0;
        int estado = inodo_mapa(raw, l, &fisico);
        if (estado == 0) {
            if (leer_bloque(fisico, buffer_a) != 0)
                return -4;
        } else {
            llenar_ceros(buffer_a, informacion.tamano_bloque);
        }

        uint32_t restante = (uint32_t)(limite - *leido);
        uint32_t copiar = informacion.tamano_bloque;
        if (copiar > restante)
            copiar = restante;

        for (uint32_t i = 0; i < copiar; i++)
            buffer[*leido + i] = (char)buffer_a[i];
        *leido += copiar;
    }

    buffer[*leido] = '\0';
    return 0;
}

static void inodo_vaciar(uint8_t *raw)
{
    uint32_t flags = leer_u32(raw + 0x20);
    inodo_poner_tamano(raw, 0);
    escribir_u32(raw + 0x1C, 0);
    llenar_ceros(raw + 0x28, 60);
    escribir_u32(raw + 0x20, flags);
}

int ext4_escribir_archivo(uint32_t numero_inodo, const char *datos, uint32_t longitud)
{
    uint8_t raw[256];

    if (!ext4_disponible || !ext4_escritura || !datos)
        return -1;
    if (inodo_leer_raw(numero_inodo, raw) != 0)
        return -2;
    if ((leer_u16(raw) & EXT4_S_IFMT) != EXT4_S_IFREG)
        return -3;

    if (liberar_bloques_inodo(raw) != 0)
        return -4;
    inodo_vaciar(raw);
    inodo_iniciar_extents(raw);

    /* Dejar el inodo vacio y consistente antes de tocar datos. */
    if (inodo_escribir_raw(numero_inodo, raw) != 0)
        return -5;

    if (longitud == 0)
        return 0;

    uint32_t total_bloques =
        (longitud + informacion.tamano_bloque - 1) / informacion.tamano_bloque;
    uint32_t logico = 0;

    while (logico < total_bloques) {
        uint32_t restantes = total_bloques - logico;
        uint32_t intento = restantes;
        uint32_t fisico = 0;
        uint32_t obtenidos = 0;

        while (intento > 0) {
            if (asignar_bloques(intento, &fisico) == 0) {
                obtenidos = intento;
                break;
            }
            intento--;
        }
        if (obtenidos == 0) {
            inodo_vaciar(raw);
            inodo_iniciar_extents(raw);
            inodo_escribir_raw(numero_inodo, raw);
            return -6;
        }

        if (inodo_agregar_extento(raw, logico, fisico, obtenidos) != 0) {
            liberar_rango(fisico, obtenidos);
            inodo_vaciar(raw);
            inodo_iniciar_extents(raw);
            inodo_escribir_raw(numero_inodo, raw);
            return -7;
        }

        for (uint32_t k = 0; k < obtenidos; k++) {
            uint32_t bloque_logico = logico + k;
            uint32_t offset = bloque_logico * informacion.tamano_bloque;
            uint32_t copiar = informacion.tamano_bloque;
            if (offset + copiar > longitud)
                copiar = longitud - offset;
            if (escribir_datos_bloque(fisico + k, datos + offset, copiar) != 0) {
                liberar_bloques_inodo(raw);
                inodo_vaciar(raw);
                inodo_iniciar_extents(raw);
                inodo_escribir_raw(numero_inodo, raw);
                return -8;
            }
        }

        logico += obtenidos;
    }

    inodo_poner_tamano(raw, (uint64_t)longitud);
    escribir_u32(raw + 0x1C,
                 total_bloques * (informacion.tamano_bloque / 512));
    return inodo_escribir_raw(numero_inodo, raw);
}

/* ------------------------------------------------------------------ */
/* Creacion y eliminacion                                             */
/* ------------------------------------------------------------------ */

int ext4_inodo_es_directorio(uint32_t numero_inodo)
{
    uint8_t raw[256];
    if (inodo_leer_raw(numero_inodo, raw) != 0)
        return 0;
    return (leer_u16(raw) & EXT4_S_IFMT) == EXT4_S_IFDIR;
}

int ext4_directorio_vacio(uint32_t numero_inodo)
{
    uint8_t raw[256];

    if (inodo_leer_raw(numero_inodo, raw) != 0)
        return -1;
    if ((leer_u16(raw) & EXT4_S_IFMT) != EXT4_S_IFDIR)
        return -1;

    uint64_t tamano = inodo_tamano(raw);
    uint32_t bloques = (uint32_t)(((uint32_t)tamano + informacion.tamano_bloque - 1) /
                                  informacion.tamano_bloque);

    for (uint32_t l = 0; l < bloques; l++) {
        uint32_t fisico = 0;
        int estado = inodo_mapa(raw, l, &fisico);
        if (estado != 0)
            continue;
        if (leer_bloque(fisico, buffer_a) != 0)
            continue;

        uint32_t pos = 0;
        while (pos + 8 <= informacion.tamano_bloque) {
            uint32_t inodo = leer_u32(buffer_a + pos);
            uint16_t rec_len = leer_u16(buffer_a + pos + 4);
            uint8_t name_len = buffer_a[pos + 6];

            if (rec_len < 8 || (rec_len & 3) != 0 ||
                pos + rec_len > informacion.tamano_bloque)
                break;
            if (name_len > (uint8_t)(rec_len - 8))
                break;

            if (inodo != 0 && name_len > 0) {
                int es_punto = (name_len == 1 && buffer_a[pos + 8] == '.');
                int es_padre = (name_len == 2 && buffer_a[pos + 8] == '.' &&
                                buffer_a[pos + 9] == '.');
                if (!es_punto && !es_padre)
                    return 0;
            }
            pos += rec_len;
        }
    }
    return 1;
}

int ext4_crear(uint32_t directorio, const char *nombre, int es_directorio, uint32_t *nuevo_inodo)
{
    uint8_t raw_padre[256];
    uint8_t raw[256];
    uint32_t inodo_nuevo = 0;

    if (!ext4_disponible || !ext4_escritura)
        return -1;
    if (!nombre_valido(nombre))
        return -1;

    ext4_dir_entry_t existente;
    if (ext4_buscar_en(directorio, nombre, &existente) == 0)
        return -2;

    if (inodo_leer_raw(directorio, raw_padre) != 0)
        return -3;
    if ((leer_u16(raw_padre) & EXT4_S_IFMT) != EXT4_S_IFDIR)
        return -3;

    if (asignar_inodo(es_directorio, &inodo_nuevo) != 0)
        return -4;

    llenar_ceros(raw, 256);
    escribir_u16(raw, es_directorio ? (EXT4_S_IFDIR | 0755) : (EXT4_S_IFREG | 0644));
    escribir_u16(raw + 0x1A, es_directorio ? 2 : 1);
    escribir_u32(raw + 0x20, EXT4_EXTENTS_FL);
    if (informacion.tamano_inodo > EXT4_GOOD_OLD_INODE_SIZE)
        escribir_u16(raw + 0x80, leer_u16(&superbloque[0x15E]));
    inodo_iniciar_extents(raw);

    if (es_directorio) {
        uint32_t bloque_dir = 0;
        if (asignar_bloques(1, &bloque_dir) != 0) {
            liberar_inodo(inodo_nuevo, 1);
            return -5;
        }

        llenar_ceros(buffer_a, informacion.tamano_bloque);
        uint32_t limite = dir_limite_entradas();

        escribir_u32(buffer_a, inodo_nuevo);
        escribir_u16(buffer_a + 4, 12);
        buffer_a[6] = 1;
        buffer_a[7] = 2;
        buffer_a[8] = '.';

        escribir_u32(buffer_a + 12, directorio);
        escribir_u16(buffer_a + 16, (uint16_t)(limite - 12));
        buffer_a[18] = 2;
        buffer_a[19] = 2;
        buffer_a[20] = '.';
        buffer_a[21] = '.';

        if (ext4_metadata_csum)
            dir_poner_cola(inodo_nuevo, buffer_a);

        if (escribir_bloque(bloque_dir, buffer_a) != 0) {
            liberar_rango(bloque_dir, 1);
            liberar_inodo(inodo_nuevo, 1);
            return -6;
        }

        if (inodo_agregar_extento(raw, 0, bloque_dir, 1) != 0) {
            liberar_rango(bloque_dir, 1);
            liberar_inodo(inodo_nuevo, 1);
            return -6;
        }
        inodo_poner_tamano(raw, informacion.tamano_bloque);
        escribir_u32(raw + 0x1C, informacion.tamano_bloque / 512);

        uint16_t enlaces = leer_u16(raw_padre + 0x1A);
        escribir_u16(raw_padre + 0x1A, (uint16_t)(enlaces + 1));
        if (inodo_escribir_raw(directorio, raw_padre) != 0) {
            liberar_rango(bloque_dir, 1);
            liberar_inodo(inodo_nuevo, 1);
            return -7;
        }
    }

    if (inodo_escribir_raw(inodo_nuevo, raw) != 0) {
        liberar_inodo(inodo_nuevo, es_directorio);
        return -8;
    }

    if (dir_agregar(directorio, nombre, inodo_nuevo, es_directorio ? 2 : 1) != 0) {
        uint8_t raw_aux[256];
        if (inodo_leer_raw(inodo_nuevo, raw_aux) == 0)
            liberar_bloques_inodo(raw_aux);
        liberar_inodo(inodo_nuevo, es_directorio);
        if (es_directorio) {
            uint16_t enlaces = leer_u16(raw_padre + 0x1A);
            if (enlaces > 0)
                escribir_u16(raw_padre + 0x1A, (uint16_t)(enlaces - 1));
            inodo_escribir_raw(directorio, raw_padre);
        }
        return -9;
    }

    if (nuevo_inodo)
        *nuevo_inodo = inodo_nuevo;
    return 0;
}

int ext4_eliminar(uint32_t directorio, const char *nombre)
{
    ext4_dir_entry_t entrada;
    uint32_t bloque = 0;
    uint32_t offset = 0;
    uint32_t anterior = 0;
    uint8_t raw[256];
    uint8_t raw_padre[256];

    if (!ext4_disponible || !ext4_escritura)
        return -1;
    if (!nombre_valido(nombre))
        return -1;

    if (dir_localizar(directorio, nombre, &entrada, &bloque, &offset, &anterior) != 0)
        return -2;
    if (entrada.inodo == EXT4_ROOT_INO)
        return -3;

    if (inodo_leer_raw(entrada.inodo, raw) != 0)
        return -4;

    int es_dir = (leer_u16(raw) & EXT4_S_IFMT) == EXT4_S_IFDIR;

    if (es_dir) {
        int vacio = ext4_directorio_vacio(entrada.inodo);
        if (vacio != 1)
            return -5;
        if (inodo_leer_raw(directorio, raw_padre) != 0)
            return -6;
        uint16_t enlaces = leer_u16(raw_padre + 0x1A);
        if (enlaces > 0) {
            escribir_u16(raw_padre + 0x1A, (uint16_t)(enlaces - 1));
            if (inodo_escribir_raw(directorio, raw_padre) != 0)
                return -6;
        }
    }

    if (dir_borrar_entrada(directorio, bloque, offset, anterior) != 0)
        return -7;

    if (!es_dir) {
        uint16_t enlaces = leer_u16(raw + 0x1A);
        if (enlaces > 1) {
            escribir_u16(raw + 0x1A, (uint16_t)(enlaces - 1));
            return inodo_escribir_raw(entrada.inodo, raw);
        }
    }

    if (liberar_bloques_inodo(raw) != 0)
        return -8;

    return liberar_inodo(entrada.inodo, es_dir);
}

int ext4_eliminar_recursivo(uint32_t directorio, const char *nombre)
{
    ext4_dir_entry_t entrada;

    if (ext4_buscar_en(directorio, nombre, &entrada) != 0)
        return -1;

    if (ext4_inodo_es_directorio(entrada.inodo)) {
        for (;;) {
            ext4_dir_entry_t hijos[8];
            int cantidad = ext4_listar_directorio(entrada.inodo, hijos, 8);
            int siguiente = -1;
            if (cantidad < 0)
                return -1;

            for (int i = 0; i < cantidad; i++) {
                if (hijos[i].name_len == 1 && hijos[i].nombre[0] == '.')
                    continue;
                if (hijos[i].name_len == 2 && hijos[i].nombre[0] == '.' &&
                    hijos[i].nombre[1] == '.')
                    continue;
                siguiente = i;
                break;
            }
            if (siguiente < 0)
                break;

            char nombre_hijo[EXT4_NAME_MAX + 1];
            uint32_t n = hijos[siguiente].name_len;
            for (uint32_t k = 0; k < n; k++)
                nombre_hijo[k] = hijos[siguiente].nombre[k];
            nombre_hijo[n] = '\0';

            if (ext4_eliminar_recursivo(entrada.inodo, nombre_hijo) != 0)
                return -1;
        }
    }

    return ext4_eliminar(directorio, nombre);
}

/* ------------------------------------------------------------------ */
/* API de compatibilidad                                              */
/* ------------------------------------------------------------------ */

int ext4_crear_archivo(const char *nombre)
{
    return ext4_crear(EXT4_ROOT_INO, nombre, 0, 0);
}

int ext4_crear_directorio(const char *nombre)
{
    return ext4_crear(EXT4_ROOT_INO, nombre, 1, 0);
}

int ext4_eliminar_archivo(const char *nombre)
{
    return ext4_eliminar(EXT4_ROOT_INO, nombre);
}

int ext4_eliminar_directorio(const char *nombre)
{
    return ext4_eliminar(EXT4_ROOT_INO, nombre);
}

int ext4_buscar_nombre(const char *nombre, uint32_t *inodo_out)
{
    ext4_dir_entry_t entrada;
    if (!inodo_out)
        return -1;
    if (ext4_buscar_en(EXT4_ROOT_INO, nombre, &entrada) != 0)
        return -2;
    *inodo_out = entrada.inodo;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Montaje y lectura de inodos publica                                 */
/* ------------------------------------------------------------------ */

int ext4_montar(ext4_read_bytes_fn leer, uint64_t inicio_particion)
{
    if (!leer)
        return -1;

    ext4_disponible = 0;
    ext4_escritura = 0;
    ext4_escribir_disco = 0;
    ext4_leer_disco = leer;
    ext4_inicio_particion = inicio_particion;

    if (leer(inicio_particion + EXT4_SUPER_OFFSET, superbloque, EXT4_SUPER_SIZE) != 0)
        return -2;
    if (leer_u16(&superbloque[0x38]) != EXT4_MAGIC)
        return -3;

    uint32_t log_bloque = leer_u32(&superbloque[0x18]);
    if (log_bloque > 6)
        return -4;

    informacion.total_inodos = leer_u32(&superbloque[0x00]);
    informacion.bloques_bajos = leer_u32(&superbloque[0x04]);
    informacion.primer_bloque_datos = leer_u32(&superbloque[0x14]);
    informacion.bloques_por_grupo = leer_u32(&superbloque[0x20]);
    informacion.inodos_por_grupo = leer_u32(&superbloque[0x28]);
    informacion.tamano_bloque = 1024u << log_bloque;
    informacion.tamano_inodo = leer_u16(&superbloque[0x58]);
    informacion.caracteristicas_compat = leer_u32(&superbloque[0x5C]);
    informacion.caracteristicas_incompatibles = leer_u32(&superbloque[0x60]);
    informacion.caracteristicas_solo_lectura = leer_u32(&superbloque[0x64]);
    informacion.primer_inodo = leer_u32(&superbloque[0x54]);
    informacion.bloques_libres = leer_u32(&superbloque[0x0C]);
    informacion.inodos_libres = leer_u32(&superbloque[0x10]);

    informacion.total_bloques = (uint64_t)informacion.bloques_bajos;
    if (informacion.caracteristicas_incompatibles & EXT4_FEATURE_INCOMPAT_64BIT)
        informacion.total_bloques |=
            ((uint64_t)leer_u32(&superbloque[0x150]) << 32);

    if (informacion.caracteristicas_incompatibles & EXT4_FEATURE_INCOMPAT_64BIT) {
        informacion.tamano_descriptor_grupo = leer_u16(&superbloque[0xFE]);
        if (informacion.tamano_descriptor_grupo < 32 ||
            informacion.tamano_descriptor_grupo > 64)
            informacion.tamano_descriptor_grupo = 64;
    } else {
        informacion.tamano_descriptor_grupo = 32;
    }

    if (informacion.tamano_bloque < 1024 ||
        informacion.tamano_bloque > EXT4_BLOCK_MAX ||
        (informacion.tamano_bloque & (informacion.tamano_bloque - 1)) != 0)
        return -5;
    if (informacion.tamano_inodo < EXT4_GOOD_OLD_INODE_SIZE ||
        informacion.tamano_inodo > 256)
        return -5;
    if (informacion.inodos_por_grupo == 0 ||
        informacion.bloques_por_grupo == 0)
        return -5;
    if (informacion.total_bloques > 0xFFFFFFFFu)
        return -5;
    if (informacion.caracteristicas_incompatibles & EXT4_FEATURE_INCOMPAT_META_BG)
        return -6;
    if (informacion.caracteristicas_incompatibles & EXT4_FEATURE_INCOMPAT_INLINE_DATA)
        return -6;
    if (!(informacion.caracteristicas_incompatibles & EXT4_FEATURE_INCOMPAT_FILETYPE))
        return -6;
    if (!(informacion.caracteristicas_incompatibles & EXT4_FEATURE_INCOMPAT_EXTENTS))
        return -6;

    for (int i = 0; i < 16; i++)
        informacion.uuid[i] = superbloque[0x68 + i];

    ext4_metadata_csum =
        (informacion.caracteristicas_solo_lectura &
         EXT4_FEATURE_RO_COMPAT_METADATA_CSUM) ? 1 : 0;

    if (ext4_metadata_csum) {
        if (informacion.caracteristicas_incompatibles &
            EXT4_FEATURE_INCOMPAT_CSUM_SEED) {
            informacion.semilla_checksum = leer_u32(&superbloque[0x270]);
        } else {
            informacion.semilla_checksum =
                crc32c(0xFFFFFFFFu, informacion.uuid, 16);
        }
    } else {
        informacion.semilla_checksum = 0;
    }

    if (informacion.total_bloques <= informacion.primer_bloque_datos)
        return -7;

    uint32_t grupos = ((uint32_t)informacion.total_bloques -
                       informacion.primer_bloque_datos +
                       informacion.bloques_por_grupo - 1) /
                      informacion.bloques_por_grupo;
    if (grupos == 0)
        return -7;
    informacion.total_grupos = grupos;

    informacion.offset_tabla_descriptores =
        (uint64_t)(informacion.primer_bloque_datos + 1) *
        (uint64_t)informacion.tamano_bloque;

    ext4_disponible = 1;
    ext4_escritura = 0;
    return 0;
}

void ext4_set_escribir(ext4_write_bytes_fn escribir)
{
    ext4_escribir_disco = escribir;
    ext4_escritura = (escribir != 0);
}

const ext4_info_t *ext4_obtener_info(void)
{
    if (!ext4_disponible)
        return 0;
    return &informacion;
}

int ext4_leer_inodo(uint32_t numero_inodo, ext4_inodo_t *inodo)
{
    uint8_t raw[256];

    if (!ext4_disponible || !inodo)
        return -1;
    if (inodo_leer_raw(numero_inodo, raw) != 0)
        return -2;

    inodo->modo = leer_u16(raw);
    inodo->enlaces = leer_u16(raw + 0x1A);
    inodo->tamano = leer_u32(raw + 0x04);
    inodo->tamano_alto = 0;
    if (informacion.tamano_inodo > EXT4_GOOD_OLD_INODE_SIZE)
        inodo->tamano_alto = leer_u32(raw + 0x6C);
    inodo->flags = leer_u32(raw + 0x20);
    inodo->sectores = leer_u32(raw + 0x1C);
    for (int i = 0; i < 60; i++)
        inodo->i_block[i] = raw[0x28 + i];

    return 0;
}
