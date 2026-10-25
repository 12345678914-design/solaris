
#ifndef IDE_H
#define IDE_H

#include <stdint.h>

int ide_leer_bytes(
    uint64_t offset,
    void *buffer,
    uint32_t size
);

#endif
