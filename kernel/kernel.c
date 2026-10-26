#include <stdint.h>

#include "shell/shell.h"
#include "console/consola.h"
#include "fs/ext4.h"
#include "fs/vfs.h"

void kernel_main(void) {
    vfs_init();

    int resultado = vfs_montar_ext4();

    if (resultado != 0) {
        imprimir("Error al montar ext4", "rojo", 1);
        imprimir("No se pudo inicializar el sistema de archivos", "rojo", 1);
    } else {
        const ext4_info_t *info = ext4_obtener_info();

        imprimir("Sistema de archivos ext4 montado", "verde", 1);

        if (info != 0) {
            imprimir("  escritura: habilitada (bitmaps + checksums)", "verde", 1);
        }

        ext4_inodo_t raiz;
        if (ext4_leer_inodo(2, &raiz) == 0) {
            imprimir("  inodo raiz leido correctamente", "verde", 1);
        } else {
            imprimir("  no se pudo leer el inodo raiz", "rojo", 1);
        }
    }

    imprimir("BIENVENIDO A SOLARIS 3.0", "verde", 1);
    imprimir("solaris> ", "amarillo", 0);

    shell_ejecutar();

    while (1) {
        __asm__ volatile("hlt");
    }
}
