#include <stdint.h>
#include "fs/ramfs.h"
#include "shell/shell.h"
#include "console/consola.h"
#include "driver/PCI/pci.h"
#include "fs/ext4.h"
#include "driver/IDE/ide.h"

//variables movidos a shell.c
//static uint16_t *video = (uint16_t *)0xB8000;
//static int posicion = 0;

//MOVIDO A PCI

//funciones movidos a shell.c

void probar_ext4(void)
{
    imprimir("Verificando disco IDE...", "blanco", 1);

    ext4_read_bytes_fn leer = ide_leer_bytes;
    int resultado = ext4_montar(leer, 0);

    if (resultado == 0) {
        imprimir("Ext4 detectado correctamente.", "verde", 1);
    } else {
        imprimir("Error al montar ext4. Codigo: ", "rojo", 1);
        /* Imprime aquí el código usando tu función numérica. */
    }
    
}





void kernel_main(void) {
    //char *video = (char *)0xB8000;
    int resultado = ext4_montar(ide_leer_bytes, 0);
    ext4_inodo_t raiz;

if (resultado == 0) {
    imprimir("Superbloque ext4 detectado", "verde", 1);

    const ext4_info_t *info = ext4_obtener_info();

    if (info != 0) {
        imprimir("Sistema de archivos disponible", "verde", 1);
    }
} else {
    imprimir("Error al detectar ext4", "rojo", 1);

}
    imprimir("BIENVENIDO A SOLARIS 3.0", "verde", 1);
    imprimir("solaris> ", "amarillo", 0);
    probar_ext4();
    if (ext4_leer_inodo(2, &raiz) == 0) {
    imprimir("Inodo raiz leido correctamente", "verde", 1);
} else {
    imprimir("Error al leer el inodo raiz", "rojo", 1);
}
    
    shell_ejecutar();
    while (1) {
        __asm__ volatile ("hlt");
    }
    
}