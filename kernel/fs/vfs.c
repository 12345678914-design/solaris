#include "vfs.h"
#include "ext4.h"
#include "../driver/IDE/ide.h"

#include <stdint.h>
#include <stddef.h>

#define VFS_MAX_ENTRADAS 128

static char cwd[VFS_PATH_MAX] = "/";
static uint32_t cwd_inodo = 2;
static int vfs_montado = 0;

/* Buffer de trabajo estatico: evita arreglos grandes en la pila. */
static ext4_dir_entry_t entradas_ext4[VFS_MAX_ENTRADAS];

/* ------------------------------------------------------------------ */
/* Utilidades de cadenas                                              */
/* ------------------------------------------------------------------ */

static int comparar_cadena(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return 0;

    while (*a && *b) {
        if (*a != *b)
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void copiar_cadena(char *dest, const char *orig, size_t max)
{
    if (dest == NULL || orig == NULL || max == 0)
        return;

    size_t i = 0;
    while (i < max - 1 && orig[i] != '\0') {
        dest[i] = orig[i];
        i++;
    }
    dest[i] = '\0';
}

static size_t longitud_cadena(const char *s)
{
    size_t n = 0;
    if (s == NULL)
        return 0;
    while (s[n] != '\0')
        n++;
    return n;
}

static void ruta_subir_directorio(char *ruta)
{
    size_t n = longitud_cadena(ruta);

    if (n <= 1) {
        ruta[0] = '/';
        ruta[1] = '\0';
        return;
    }

    while (n > 1 && ruta[n - 1] != '/')
        n--;
    if (n > 1)
        n--;
    ruta[n] = '\0';

    if (ruta[0] == '\0') {
        ruta[0] = '/';
        ruta[1] = '\0';
    }
}

static int ruta_agregar_componente(char *ruta, const char *componente, size_t capacidad)
{
    size_t actual = longitud_cadena(ruta);
    size_t nombre = longitud_cadena(componente);
    int barra = (actual > 1 && ruta[actual - 1] != '/');

    if (actual + (size_t)barra + nombre >= capacidad)
        return -1;

    if (barra)
        ruta[actual++] = '/';

    for (size_t i = 0; i < nombre; i++)
        ruta[actual++] = componente[i];

    ruta[actual] = '\0';
    return 0;
}

static int nombre_simple_valido(const char *nombre)
{
    size_t n = longitud_cadena(nombre);

    if (n == 0 || n > EXT4_NAME_MAX)
        return 0;
    if (n == 1 && nombre[0] == '.')
        return 0;
    if (n == 2 && nombre[0] == '.' && nombre[1] == '.')
        return 0;
    for (size_t i = 0; i < n; i++) {
        if (nombre[i] == '/')
            return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Resolucion de rutas                                                */
/* ------------------------------------------------------------------ */

/*
 * Resuelve una ruta de directorio componente a componente.
 * Admite rutas absolutas, relativas, ".", ".." y barras repetidas.
 */
static int resolver_directorio(const char *ruta, uint32_t *resultado, char *ruta_resultado)
{
    if (!vfs_montado || ruta == NULL || resultado == NULL)
        return -1;

    uint32_t actual;
    char normalizada[VFS_PATH_MAX];

    if (ruta[0] == '/') {
        actual = 2;
        copiar_cadena(normalizada, "/", sizeof(normalizada));
    } else {
        actual = cwd_inodo;
        copiar_cadena(normalizada, cwd, sizeof(normalizada));
    }

    size_t i = 0;
    while (ruta[i] != '\0') {
        while (ruta[i] == '/')
            i++;
        if (ruta[i] == '\0')
            break;

        char componente[VFS_NAME_MAX];
        size_t j = 0;
        while (ruta[i] != '\0' && ruta[i] != '/') {
            if (j >= sizeof(componente) - 1)
                return -1;
            componente[j++] = ruta[i++];
        }
        componente[j] = '\0';

        if (comparar_cadena(componente, "."))
            continue;

        if (comparar_cadena(componente, "..")) {
            if (actual != 2) {
                ext4_dir_entry_t padre;
                if (ext4_buscar_en(actual, "..", &padre) != 0)
                    return -1;
                actual = padre.inodo;
            }
            ruta_subir_directorio(normalizada);
            continue;
        }

        ext4_dir_entry_t entrada;
        if (ext4_buscar_en(actual, componente, &entrada) != 0)
            return -1;
        if (!ext4_inodo_es_directorio(entrada.inodo))
            return -1;

        actual = entrada.inodo;
        if (ruta_agregar_componente(normalizada, componente, sizeof(normalizada)) != 0)
            return -1;
    }

    *resultado = actual;
    if (ruta_resultado != NULL)
        copiar_cadena(ruta_resultado, normalizada, VFS_PATH_MAX);
    return 0;
}

/*
 * Separa una ruta en directorio padre + nombre final.
 * "docs/nota.txt" -> padre "docs", nombre "nota.txt"
 * "/a/b"          -> padre "/a",  nombre "b"
 */
static int resolver_padre(const char *ruta, uint32_t *padre, char *nombre)
{
    if (!vfs_montado || ruta == NULL || padre == NULL || nombre == NULL)
        return -1;

    size_t n = longitud_cadena(ruta);
    if (n == 0 || n >= VFS_PATH_MAX)
        return -1;
    if (ruta[n - 1] == '/')
        return -1;

    int corte = -1;
    for (size_t i = 0; i < n; i++) {
        if (ruta[i] == '/')
            corte = (int)i;
    }

    if (corte < 0) {
        copiar_cadena(nombre, ruta, VFS_NAME_MAX);
        *padre = cwd_inodo;
    } else {
        const char *inicio_nombre = ruta + corte + 1;
        copiar_cadena(nombre, inicio_nombre, VFS_NAME_MAX);

        if ((size_t)corte == 0) {
            *padre = 2;
        } else {
            char base[VFS_PATH_MAX];
            if ((size_t)corte >= sizeof(base))
                return -1;
            for (int i = 0; i < corte; i++)
                base[i] = ruta[i];
            base[corte] = '\0';
            if (resolver_directorio(base, padre, NULL) != 0)
                return -1;
        }
    }

    if (!nombre_simple_valido(nombre))
        return -1;
    return 0;
}

static int es_ancestro(uint32_t posible_ancestro, uint32_t inodo)
{
    if (posible_ancestro == inodo)
        return 1;

    uint32_t actual = inodo;
    for (int nivel = 0; nivel < 512; nivel++) {
        if (actual == 2)
            break;
        ext4_dir_entry_t padre;
        if (ext4_buscar_en(actual, "..", &padre) != 0)
            break;
        if (padre.inodo == actual)
            break;
        actual = padre.inodo;
        if (actual == posible_ancestro)
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Inicializacion y montaje                                           */
/* ------------------------------------------------------------------ */

void vfs_init(void)
{
    cwd[0] = '/';
    cwd[1] = '\0';
    cwd_inodo = 2;
    vfs_montado = 0;
}

int vfs_montar_ext4(void)
{
    vfs_montado = 0;

    int resultado = ext4_montar(ide_leer_bytes, 0);
    if (resultado != 0)
        return resultado;

    ext4_set_escribir(ide_escribir_bytes);

    cwd[0] = '/';
    cwd[1] = '\0';
    cwd_inodo = 2;
    vfs_montado = 1;
    return 0;
}

const char *vfs_get_cwd(void)
{
    return cwd;
}

/* ------------------------------------------------------------------ */
/* Navegacion                                                         */
/* ------------------------------------------------------------------ */

int vfs_cd(const char *ruta)
{
    if (!vfs_montado || ruta == NULL || ruta[0] == '\0')
        return -1;

    uint32_t nuevo;
    char nueva_ruta[VFS_PATH_MAX];

    if (resolver_directorio(ruta, &nuevo, nueva_ruta) != 0)
        return -1;

    cwd_inodo = nuevo;
    copiar_cadena(cwd, nueva_ruta, sizeof(cwd));
    return 0;
}

int vfs_ls(const char *ruta, vfs_entrada_t *entradas, int max)
{
    if (!vfs_montado || entradas == NULL || max <= 0)
        return -1;

    uint32_t directorio = cwd_inodo;
    if (ruta != NULL && ruta[0] != '\0') {
        if (resolver_directorio(ruta, &directorio, NULL) != 0)
            return -1;
    }

    int cantidad = ext4_listar_directorio(directorio, entradas_ext4, VFS_MAX_ENTRADAS);
    if (cantidad < 0)
        return cantidad;
    if (cantidad > VFS_MAX_ENTRADAS)
        return -1;

    int salida = 0;
    for (int i = 0; i < cantidad && salida < max; i++) {
        ext4_dir_entry_t *entrada = &entradas_ext4[i];

        if (entrada->name_len == 1 && entrada->nombre[0] == '.')
            continue;
        if (entrada->name_len == 2 && entrada->nombre[0] == '.' &&
            entrada->nombre[1] == '.')
            continue;

        copiar_cadena(entradas[salida].nombre, entrada->nombre,
                      sizeof(entradas[salida].nombre));
        entradas[salida].inodo = entrada->inodo;

        ext4_inodo_t inodo;
        if (ext4_leer_inodo(entrada->inodo, &inodo) == 0) {
            entradas[salida].tamano = inodo.tamano;
            if ((inodo.modo & 0xF000) == 0x4000)
                entradas[salida].tipo = VFS_TIPO_CARPETA;
            else if ((inodo.modo & 0xF000) == 0x8000)
                entradas[salida].tipo = VFS_TIPO_ARCHIVO;
            else
                entradas[salida].tipo = VFS_TIPO_DESCONOCIDO;
        } else {
            entradas[salida].tamano = 0;
            if (entrada->file_type == 2)
                entradas[salida].tipo = VFS_TIPO_CARPETA;
            else if (entrada->file_type == 1)
                entradas[salida].tipo = VFS_TIPO_ARCHIVO;
            else
                entradas[salida].tipo = VFS_TIPO_DESCONOCIDO;
        }
        salida++;
    }
    return salida;
}

int vfs_cat(const char *ruta, char *buffer, size_t capacidad)
{
    if (!vfs_montado || ruta == NULL || buffer == NULL || capacidad == 0)
        return -1;
    if (capacidad > 0xFFFFFFFFu)
        capacidad = 0xFFFFFFFFu;

    uint32_t padre;
    char nombre[VFS_NAME_MAX];

    if (resolver_padre(ruta, &padre, nombre) != 0)
        return -1;

    ext4_dir_entry_t entrada;
    if (ext4_buscar_en(padre, nombre, &entrada) != 0)
        return -2;
    if (ext4_inodo_es_directorio(entrada.inodo))
        return -3;

    uint32_t leido = 0;
    int resultado = ext4_leer_archivo(entrada.inodo, buffer,
                                      (uint32_t)capacidad, &leido);
    if (resultado != 0)
        return resultado;
    return (int)leido;
}

/* ------------------------------------------------------------------ */
/* Creacion, escritura y borrado                                      */
/* ------------------------------------------------------------------ */

int vfs_mkdir(const char *ruta)
{
    if (!vfs_montado || ruta == NULL || ruta[0] == '\0')
        return -1;

    uint32_t padre;
    char nombre[VFS_NAME_MAX];
    if (resolver_padre(ruta, &padre, nombre) != 0)
        return -1;

    return ext4_crear(padre, nombre, 1, NULL);
}

int vfs_touch(const char *ruta)
{
    if (!vfs_montado || ruta == NULL || ruta[0] == '\0')
        return -1;

    uint32_t padre;
    char nombre[VFS_NAME_MAX];
    if (resolver_padre(ruta, &padre, nombre) != 0)
        return -1;

    ext4_dir_entry_t existente;
    if (ext4_buscar_en(padre, nombre, &existente) == 0)
        return 0;

    return ext4_crear(padre, nombre, 0, NULL);
}

int vfs_crear_archivo(const char *ruta)
{
    return vfs_touch(ruta);
}

int vfs_write(const char *ruta, const char *datos)
{
    if (!vfs_montado || ruta == NULL || datos == NULL)
        return -1;

    size_t longitud = longitud_cadena(datos);
    if (longitud > 0xFFFFFFFFu)
        return -1;

    uint32_t padre;
    char nombre[VFS_NAME_MAX];
    if (resolver_padre(ruta, &padre, nombre) != 0)
        return -1;

    ext4_dir_entry_t entrada;
    uint32_t inodo;

    if (ext4_buscar_en(padre, nombre, &entrada) == 0) {
        if (ext4_inodo_es_directorio(entrada.inodo))
            return -2;
        inodo = entrada.inodo;
    } else {
        int creado = ext4_crear(padre, nombre, 0, &inodo);
        if (creado != 0)
            return creado;
    }

    return ext4_escribir_archivo(inodo, datos, (uint32_t)longitud);
}

int vfs_rm(const char *ruta)
{
    if (!vfs_montado || ruta == NULL || ruta[0] == '\0')
        return -1;

    uint32_t padre;
    char nombre[VFS_NAME_MAX];
    if (resolver_padre(ruta, &padre, nombre) != 0)
        return -1;

    ext4_dir_entry_t entrada;
    if (ext4_buscar_en(padre, nombre, &entrada) != 0)
        return -2;
    if (ext4_inodo_es_directorio(entrada.inodo))
        return -3;

    return ext4_eliminar(padre, nombre);
}

int vfs_rmdir(const char *ruta)
{
    if (!vfs_montado || ruta == NULL || ruta[0] == '\0')
        return -1;

    uint32_t padre;
    char nombre[VFS_NAME_MAX];
    if (resolver_padre(ruta, &padre, nombre) != 0)
        return -1;

    ext4_dir_entry_t entrada;
    if (ext4_buscar_en(padre, nombre, &entrada) != 0)
        return -2;
    if (!ext4_inodo_es_directorio(entrada.inodo))
        return -3;
    if (es_ancestro(entrada.inodo, cwd_inodo))
        return -4;

    return ext4_eliminar(padre, nombre);
}

int vfs_rrm(const char *ruta)
{
    if (!vfs_montado || ruta == NULL || ruta[0] == '\0')
        return -1;

    uint32_t padre;
    char nombre[VFS_NAME_MAX];
    if (resolver_padre(ruta, &padre, nombre) != 0)
        return -1;

    ext4_dir_entry_t entrada;
    if (ext4_buscar_en(padre, nombre, &entrada) != 0)
        return -2;
    if (es_ancestro(entrada.inodo, cwd_inodo))
        return -3;

    return ext4_eliminar_recursivo(padre, nombre);
}
