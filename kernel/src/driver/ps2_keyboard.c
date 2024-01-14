#include "driver/ps2_keyboard.h"
#include "cpu/io.h"
#include "cpu/smp.h"
#include "apic/ioapic.h"

#include <stdint.h>

uint8_t ps2_keyboard_vector = 0;

uint8_t ps2_read(void) {
    uint8_t out = 0;
    // this only reads the last byte in the buffer
    while (inb(0x64) & 1)
        out = inb(0x60);
    return out;
}

void ps2_write(uint16_t port, uint8_t value) {
    while ((inb(0x64) & 2) != 0);
    outb(port, value);
}

uint8_t ps2_read_config(void) {
    ps2_write(0x64, 0x20);
    return ps2_read();
}

void ps2_write_config(uint8_t value) {
    ps2_write(0x64, 0x60);
    ps2_write(0x60, value);
}

void ps2_init(void) {
    // Disable primary and secondary PS/2 ports
    ps2_write(0x64, 0xad);
    ps2_write(0x64, 0xa7);

    uint8_t ps2_config = ps2_read_config();
    // Enable keyboard interrupt and keyboard scancode translation
    ps2_config |= (1 << 0) | (1 << 6);
    // Enable mouse interrupt if any
    if ((ps2_config & (1 << 5)) != 0) {
        ps2_config |= (1 << 1);
    }
    ps2_write_config(ps2_config);

    // Enable keyboard port
    ps2_write(0x64, 0xae);
    // Enable mouse port if any
    if ((ps2_config & (1 << 5)) != 0) {
        ps2_write(0x64, 0xa8);
    }

    // some random ass vector
    ps2_keyboard_vector = 0x99;
    ioapic_redirect_irq(1, ps2_keyboard_vector, smp_request.response->bsp_lapic_id);
    inb(0x60);
}