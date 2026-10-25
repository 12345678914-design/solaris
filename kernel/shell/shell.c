#include <stdint.h>

#include "shell.h"
#include "../driver/teclado.h"
#include "../console/consola.h"
#include "../fs/ramfs.h"
#include "../driver/io.h"
#include "../driver/PCI/power.h"

#define MAXCOMMAND 128
#define MAX_ARGS 16


char comando[MAXCOMMAND];
int longitud_comando = 0;
char *user = nullptr;
static char usuario_almacenado[MAXCOMMAND] = "";

int comparar_cadenas(char *a, char *b) {
    int i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if(a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}
//mapa de teclas movido

//funciones de consola movidos a consola.c

//leer tecla movido
char *leer_input_char () {
    longitud_comando = 0;
    comando[0] = '\0';
    while(1) {
        char tecla = teclado_leer_caracter();
        if(tecla == 0 || tecla == '\t') {
            continue;
        }
        if(tecla == '\n') {
            comando[longitud_comando] = '\0';
            return comando;
        }
        else if(tecla == '\b') {
            if (longitud_comando > 0){
                longitud_comando--;
                comando[longitud_comando] = '\0';
                imprimir("\b \b", "blanco", 0);
            }
        }
        else if (longitud_comando < MAXCOMMAND - 1) {
            comando[longitud_comando++] = tecla;
            comando[longitud_comando] = '\0';

            char texto[2] = {
                tecla, '\0'
            };
            imprimir(texto, "blanco", 0);
        }
    }
    longitud_comando = 0;
    comando[0] = '\0';
}

int length(char *text) {
    int i = 0;
    while(text[i] != '\0') {
        i++;
    }
    return i;
}

void mensaje(char *text) {
    int i = 0;
    while (text[i] != '\0') {
        i++;
    }
    int j = 0;
    while (comando[j] != '\0' && i < 256){
        text[i++] = comando[j++];
    }
    text[i] = '\0';
    imprimir(text, "blanco", 1);
    text[0] = '\0';
}


int analizar_comando(char *texto, char *argv[], int max_args)
{
    int argc = 0;
    char *p = texto;
    char *dest = texto;

    while (*p != '\0') {

        // Saltar espacios
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
                    // Avanzar ANTES de escribir '\0'
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



void procesar_comando(char *text)
{
    char comando1[MAXCOMMAND];
    char *argumento;
    int i = 0;
    while(text[i] != '\0' && text[i] != ' ') {
        comando1[i] = text[i];
        i++;
    }
    comando1[i] = '\0';
    argumento = text + i;
    int j = 0;
    while ((argumento[j] == ' ' && argumento[j] != '\0') || argumento[j] == '"') {
        j++;
    }
    argumento += j;
    int tamaño = length(argumento);
    if (tamaño > 0 && argumento[tamaño - 1] == '"') {
        argumento[tamaño - 1] = '\0';
    }
    //por corregir, comando no atrapa la cadena correcta
    if (comparar_cadenas(text, "help")) {
        imprimir("", "blanco", 1);
        imprimir("comando para saber", "blanco", 1);
    }
    else if (comparar_cadenas(text, "clear")) {
        clear();
    }
    else if (comparar_cadenas(text, "about")) {
        imprimir("", "blanco", 1);
        imprimir("SOLARIS 3.0", "verde", 1);
        imprimir("Kernel 1.0", "blanco", 1);
    }
    else if (comparar_cadenas(comando1, "echo")) {
        imprimir("", "blanco", 1);
        imprimir(argumento, "blanco", 1);
    }
    else if (comparar_cadenas(comando1, "kernel")) {
        imprimir("", "blanco", 1);
        if(comparar_cadenas(argumento, "--version") || comparar_cadenas(argumento, "-v")){
            imprimir("version 1.0.1 ", "blanco", 1);
        }

        else {
            imprimir("argumento invalido ", "rojo", 1);
        }
    }
    else if (comparar_cadenas(text, "ls")) {
        char lista[RAMFS_MAX_FILES * (RAMFS_NAME_MAX + 1)];
        imprimir("", "blanco", 1);
        if(ramfs_list(lista, sizeof(lista))) {
            imprimir(lista, "blanco", 1);
        }
        else {
            imprimir("error al listar archivos ", "rojo", 1);
        }
    }
    else if (comparar_cadenas(comando1, "mkdir")) {
        if(argumento[0] != '\0') {
            if(comparar_cadenas(argumento, "-f") || comparar_cadenas(argumento, "--file")){
                imprimir("comando para crear archivos", "verde", 1);
            }
            else if (comparar_cadenas(argumento, "-p")){

        }

        else {
            imprimir("requiere un nombre ", "blanco", 1);
        }
    }
}
    else if (comparar_cadenas(comando1, "open")) {
        char contenido[RAMFS_DATA_MAX];
        if (argumento[0] != '\0') {
            if (ramfs_read(argumento, contenido, sizeof(contenido))) {
                imprimir(contenido, "blanco", 1);
                if (contenido[0] == '\0') {
                    char *contenido1 = leer_input_char();
                    
                    ramfs_write(argumento, contenido1);
                }
                else {
                    imprimir(contenido, "blanco", 1);
                }
            }
            else {
                imprimir("error no se pudo leer el archivo ", "rojo", 1);
            }
        }
    }
    else if (comparar_cadenas(text, "shutdown")) {
        imprimir("", "blanco", 1);
        imprimir("Apagando...", "verde", 1);
        apagar();
    }
    else if (comparar_cadenas(text, "reboot")) {
        imprimir("", "blanco", 1);
        imprimir("Reiniciando...", "verde", 1);
        reiniciar();
    }
    else if (comparar_cadenas(text, "whoami")) {
        imprimir("", "blanco", 1);
        if (usuario_almacenado[0] == '\0') {
            imprimir("Usuario no configurado", "amarillo", 1);
        } else {
            imprimir(usuario_almacenado, "blanco", 1);
        }
    }
    else if(text[0] == '\0') {
        imprimir("", "blanco", 1);
    }
    else {
        imprimir("Comando desconocido: ", "rojo", 0);
        imprimir(comando1, "rojo", 0);
        imprimir(" argumento: ", "blanco", 0);
        imprimir(argumento, "blanco", 1);
    }

    text[0] = '\0';
}


void shell_ejecutar (void) {
    longitud_comando = 0;
    comando[0] = '\0';
    imprimir("", "blanco", 0);
    while(1) {
        char tecla = teclado_leer_caracter();
        if (tecla == 0 || tecla == '\t') {
            continue;
        }
        if(tecla == '\n') {
            comando[longitud_comando] = '\0';
            //imprimir("", 1);
            procesar_comando(comando);
            imprimir("solaris> ", "amarillo", 0);
            longitud_comando = 0;
            comando[0] = '\0';
        }
        else if (tecla == '\b') {
            if (longitud_comando > 0){
                longitud_comando--;
                comando[longitud_comando] = '\0';
                imprimir("\b \b", "blanco", 0);
            }
        }
        else if (longitud_comando < MAXCOMMAND - 1) {
            comando[longitud_comando++] = tecla;
            comando[longitud_comando] = '\0';

            char texto[2] = {
                tecla, '\0'
            };
            imprimir(texto, "blanco", 0);
        }
        
    }
}