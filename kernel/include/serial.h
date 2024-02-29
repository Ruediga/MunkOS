#pragma once

#include <stdbool.h>

enum serial_port {
    port_tty1 = 0x3F8,   // com1
    port_tty2 = 0x2F8    // com2
};

bool init_serial(void);
void serial_out_char(enum serial_port port, char c);