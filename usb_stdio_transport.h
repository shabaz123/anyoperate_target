#pragma once

#include <cstring>

#include "pico/stdlib.h"

#include "config_protocol.h"

class UsbStdioTransport : public ProtocolTransport
{
public:
    int readChar() override {
        const int ch =
            getchar_timeout_us(0);

        if (ch == PICO_ERROR_TIMEOUT) {
            return -1;
        }

        return ch;
    }

    void write(const char* data, size_t length) override {
        for (size_t i = 0; i < length; ++i) {
            putchar_raw(data[i]);
        }
    }
};
