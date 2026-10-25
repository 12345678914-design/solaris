
#include "ide.h"
#include "../io.h"

#define IDE_DATA       0x1F0
#define IDE_ERROR      0x1F1
#define IDE_SECCOUNT   0x1F2
#define IDE_LBA0       0x1F3
#define IDE_LBA1       0x1F4
#define IDE_LBA2       0x1F5
#define IDE_DRIVE      0x1F6
#define IDE_STATUS     0x1F7
#define IDE_COMMAND    0x1F7

#define ATA_BSY        0x80
#define ATA_DRQ        0x08
#define ATA_ERR        0x01
#define ATA_READ_EXT   0x24

#define SECTOR_SIZE    512

static int ide_esperar(void)
{
    /* Cuatro lecturas de estado para el retardo ATA. */
    inb(IDE_STATUS);
    inb(IDE_STATUS);
    inb(IDE_STATUS);
    inb(IDE_STATUS);

    for (uint32_t i = 0; i < 1000000; i++) {
        uint8_t estado = inb(IDE_STATUS);

        /* 0x00 y 0xFF indican ausencia/no disponibilidad. */
        if (estado == 0 || estado == 0xFF)
            return -1;

        if (!(estado & ATA_BSY)) {
            if (estado & ATA_ERR)
                return -2;

            if (estado & ATA_DRQ)
                return 0;
        }
    }

    return -3;
}

/* Lee exactamente un sector de 512 bytes usando LBA48. */
static int ide_leer_sector(uint64_t lba, uint8_t *buffer)
{
    if (lba >= (1ULL << 48))
        return -1;

    /*
     * Seleccionar el disco primario maestro.
     * Requiere que QEMU presente el disco en IDE index=0.
     */
    outb(IDE_DRIVE, 0x40);
    inb(IDE_STATUS);
    inb(IDE_STATUS);
    inb(IDE_STATUS);
    inb(IDE_STATUS);

    /* LBA48: primero los bytes altos. */
    outb(IDE_SECCOUNT, 0);
    outb(IDE_LBA0, (uint8_t)(lba >> 24));
    outb(IDE_LBA1, (uint8_t)(lba >> 32));
    outb(IDE_LBA2, (uint8_t)(lba >> 40));

    /* Después, los bytes bajos. */
    outb(IDE_SECCOUNT, 1);
    outb(IDE_LBA0, (uint8_t)lba);
    outb(IDE_LBA1, (uint8_t)(lba >> 8));
    outb(IDE_LBA2, (uint8_t)(lba >> 16));

    outb(IDE_COMMAND, ATA_READ_EXT);

    if (ide_esperar() != 0)
        return -2;

    for (int i = 0; i < 256; i++) {
        uint16_t palabra = inw(IDE_DATA);

        buffer[i * 2]     = (uint8_t)palabra;
        buffer[i * 2 + 1] = (uint8_t)(palabra >> 8);
    }

    return 0;
}

/* Lectura de bytes arbitrarios sobre sectores de 512 bytes. */
int ide_leer_bytes(
    uint64_t offset,
    void *buffer,
    uint32_t size
)
{
    if (buffer == 0 && size != 0)
        return -1;

    uint8_t *destino = (uint8_t *)buffer;
    uint8_t sector[SECTOR_SIZE];

    while (size > 0) {
        uint64_t lba = offset / SECTOR_SIZE;
        uint32_t dentro = (uint32_t)(offset % SECTOR_SIZE);

        if (ide_leer_sector(lba, sector) != 0)
            return -2;

        uint32_t cantidad = SECTOR_SIZE - dentro;

        if (cantidad > size)
            cantidad = size;

        for (uint32_t i = 0; i < cantidad; i++)
            destino[i] = sector[dentro + i];

        destino += cantidad;
        offset += cantidad;
        size -= cantidad;
    }

    return 0;
}
