#include <stdint.h>
#include "../driver/PCI/pci.h"
#include "../shell/shell.h"
#include "../driver/io.h"
static uint16_t *video = (uint16_t *)0xB8000;
static int posicion = 0;
void actualizar_cursor(void) {
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(posicion & 0xFF));

    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((posicion >> 8) & 0xFF));
}
void clear(void) {
    for (int i = 0; i < 80 * 25; i++) {
        video[i] = ((uint8_t)0x0F << 8) | ' ';
    }
    posicion = 0;
    actualizar_cursor();
}

void print(const char *text) {
    char *video = (char *)0xB8000;
    int i = 0;
    while (text[i] != '\0') {
        video[i * 2] = text[i];
        video[i * 2 + 1] = 0x0F;
        i++;
    }
}
static void desplazar_pantalla(void) {
    while (posicion >= 80 * 25) {
        for (int i = 0; i < 80 * 24; i++) {
            video[i] = video[i + 80];
        }
        for (int i = 80 * 24; i < 80 * 25; i++) {
            video[i] = ((uint16_t)0x0F << 8) | ' ';
        }
        posicion -= 80;
    }
}
void imprimir(char *text, char *color, int salto) {
    uint8_t atributo = 0x0F;
    if (comparar_cadenas(color, "verde")) {
        atributo = 0x0A;
    } else if (comparar_cadenas(color, "amarillo")) {
        atributo = 0x0E;
    } else if (comparar_cadenas(color, "rosado")) {
        atributo = 0x0D;
    } else if (comparar_cadenas(color, "rojo")) {
        atributo = 0x0C;
    }

    while (*text) {
        if (*text == '\n') {
            posicion = ((posicion / 80) + 1) * 80;
        } else if (*text == '\b') {
            if (posicion > 0) {
                posicion--;
            }
            video[posicion] = ((uint16_t)atributo << 8) | ' ';
        } else {
            video[posicion] = ((uint16_t)atributo << 8) | (uint8_t)*text;
            posicion++;
        }
        if (posicion >= 80 * 25) {
            desplazar_pantalla();
        }
        text++;
    }
    if (salto) {
        posicion = ((posicion / 80) + 1) * 80;
    }
    if (posicion >= 80 * 25) {
        desplazar_pantalla();
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