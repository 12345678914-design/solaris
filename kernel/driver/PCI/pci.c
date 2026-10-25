#include <stdint.h>
#include "../io.h"
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