#include <stdint.h>

#include "shell.h"
#include "../driver/teclado.h"
#include "../console/consola.h"
#include "../driver/io.h"
#include "../driver/PCI/power.h"
#include "../fs/vfs.h"
#include "../fs/ext4.h"

#define MAXCOMMAND 128
#define MAX_ARGS 16

char comando[MAXCOMMAND];
int longitud_comando = 0;
static char usuario_almacenado[MAXCOMMAND] = "";

int comparar_cadenas(char *a, char *b) {
    int i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

char *leer_input_char(void) {
    longitud_comando = 0;
    comando[0] = '\0';
    while (1) {
        char tecla = teclado_leer_caracter();
        if (tecla == 0 || tecla == '\t') {
            continue;
        }
        if (tecla == '\n') {
            comando[longitud_comando] = '\0';
            imprimir("", "blanco", 1);
            return comando;
        } else if (tecla == '\b') {
            if (longitud_comando > 0) {
                longitud_comando--;
                comando[longitud_comando] = '\0';
                imprimir("\b \b", "blanco", 0);
            }
        } else if (longitud_comando < MAXCOMMAND - 1) {
            comando[longitud_comando++] = tecla;
            comando[longitud_comando] = '\0';

            char texto[2] = {tecla, '\0'};
            imprimir(texto, "blanco", 0);
        }
    }
    longitud_comando = 0;
    comando[0] = '\0';
}

int length(char *text) {
    int i = 0;
    while (text[i] != '\0') {
        i++;
    }
    return i;
}

static void copiar_texto(char *destino, const char *origen, int capacidad) {
    int i = 0;
    if (destino == 0 || origen == 0 || capacidad <= 0) {
        return;
    }
    while (i < capacidad - 1 && origen[i] != '\0') {
        destino[i] = origen[i];
        i++;
    }
    destino[i] = '\0';
}

void mensaje(char *text) {
    int i = 0;
    while (text[i] != '\0') {
        i++;
    }
    int j = 0;
    while (comando[j] != '\0' && i < 256) {
        text[i++] = comando[j++];
    }
    text[i] = '\0';
    imprimir(text, "blanco", 1);
    text[0] = '\0';
}

int analizar_comando(char *texto, char *argv[], int max_args) {
    int argc = 0;
    char *p = texto;
    char *dest = texto;

    while (*p != '\0') {
        while (*p == ' ' || *p == '\t') {
            p++;
        }

        if (*p == '\0' || argc >= max_args) {
            break;
        }

        argv[argc++] = dest;
        char comilla = '\0';

        while (*p != '\0') {
            if (comilla != '\0') {
                if (*p == comilla) {
                    comilla = '\0';
                    p++;
                } else {
                    *dest++ = *p++;
                }
            } else {
                if (*p == '"' || *p == '\'') {
                    comilla = *p++;
                } else if (*p == ' ' || *p == '\t') {
                    p++;
                    break;
                } else {
                    *dest++ = *p++;
                }
            }
        }

        *dest++ = '\0';
    }

    return argc;
}

void procesar_comando(char *text) {
    if (text[0] == '\0') {
        imprimir("", "blanco", 1);
        return;
    }

    char comando1[MAXCOMMAND];
    char *argumento;
    int i = 0;
    while (text[i] != '\0' && text[i] != ' ' && text[i] != '\t') {
        if (i < MAXCOMMAND - 1) {
            comando1[i] = text[i];
            i++;
        } else {
            break;
        }
    }
    comando1[i] = '\0';
    argumento = text + i;
    int j = 0;
    while ((argumento[j] == ' ' || argumento[j] == '\t') && argumento[j] != '\0') {
        j++;
    }
    argumento += j;
    int tamano_arg = length(argumento);
    while (tamano_arg > 0 && (argumento[tamano_arg - 1] == ' ' || argumento[tamano_arg - 1] == '\t')) {
        argumento[tamano_arg - 1] = '\0';
        tamano_arg--;
    }
    if (tamano_arg > 0 && argumento[tamano_arg - 1] == '"') {
        argumento[tamano_arg - 1] = '\0';
    }
    if (tamano_arg > 0 && argumento[0] == '"') {
        argumento++;
    }

    if (comparar_cadenas(comando1, "help")) {
        imprimir("", "blanco", 1);
        imprimir("Comandos disponibles:", "verde", 1);
        imprimir("  help        - Muestra esta ayuda", "blanco", 1);
        imprimir("  clear       - Limpia la pantalla", "blanco", 1);
        imprimir("  about       - Informacion del sistema", "blanco", 1);
        imprimir("  ls [ruta]   - Lista archivos/directorios (ext4)", "blanco", 1);
        imprimir("  cd <ruta>   - Cambia de directorio", "blanco", 1);
        imprimir("  pwd         - Muestra directorio actual", "blanco", 1);
        imprimir("  cat <archivo> - Lee y muestra archivo", "blanco", 1);
        imprimir("  mkdir <dir> - Crea directorio", "blanco", 1);
        imprimir("  mkdir -f <archivo> - Crea archivo", "blanco", 1);
        imprimir("  touch <archivo>   - Crea archivo", "blanco", 1);
        imprimir("  write <archivo>   - Escribe contenido", "blanco", 1);
        imprimir("  rm <archivo>      - Elimina archivo", "blanco", 1);
        imprimir("  rmdir <carpeta>   - Elimina carpeta vacia", "blanco", 1);
        imprimir("  rrm <carpeta>     - Elimina carpeta recursivamente", "blanco", 1);
        imprimir("  echo <texto> - Imprime texto", "blanco", 1);
        imprimir("  kernel -v   - Version del kernel", "blanco", 1);
        imprimir("  whoami      - Usuario actual", "blanco", 1);
        imprimir("  shutdown    - Apagar", "blanco", 1);
        imprimir("  reboot      - Reiniciar", "blanco", 1);
        return;
    } else if (comparar_cadenas(comando1, "clear")) {
        clear();
        return;
    } else if (comparar_cadenas(comando1, "about")) {
        imprimir("", "blanco", 1);
        imprimir("SOLARIS 3.0", "verde", 1);
        imprimir("Kernel 1.0", "blanco", 1);
        return;
    } else if (comparar_cadenas(comando1, "echo")) {
        imprimir("", "blanco", 1);
        imprimir(argumento, "blanco", 1);
        return;
    } else if (comparar_cadenas(comando1, "kernel")) {
        imprimir("", "blanco", 1);
        if (comparar_cadenas(argumento, "--version") || comparar_cadenas(argumento, "-v")) {
            imprimir("version 1.0.1 ", "blanco", 1);
        } else {
            imprimir("argumento invalido ", "rojo", 1);
        }
        return;
    } else if (comparar_cadenas(comando1, "ls")) {
        imprimir("", "blanco", 1);
        vfs_entrada_t entradas[64];
        int n = vfs_ls(argumento[0] != '\0' ? argumento : ".", entradas, 64);
        if (n < 0) {
            imprimir("Error al listar", "rojo", 1);
            return;
        }
        if (n == 0) {
            imprimir("(vacio)", "amarillo", 1);
            return;
        }
        for (int k = 0; k < n; k++) {
            if (entradas[k].tipo == VFS_TIPO_CARPETA) {
                imprimir("[DIR] ", "azul", 0);
                imprimir(entradas[k].nombre, "azul", 1);
            } else if (entradas[k].tipo == VFS_TIPO_ARCHIVO) {
                imprimir("[FILE] ", "blanco", 0);
                imprimir(entradas[k].nombre, "blanco", 1);
            } else {
                imprimir("[?] ", "amarillo", 0);
                imprimir(entradas[k].nombre, "blanco", 1);
            }
        }
        return;
    } else if (comparar_cadenas(comando1, "cd")) {
        imprimir("", "blanco", 1);
        if (argumento[0] == '\0') {
            vfs_cd("/");
            imprimir("/", "blanco", 1);
            return;
        }
        if (vfs_cd(argumento) == 0) {
            imprimir(vfs_get_cwd(), "blanco", 1);
        } else {
            imprimir("No se puede cambiar de directorio", "rojo", 1);
        }
        return;
    } else if (comparar_cadenas(comando1, "pwd")) {
        imprimir("", "blanco", 1);
        imprimir(vfs_get_cwd(), "blanco", 1);
        return;
    } else if (comparar_cadenas(comando1, "cat")) {
        imprimir("", "blanco", 1);
        if (argumento[0] == '\0') {
            imprimir("Uso: cat <archivo>", "rojo", 1);
            return;
        }
        char buffer[1024];
        int res = vfs_cat(argumento, buffer, sizeof(buffer));
        if (res < 0) {
            imprimir("Error al leer archivo", "rojo", 1);
            return;
        }
        if (buffer[0] == '\0') {
            imprimir("(archivo vacio)", "amarillo", 1);
            return;
        }
        imprimir(buffer, "blanco", 1);
        return;
    } else if (comparar_cadenas(comando1, "touch")) {
        imprimir("", "blanco", 1);
        if (argumento[0] == '\0') {
            imprimir("Uso: touch <archivo>", "rojo", 1);
            return;
        }
        if (vfs_crear_archivo(argumento) == 0) {
            imprimir("Archivo creado", "verde", 1);
        } else {
            imprimir("No se pudo crear archivo", "rojo", 1);
        }
        return;
    } else if (comparar_cadenas(comando1, "mkdir")) {
        imprimir("", "blanco", 1);
        if (argumento[0] == '\0') {
            imprimir("Uso: mkdir <dir> o mkdir -f <archivo>", "rojo", 1);
            return;
        }
        if (argumento[0] == '-' && argumento[1] == 'f') {
            int k = 2;
            while (argumento[k] == ' ' || argumento[k] == '\t')
                k++;
            if (argumento[k] == '\0') {
                imprimir("Uso: mkdir -f <nombre_archivo>", "rojo", 1);
                return;
            }
            char *nombre = argumento + k;
            if (vfs_crear_archivo(nombre) == 0) {
                imprimir("Archivo creado", "verde", 1);
                return;
            } else {
                imprimir("No se pudo crear archivo", "rojo", 1);
                return;
            }
        }
        if (vfs_mkdir(argumento) == 0) {
            imprimir("Directorio creado", "verde", 1);
        } else {
            imprimir("No se pudo crear directorio", "rojo", 1);
        }
        return;
    } else if (comparar_cadenas(comando1, "write")) {
        imprimir("", "blanco", 1);
        if (argumento[0] == '\0') {
            imprimir("Uso: write <archivo>", "rojo", 1);
            return;
        }
        imprimir("Escriba el contenido (termine con Enter):", "amarillo", 1);
        imprimir("> ", "amarillo", 0);
        char destino[MAXCOMMAND];
        copiar_texto(destino, argumento, MAXCOMMAND);
        char *contenido = leer_input_char();
        if (vfs_write(destino, contenido) == 0) {
            imprimir("Escrito correctamente", "verde", 1);
        } else {
            imprimir("No se pudo escribir el archivo", "rojo", 1);
        }
        return;
    } else if (comparar_cadenas(comando1, "open")) {
        imprimir("", "blanco", 1);
        if (argumento[0] == '\0') {
            imprimir("Uso: open <archivo>", "rojo", 1);
            return;
        }
        char buffer[1024];
        int res = vfs_cat(argumento, buffer, sizeof(buffer));
        if (res < 0) {
            imprimir("Archivo no encontrado o error", "rojo", 1);
            return;
        }
        if (buffer[0] == '\0') {
            imprimir("(archivo vacio) - escribiendo...", "amarillo", 1);
            imprimir("> ", "amarillo", 0);
            char destino[MAXCOMMAND];
            copiar_texto(destino, argumento, MAXCOMMAND);
            char *contenido = leer_input_char();
            if (vfs_write(destino, contenido) == 0) {
                imprimir("Escrito correctamente", "verde", 1);
            } else {
                imprimir("No se pudo escribir el archivo", "rojo", 1);
            }
            return;
        }
        imprimir(buffer, "blanco", 1);
        return;
    } else if (comparar_cadenas(comando1, "rm")) {
        imprimir("", "blanco", 1);
        if (argumento[0] == '\0') {
            imprimir("Uso: rm <archivo>", "rojo", 1);
            return;
        }
        if (vfs_rm(argumento) == 0) {
            imprimir("Archivo eliminado", "verde", 1);
        } else {
            imprimir("No se pudo eliminar el archivo", "rojo", 1);
        }
        return;
    } else if (comparar_cadenas(comando1, "rrm")) {
        imprimir("", "blanco", 1);
        if (argumento[0] == '\0') {
            imprimir("Uso: rrm <carpeta> (recursivo)", "rojo", 1);
            return;
        }
        if (vfs_rrm(argumento) == 0) {
            imprimir("Eliminacion recursiva completada", "verde", 1);
        } else {
            imprimir("No se pudo eliminar recursivamente", "rojo", 1);
        }
        return;
    } else if (comparar_cadenas(comando1, "rmdir")) {
        imprimir("", "blanco", 1);
        if (argumento[0] == '\0') {
            imprimir("Uso: rmdir <carpeta>", "rojo", 1);
            return;
        }
        if (vfs_rmdir(argumento) == 0) {
            imprimir("Carpeta eliminada", "verde", 1);
        } else {
            imprimir("No se pudo eliminar la carpeta", "rojo", 1);
        }
        return;
    } else if (comparar_cadenas(comando1, "shutdown")) {
        imprimir("", "blanco", 1);
        imprimir("Apagando...", "verde", 1);
        apagar();
        return;
    } else if (comparar_cadenas(comando1, "reboot")) {
        imprimir("", "blanco", 1);
        imprimir("Reiniciando...", "verde", 1);
        reiniciar();
        return;
    } else if (comparar_cadenas(comando1, "whoami")) {
        imprimir("", "blanco", 1);
        if (usuario_almacenado[0] == '\0') {
            imprimir("Usuario no configurado", "amarillo", 1);
        } else {
            imprimir(usuario_almacenado, "blanco", 1);
        }
        return;
    } else {
        imprimir("Comando desconocido: ", "rojo", 0);
        imprimir(comando1, "rojo", 0);
        imprimir("", "blanco", 1);
        return;
    }
}

void shell_ejecutar(void) {
    longitud_comando = 0;
    comando[0] = '\0';
    imprimir("", "blanco", 0);
    while (1) {
        char tecla = teclado_leer_caracter();
        if (tecla == 0 || tecla == '\t') {
            continue;
        }
        if (tecla == '\n') {
            comando[longitud_comando] = '\0';
            procesar_comando(comando);
            imprimir("solaris> ", "amarillo", 0);
            longitud_comando = 0;
            comando[0] = '\0';
        } else if (tecla == '\b') {
            if (longitud_comando > 0) {
                longitud_comando--;
                comando[longitud_comando] = '\0';
                imprimir("\b \b", "blanco", 0);
            }
        } else if (longitud_comando < MAXCOMMAND - 1) {
            comando[longitud_comando++] = tecla;
            comando[longitud_comando] = '\0';

            char texto[2] = {tecla, '\0'};
            imprimir(texto, "blanco", 0);
        }
    }
}
