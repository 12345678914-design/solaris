#include <stdint.h>
#include "fs/ramfs.h"
#define MAXCOMMAND 128
#define MAX_ARGS 16


char comando[MAXCOMMAND];
int longitud_comando = 0;
char *user = nullptr;
static char usuario_almacenado[MAXCOMMAND] = "";
static uint16_t *video = (uint16_t *)0xB8000;
static int posicion = 0;

static inline void outb(uint16_t puerto, uint8_t valor) {
    __asm__ volatile ("outb %0, %1"::"a"(valor), "Nd"(puerto));
}

static inline void outw(uint16_t puerto, uint16_t valor) {
    __asm__ volatile ("outw %0, %1"::"a"(valor), "Nd"(puerto));
}
static inline uint8_t inb(uint16_t puerto) {
    uint8_t resultado;
    __asm__ volatile ("inb %w1, %0":"=a"(resultado):"Nd"(puerto));
    return resultado;
}
void apagar(void){
    outw(0x604, 0x2000);
    for(;;) {
        __asm__ volatile("cli;hlt");
    }
}
void reiniciar(void) {
    __asm__ volatile ("cli");
        outb(0xCF9, 0x02);
        outb(0xCF9, 0x06);

        for(;;) {
            __asm__ volatile("hlt");
        }
}

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
static const char mapa_teclas[128] = {
    [0x02] = '1',
    [0x03] = '2',
    [0x04] = '3',
    [0x05] = '4',
    [0x06] = '5',
    [0x07] = '6',
    [0x08] = '7',
    [0x09] = '8',
    [0x0A] = '9',
    [0x0B] = '0',
    [0x0C] = '-',
    [0x0D] = '=',
    [0x0E] = '\b',
    [0x0F] = '\t',
    [0x10] = 'q',
    [0x11] = 'w',
    [0x12] = 'e',
    [0x13] = 'r',
    [0x14] = 't',
    [0x15] = 'y',
    [0x16] = 'u',
    [0x17] = 'i',
    [0x18] = 'o',
    [0x19] = 'p',
    [0x1A] = '[',
    [0x1B] = ']',
    [0x1C] = '\n',
    [0x1E] = 'a',
    [0x1F] = 's',
    [0X20] = 'd',
    [0x21] = 'f',
    [0x22] = 'g',
    [0x23] = 'h',
    [0x24] = 'j',
    [0x25] = 'k',
    [0x26] = 'l',
    [0x27] = ';',
    [0x28] = '\'',
    [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z',
    [0x2D] = 'x',
    [0x2E] = 'c',
    [0x2F] = 'v',
    [0x30] = 'b',
    [0x31] = 'n',
    [0x32] = 'm',
    [0x33] = ',',
    [0x34] = '.',
    [0x35] = '/',
    [0x39] = ' ',


};

void actualizar_cursor(void) {

    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(posicion & 0xFF));

    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((posicion >> 8) & 0xFF));
}
void clear(void) {
    for(int i = 0; i < 80*25; i++){
        video[i] = ((uint8_t)0x0F <<8) | ' ';
    }
    posicion = 0;
    actualizar_cursor();
}

void print(const char *text){
    char *video = (char *)0xB8000;
    int i = 0;
    while (text[i] != '\0'){
        video[i*2] = text[i];
        video[i*2 + 1] = 0x0F;
        i++;
    }
}
void imprimir(char *text, char *color, int salto) {

    uint8_t atributo = 0x0F;
    if(comparar_cadenas(color, "verde")){
        atributo = 0x0A;
    }
    else if(comparar_cadenas(color, "amarillo")) {
        atributo = 0x0E;
    }
    else if(comparar_cadenas(color, "rosado")) {
        atributo = 0x0D;
    }
    else if(comparar_cadenas(color, "rojo")) {
        atributo = 0x0C;
    }

    while(*text) {
        if(*text == '\n') {
            posicion = ((posicion / 80) + 1) * 80;
        }
        else if(*text == '\b') {
            if(posicion > 0) {
                posicion--;
            }
            video[posicion] = ((uint16_t)atributo << 8) | ' ';
        }
        else {
            video[posicion] = ((uint16_t)atributo << 8)|(uint8_t)*text;
            posicion++;
        }
        if(posicion >= 80*25) {
            posicion = 0;
        }
        text++;
    }
    if(salto) {
        posicion = ((posicion / 80) + 1) * 80;
    }
    if(posicion >= 80*25) {
        posicion = 0;
    }
    actualizar_cursor();
}
void imprimirLogo(char *color) {                                                                                   
                                                                                
imprimir("                                        .                                       ", color, 1);
imprimir("                                        : .                                     ", color, 1);
imprimir("                                      ..-                                       ", color, 1);
imprimir("                                       :=.  .                                   ", color, 1);
imprimir("                                       :*:  .                                   ", color, 1);
imprimir("                                    :..-*:...                                   ", color, 1);
imprimir("                               ....:=--+#=-:=-...                               ", color, 1);
imprimir("                             ::--++**#**##+**#==-:...                ...        ", color, 1);
imprimir("                          ...:-:=#+--:.   .:+*+=--:::..........::::::::::-:...  ", color, 1);
imprimir("                         ..::+*+-::=+##*##**+-.-*+*+-:--:::---==+++==--:.--:.   ", color, 1);
imprimir("                        .::-=+-.-#*-.      .-#+=:-++++*##**###***=-:==-:-:..   .", color, 1);
imprimir("                       ..:=+=:-=*-            .*=:.:-+###*+==+##*=----::..     .", color, 1);
imprimir("                      .:-++=.:++.              .*+:  :=######+=-:--::..        .", color, 1);
imprimir("                    ..:=*==:.=#-                =#*######+=-=---:.....         .", color, 1);
imprimir("                ...::-==++:. =#.           ..+*###*+=-=+-:::....               .", color, 1);
imprimir("             ...:::=+==*+:.  -*:     ..-*#*##*==+=---:.:...                    .", color, 1);
imprimir("          .::-:-=++++**==:  .-*#+**###*+*+=+==:-*=:......                      .", color, 1);
imprimir("       .:::----*#+-*###########*+*==+=-:-: .  :*+:......                       .", color, 1);
imprimir("     ::::==:=++*#*=++++**+++--=--+++-.     -+#+=-::..                           ", color, 1);
imprimir("   ..:-:::-----------:::--::....:::=***+##++=-:--::..                           ", color, 1);
imprimir("   .:..:=--=--::::....  ..   .:.:====+=+#+-=*+-.:.                             .", color, 1);
imprimir("        .       .             . ..::=++*#+-:::. .                              .", color, 1);
imprimir("                                   ..:.=#:...                                   ", color, 1);
imprimir("                                     ..-#:.                                     ", color, 1);
imprimir("                                       .=. .                                    ", color, 1);
imprimir("                                       .-                                       ", color, 1);
imprimir("                                        ::.                                     ", color, 1);
imprimir("                                        .                                       ", color, 1);
                                                                            
}

static char leer__tecla(void) {
    uint8_t scancode;
    while((inb(0x64) & 1) == 0) {
        __asm__ volatile ("pause");
    }
    scancode = inb(0x60);
    if(scancode & 0x80) {
        return 0;
    }
    if (scancode >= 128) {
        return 0;
    }

    return mapa_teclas[scancode];

}
char *leer_input_char () {
    longitud_comando = 0;
    comando[0] = '\0';
    while(1) {
        char tecla = leer__tecla();
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
    while (argumento[j] == ' ' && argumento[j] != '\0') {
        j++;
    }
    argumento += j;
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
    else if (comparar_cadenas(comando1, "file")) {
        if(argumento[0] != '\0') {
            ramfs_create(argumento);
            imprimir("", "blanco", 1);
        }
        else {
            imprimir("requiere un nombre ", "blanco", 1);
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


void leer_input(void) {
    longitud_comando = 0;
    comando[0] = '\0';
    imprimir("", "blanco", 0);
    while(1) {
        char tecla = leer__tecla();
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






void kernel_main(void) {
    char *video = (char *)0xB8000;
    imprimir("BIENVENIDO A SOLARIS 3.0", "verde", 1);
    imprimir("solaris> ", "amarillo", 0);
    leer_input();
    while (1) {
        __asm__ volatile ("hlt");
    }
    
}