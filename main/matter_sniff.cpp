#include "matter_sniff.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

namespace {

constexpr size_t kLog = 64;

matter_sniff_entry_t s_log[kLog];
uint8_t s_head;
uint8_t s_count;
uint32_t s_seq;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

} // namespace

void matter_sniff_push(uint8_t dir, uint64_t node, uint32_t cluster, uint16_t endpoint,
                       uint32_t attr, const char *text)
{
    matter_sniff_entry_t entry = {};
    entry.ms = esp_timer_get_time() / 1000;
    entry.dir = dir;
    entry.endpoint = endpoint;
    entry.cluster = cluster;
    entry.attr = attr;
    entry.node = node;
    snprintf(entry.text, sizeof(entry.text), "%s", text ? text : "");

    portENTER_CRITICAL(&s_mux);
    s_log[s_head] = entry;
    s_head = static_cast<uint8_t>((s_head + 1) % kLog);
    if (s_count < kLog) {
        s_count++;
    }
    s_seq++;
    portEXIT_CRITICAL(&s_mux);
}

uint32_t matter_sniff_seq(void)
{
    return s_seq;
}

void matter_sniff_clear(void)
{
    portENTER_CRITICAL(&s_mux);
    s_head = 0;
    s_count = 0;
    s_seq++;
    portEXIT_CRITICAL(&s_mux);
}

size_t matter_sniff_copy(matter_sniff_entry_t *out, size_t max)
{
    if (!out || max == 0) {
        return 0;
    }
    portENTER_CRITICAL(&s_mux);
    size_t count = s_count;
    if (count > max) {
        count = max;
    }
    size_t start = (s_head + kLog - s_count) % kLog;
    if (s_count > count) {
        start = (s_head + kLog - count) % kLog;
    }
    for (size_t i = 0; i < count; ++i) {
        out[i] = s_log[(start + i) % kLog];
    }
    portEXIT_CRITICAL(&s_mux);
    return count;
}
