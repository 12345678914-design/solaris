
#include <stdint.h>
#include "io.h"

/* Teclado con distribución US */
static const char mapa_teclas[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3',
    [0x05] = '4', [0x06] = '5', [0x07] = '6',
    [0x08] = '7', [0x09] = '8', [0x0A] = '9',
    [0x0B] = '0', [0x0C] = '-', [0x0D] = '=',
    [0x0E] = '\b', [0x0F] = '\t',

    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e',
    [0x13] = 'r', [0x14] = 't', [0x15] = 'y',
    [0x16] = 'u', [0x17] = 'i', [0x18] = 'o',
    [0x19] = 'p', [0x1A] = '[', [0x1B] = ']',
    [0x1C] = '\n',

    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd',
    [0x21] = 'f', [0x22] = 'g', [0x23] = 'h',
    [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'',
    [0x29] = '`', [0x2B] = '\\',

    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c',
    [0x2F] = 'v', [0x30] = 'b', [0x31] = 'n',
    [0x32] = 'm', [0x33] = ',', [0x34] = '.',
    [0x35] = '/', [0x39] = ' '
};

static const char mapa_shift[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#',
    [0x05] = '$', [0x06] = '%', [0x07] = '^',
    [0x08] = '&', [0x09] = '*', [0x0A] = '(',
    [0x0B] = ')', [0x0C] = '_', [0x0D] = '+',

    [0x0E] = '\b', [0x0F] = '\t',

    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E',
    [0x13] = 'R', [0x14] = 'T', [0x15] = 'Y',
    [0x16] = 'U', [0x17] = 'I', [0x18] = 'O',
    [0x19] = 'P', [0x1A] = '{', [0x1B] = '}',
    [0x1C] = '\n',

    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D',
    [0x21] = 'F', [0x22] = 'G', [0x23] = 'H',
    [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = ':', [0x28] = '"',
    [0x29] = '~', [0x2B] = '|',

    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C',
    [0x2F] = 'V', [0x30] = 'B', [0x31] = 'N',
    [0x32] = 'M', [0x33] = '<', [0x34] = '>',
    [0x35] = '?', [0x39] = ' '
};

/* Estado del teclado */
static uint8_t shift_izquierdo = 0;
static uint8_t shift_derecho = 0;
static uint8_t bloq_mayus = 0;
static uint8_t tecla_mayus_pulsada = 0;
static uint8_t prefijo_extendido = 0;

static uint8_t shift_activo(void)
{
    return shift_izquierdo || shift_derecho;
}

char teclado_leer_caracter(void)
{
    uint8_t scancode;
    uint8_t liberada;
    uint8_t shift;

    /* Esperar hasta que haya un dato disponible */
    while ((inb(0x64) & 1) == 0) {
        __asm__ volatile ("pause");
    }

    scancode = inb(0x60);

    /* Los códigos 0xE0 indican teclas extendidas */
    if (scancode == 0xE0) {
        prefijo_extendido = 1;
        return 0;
    }

    if (prefijo_extendido) {
        prefijo_extendido = 0;
        return 0;
    }

    /* El bit 7 indica liberación de la tecla */
    liberada = scancode & 0x80;
    scancode &= 0x7F;

    /* Shift izquierdo: pulsación 0x2A, liberación 0xAA */
    if (scancode == 0x2A) {
        shift_izquierdo = !liberada;
        return 0;
    }

    /* Shift derecho: pulsación 0x36, liberación 0xB6 */
    if (scancode == 0x36) {
        shift_derecho = !liberada;
        return 0;
    }

    /* Bloq Mayús: alternar solo al pulsar */
    if (scancode == 0x3A) {
        if (!liberada && !tecla_mayus_pulsada) {
            bloq_mayus = !bloq_mayus;
            tecla_mayus_pulsada = 1;
        }

        if (liberada) {
            tecla_mayus_pulsada = 0;
        }

        return 0;
    }

    /* Ignorar cualquier otra liberación */
    if (liberada) {
        return 0;
    }

    if (scancode >= 128) {
        return 0;
    }

    shift = shift_activo();

    /* Bloq Mayús invierte el efecto de Shift en las letras */
    char caracter = mapa_teclas[scancode];

    if (caracter >= 'a' && caracter <= 'z') {
        if (shift != bloq_mayus) {
            caracter = (char)(caracter - 'a' + 'A');
        }
    } else if (shift) {
        caracter = mapa_shift[scancode];
    }

    return caracter;
}
