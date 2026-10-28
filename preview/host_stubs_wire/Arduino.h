// Stub para compilar src/net/link.cpp no PC (preview/test_wire.cpp): uma
// HardwareSerial que grava o que o CYD envia e entrega o que o teste injeta.
#pragma once
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define SERIAL_8N1 0x800001c

unsigned long millis(void);

struct HardwareSerial {
    std::string tx, rx;
    size_t rx_pos = 0;
    bool quiet = false;
    void setTxBufferSize(size_t) {}
    void setRxBufferSize(size_t) {}
    void begin(unsigned long, uint32_t = 0, int = -1, int = -1) {}
    size_t write(const uint8_t *b, size_t n) { tx.append((const char *)b, n); return n; }
    size_t print(const char *s) { tx += s; return strlen(s); }
    int available() { return (int)(rx.size() - rx_pos); }
    int read() { return rx_pos < rx.size() ? (uint8_t)rx[rx_pos++] : -1; }
    int printf(const char *fmt, ...) {
        if (quiet) return 0;
        va_list a; va_start(a, fmt); int n = vprintf(fmt, a); va_end(a); return n;
    }
    void println(const char *s) { if (!quiet) puts(s); }
};

extern HardwareSerial Serial;
extern HardwareSerial Serial2;
