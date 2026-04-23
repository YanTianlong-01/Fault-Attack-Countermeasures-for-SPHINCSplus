#include "hal.h"
#include "simpleserial.h"

void send_hex_value(uint32_t value) {
    putch('0');
    putch('x');

    int hex_digits = 0;
    uint32_t temp = value;
    do {
        hex_digits++;
        temp >>= 4;
    } while (temp > 0);

    for (int i = (hex_digits - 1) * 4; i >= 0; i -= 4) {
        uint8_t nibble = (value >> i) & 0xF;
        char c = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
        putch(c);
    }

    // putch('\n');
}

void print_unsigned_dec(unsigned int num) {
    if (num == 0) {
        putch('0');
        return;
    }

    char buffer[32];
    int index = 0;

    while (num > 0) {
        buffer[index++] = '0' + (num % 10);
        num /= 10;
    }

    for (int i = index - 1; i >= 0; i--) {
        putch(buffer[i]);
    }
}

void print_uint64_hex(uint64_t value) {
    putch('0');
    putch('x');

    for (int i = 60; i >= 0; i -= 4) {
        uint8_t nibble = (value >> i) & 0xF;
        char c = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
        putch(c);
    }
}