#include "ide.h"
#include "../io.h"

#define IDE_DATA 0x1F0
#define IDE_ERROR 0x1F1
#define IDE_SECCOUNT 0x1F2
#define IDE_LBA0 0x1F3
#define IDE_LBA1 0x1F4
#define IDE_LBA2 0x1F5
#define IDE_DRIVE 0x1F6
#define IDE_STATUS 0x1F7
#define IDE_COMMAND 0x1F7

#define ATA_BSY 0x80
#define ATA_DRDY 0x40
#define ATA_DRQ 0x08
#define ATA_ERR 0x01
#define ATA_READ_EXT 0x24
#define ATA_WRITE_EXT 0x34
#define ATA_FLUSH_EXT 0xEA

#define SECTOR_SIZE 512
#define MAX_SECTORES 255

static int ide_esperar_drq(void) {
    inb(IDE_STATUS);
    inb(IDE_STATUS);
    inb(IDE_STATUS);
    inb(IDE_STATUS);

    for (uint32_t i = 0; i < 10000000u; i++) {
        uint8_t estado = inb(IDE_STATUS);

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

static int ide_esperar_fin(void) {
    for (uint32_t i = 0; i < 10000000u; i++) {
        uint8_t estado = inb(IDE_STATUS);

        if (estado == 0 || estado == 0xFF)
            return -1;
        if (!(estado & ATA_BSY)) {
            if (estado & ATA_ERR)
                return -2;
            return 0;
        }
    }
    return -3;
}

static void ide_preparar_lba48(uint64_t lba, uint8_t sectores) {
    outb(IDE_DRIVE, 0x40);
    inb(IDE_STATUS);
    inb(IDE_STATUS);
    inb(IDE_STATUS);
    inb(IDE_STATUS);

    outb(IDE_SECCOUNT, 0);
    outb(IDE_LBA0, (uint8_t)(lba >> 24));
    outb(IDE_LBA1, (uint8_t)(lba >> 32));
    outb(IDE_LBA2, (uint8_t)(lba >> 40));

    outb(IDE_SECCOUNT, sectores);
    outb(IDE_LBA0, (uint8_t)lba);
    outb(IDE_LBA1, (uint8_t)(lba >> 8));
    outb(IDE_LBA2, (uint8_t)(lba >> 16));
}

static void ide_retardo(void) {
    for (int i = 0; i < 4; i++)
        inb(0x3F6);
}

static int ide_leer_sectores(uint64_t lba, uint8_t cantidad, uint8_t *buffer) {
    if (cantidad == 0 || lba >= (1ULL << 48))
        return -1;

    for (uint32_t s = 0; s < cantidad; s++) {
        if (lba + s >= (1ULL << 48))
            return -1;

        ide_preparar_lba48(lba + s, 1);
        outb(IDE_COMMAND, ATA_READ_EXT);

        if (ide_esperar_drq() != 0)
            return -2;

        uint8_t *destino = buffer + (uint32_t)s * SECTOR_SIZE;
        for (uint32_t i = 0; i < 256; i++) {
            uint16_t palabra = inw(IDE_DATA);
            destino[i * 2] = (uint8_t)palabra;
            destino[i * 2 + 1] = (uint8_t)(palabra >> 8);
        }

        ide_retardo();
        if (ide_esperar_fin() != 0)
            return -3;
    }
    return 0;
}

static int ide_escribir_sectores(uint64_t lba, uint8_t cantidad, const uint8_t *buffer) {
    if (cantidad == 0 || lba >= (1ULL << 48))
        return -1;

    for (uint32_t s = 0; s < cantidad; s++) {
        if (lba + s >= (1ULL << 48))
            return -1;

        ide_preparar_lba48(lba + s, 1);
        outb(IDE_COMMAND, ATA_WRITE_EXT);

        if (ide_esperar_drq() != 0)
            return -2;

        const uint8_t *origen = buffer + (uint32_t)s * SECTOR_SIZE;
        for (uint32_t i = 0; i < 256; i++) {
            uint16_t palabra = ((uint16_t)origen[i * 2 + 1] << 8) | (uint16_t)origen[i * 2];
            outw(IDE_DATA, palabra);
        }

        ide_retardo();
        if (ide_esperar_fin() != 0)
            return -3;
    }
    return 0;
}

static int ide_volcar(void) {
    outb(IDE_COMMAND, ATA_FLUSH_EXT);
    return ide_esperar_fin();
}

int ide_leer_bytes(uint64_t offset, void *buffer, uint32_t size) {
    if (buffer == 0 && size != 0)
        return -1;

    uint8_t *destino = (uint8_t *)buffer;
    uint8_t sector[SECTOR_SIZE];

    while (size > 0) {
        uint64_t lba = offset / SECTOR_SIZE;
        uint32_t dentro = (uint32_t)(offset % SECTOR_SIZE);

        if (dentro == 0 && size >= SECTOR_SIZE) {
            uint32_t sectores = size / SECTOR_SIZE;
            if (sectores > MAX_SECTORES)
                sectores = MAX_SECTORES;

            if (ide_leer_sectores(lba, (uint8_t)sectores, destino) != 0)
                return -2;

            uint32_t bytes = sectores * SECTOR_SIZE;
            destino += bytes;
            offset += bytes;
            size -= bytes;
        } else {
            if (ide_leer_sectores(lba, 1, sector) != 0)
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
    }
    return 0;
}

int ide_escribir_bytes(uint64_t offset, const void *buffer, uint32_t size) {
    if (buffer == 0 && size != 0)
        return -1;

    const uint8_t *origen = (const uint8_t *)buffer;
    uint8_t sector[SECTOR_SIZE];
    int hubo_escritura = 0;

    while (size > 0) {
        uint64_t lba = offset / SECTOR_SIZE;
        uint32_t dentro = (uint32_t)(offset % SECTOR_SIZE);

        if (dentro == 0 && size >= SECTOR_SIZE) {
            uint32_t sectores = size / SECTOR_SIZE;
            if (sectores > MAX_SECTORES)
                sectores = MAX_SECTORES;

            if (ide_escribir_sectores(lba, (uint8_t)sectores, origen) != 0)
                return -2;

            hubo_escritura = 1;
            uint32_t bytes = sectores * SECTOR_SIZE;
            origen += bytes;
            offset += bytes;
            size -= bytes;
        } else {
            if (ide_leer_sectores(lba, 1, sector) != 0)
                return -3;

            uint32_t cantidad = SECTOR_SIZE - dentro;
            if (cantidad > size)
                cantidad = size;

            for (uint32_t i = 0; i < cantidad; i++)
                sector[dentro + i] = origen[i];

            if (ide_escribir_sectores(lba, 1, sector) != 0)
                return -4;

            hubo_escritura = 1;
            origen += cantidad;
            offset += cantidad;
            size -= cantidad;
        }
    }

    if (hubo_escritura && ide_volcar() != 0)
        return -5;

    return 0;
}
