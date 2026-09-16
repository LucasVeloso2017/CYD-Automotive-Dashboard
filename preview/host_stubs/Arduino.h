// Stub minimo para compilar elm327_gateway.cpp no PC (preview/test_dtc.cpp).
#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
unsigned long millis(void);
void delay(unsigned long ms);
struct SerialStub {
    int printf(const char *fmt, ...) { va_list a; va_start(a, fmt); int n = vprintf(fmt, a); va_end(a); return n; }
    void println(const char *s) { puts(s); }
};
extern SerialStub Serial;
