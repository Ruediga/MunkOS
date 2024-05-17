#include "serial.h"
#include "io.h"
#include "cpu.h"
#include "compiler.h"

#include <stdint.h>
#include <stddef.h>

static bool serial_init_port(enum serial_port port)
{
    // port + 7: scratch register (test of the regis)
    outb(port + 7, 123);
    uint8_t loopback = inb(port + 7);
    if (loopback != 123) return false;

    // interrupts disable
    outb(port + 1, 0);

    // enable DLAB (divisor reg access)
    uint8_t port_3 = inb(port + 3);
    outb(port + 3, port_3 | (1 << 8));

    // divisor 1
    outb(port + 0, 1);
    outb(port + 1, 0);

    // disable DLAB
    outb(port + 3, port_3 & ~1);

    // transmitter empty interrupt type
    outb(port + 1, 1);

    return true;
}

// return success
bool init_serial(void) {
    return !(!serial_init_port(port_tty1) | !serial_init_port(port_tty2));
}

void serial_out_char(enum serial_port port, char c)
{
    // not ready to receive
    while (!(inb(port + 5) & (1 << 5))) arch_spin_hint();

    outb(port + 0, c);
}