#pragma once

#include <stddef.h>
#include <stdint.h>

enum {
    MATTER_SNIFF_RX = 0,
    MATTER_SNIFF_TX = 1,
    MATTER_SNIFF_EVT = 2,
};

struct matter_sniff_entry_t {
    int64_t ms;
    uint8_t dir;
    uint16_t endpoint;
    uint32_t cluster;
    uint32_t attr;
    uint64_t node;
    char text[40];
};

void matter_sniff_push(uint8_t dir, uint64_t node, uint32_t cluster, uint16_t endpoint,
                       uint32_t attr, const char *text);
uint32_t matter_sniff_seq(void);
void matter_sniff_clear(void);
size_t matter_sniff_copy(matter_sniff_entry_t *out, size_t max);
