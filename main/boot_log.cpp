#include "boot_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_attr.h"
#include "esp_core_dump.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "nvs.h"
#include "sdkconfig.h"

namespace {

const char *TAG = "boot";
constexpr uint32_t kMagic = 0xB00710C6u;
constexpr size_t kLogBytes = 2816;

struct RtcLog {
    uint32_t magic;
    uint32_t len;
    char text[kLogBytes];
};

RTC_NOINIT_ATTR RtcLog s_rtc;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

char s_previous[1800];
char s_panic[700];
char s_reset[40];
vprintf_like_t s_original_log;

void ring_append(const char *text, int length)
{
    if (!text || length <= 0) {
        return;
    }
    if (length > 180) {
        length = 180;
    }
    portENTER_CRITICAL(&s_mux);
    if (s_rtc.len + static_cast<uint32_t>(length) >= sizeof(s_rtc.text)) {
        uint32_t drop = 1024;
        if (drop > s_rtc.len) {
            drop = s_rtc.len;
        }
        memmove(s_rtc.text, s_rtc.text + drop, s_rtc.len - drop);
        s_rtc.len -= drop;
    }
    memcpy(s_rtc.text + s_rtc.len, text, static_cast<size_t>(length));
    s_rtc.len += static_cast<uint32_t>(length);
    s_rtc.text[s_rtc.len] = 0;
    portEXIT_CRITICAL(&s_mux);
}

int boot_vprintf(const char *fmt, va_list args)
{
    if (xPortInIsrContext()) {
        return 0;
    }
    char line[192];
    va_list copy;
    va_copy(copy, args);
    int written = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (written <= 0) {
        return 0;
    }
    int keep = written < static_cast<int>(sizeof(line)) - 1
                   ? written
                   : static_cast<int>(sizeof(line)) - 1;
    ring_append(line, keep);
    // The console driver waits forever when the USB host is not reading.
    // That stall sat in the OpenThread task and the RCP then gave up.
    if (usb_serial_jtag_is_driver_installed()) {
        char usb[220];
        int out = 0;
        for (int i = 0; i < keep && out + 2 < static_cast<int>(sizeof(usb)); ++i) {
            if (line[i] == '\n') {
                usb[out++] = '\r';
            }
            usb[out++] = line[i];
        }
        usb_serial_jtag_write_bytes(usb, static_cast<size_t>(out), 0);
    } else if (s_original_log) {
        s_original_log(fmt, args);
    }
    return written;
}

const char *cause_name(uint32_t cause)
{
    switch (cause) {
    case 0:
        return "Illegal instruction";
    case 2:
        return "Instruction fetch fault";
    case 3:
        return "Load/store fault";
    case 9:
        return "Unaligned access";
    case 28:
        return "Load prohibited";
    case 29:
        return "Store prohibited";
    default:
        return "Exception";
    }
}

bool crash_reset(esp_reset_reason_t reason)
{
    return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
           reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
           reason == ESP_RST_BROWNOUT || reason == ESP_RST_CPU_LOCKUP ||
           reason == ESP_RST_PWR_GLITCH;
}

void save_panic(const char *text)
{
    nvs_handle_t handle;
    if (nvs_open("diag", NVS_READWRITE, &handle) != ESP_OK) {
        return;
    }
    nvs_set_str(handle, "panic", text);
    nvs_commit(handle);
    nvs_close(handle);
}

void load_panic(void)
{
    nvs_handle_t handle;
    s_panic[0] = 0;
    if (nvs_open("diag", NVS_READONLY, &handle) != ESP_OK) {
        snprintf(s_panic, sizeof(s_panic), "none");
        return;
    }
    size_t length = sizeof(s_panic);
    if (nvs_get_str(handle, "panic", s_panic, &length) != ESP_OK || !s_panic[0]) {
        snprintf(s_panic, sizeof(s_panic), "none");
    }
    nvs_close(handle);
}

void read_coredump(esp_reset_reason_t reason)
{
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
    if (esp_core_dump_image_check() != ESP_OK) {
        if (crash_reset(reason)) {
            snprintf(s_panic, sizeof(s_panic),
                     "%s, no coredump stored. See the log below.", s_reset);
            save_panic(s_panic);
        } else {
            load_panic();
        }
        return;
    }

    char reason_text[240];
    reason_text[0] = 0;
    esp_core_dump_get_panic_reason(reason_text, sizeof(reason_text));

    static esp_core_dump_summary_t summary;
    memset(&summary, 0, sizeof(summary));
    bool have_summary = esp_core_dump_get_summary(&summary) == ESP_OK;
    char backtrace[180];
    backtrace[0] = 0;
    if (have_summary) {
        size_t used = 0;
        uint32_t depth = summary.exc_bt_info.depth;
        if (depth > 8) {
            depth = 8;
        }
        for (uint32_t i = 0; i < depth && used + 12 < sizeof(backtrace); ++i) {
            used += static_cast<size_t>(snprintf(
                backtrace + used, sizeof(backtrace) - used, "0x%08lx ",
                static_cast<unsigned long>(summary.exc_bt_info.bt[i])));
        }
        char task[17];
        memcpy(task, summary.exc_task, 16);
        task[16] = 0;
        snprintf(s_panic, sizeof(s_panic),
                 "task %s  pc 0x%08lx  %s (%lu)  vaddr 0x%08lx  %s  bt %s",
                 task[0] ? task : "?",
                 static_cast<unsigned long>(summary.exc_pc),
                 cause_name(summary.ex_info.exc_cause),
                 static_cast<unsigned long>(summary.ex_info.exc_cause),
                 static_cast<unsigned long>(summary.ex_info.exc_vaddr),
                 reason_text[0] ? reason_text : s_reset, backtrace);
    } else {
        snprintf(s_panic, sizeof(s_panic), "%s  %s", s_reset,
                 reason_text[0] ? reason_text : "coredump present");
    }
    save_panic(s_panic);
    esp_core_dump_image_erase();
#else
    if (crash_reset(reason)) {
        snprintf(s_panic, sizeof(s_panic), "%s", s_reset);
        save_panic(s_panic);
    } else {
        load_panic();
    }
#endif
}

} // namespace

extern "C" void boot_log_init(void)
{
    s_previous[0] = 0;
    if (s_rtc.magic == kMagic && s_rtc.len < sizeof(s_rtc.text)) {
        const char *source = s_rtc.text;
        size_t length = s_rtc.len;
        if (length >= sizeof(s_previous)) {
            source += length - (sizeof(s_previous) - 1);
            length = sizeof(s_previous) - 1;
        }
        memcpy(s_previous, source, length);
        s_previous[length] = 0;
    }

    s_rtc.magic = kMagic;
    s_rtc.len = 0;
    s_rtc.text[0] = 0;
    s_original_log = esp_log_set_vprintf(boot_vprintf);

    esp_reset_reason_t reason = esp_reset_reason();
    switch (reason) {
    case ESP_RST_POWERON:
        snprintf(s_reset, sizeof(s_reset), "Power-on");
        break;
    case ESP_RST_EXT:
        snprintf(s_reset, sizeof(s_reset), "External pin");
        break;
    case ESP_RST_SW:
        snprintf(s_reset, sizeof(s_reset), "Software restart");
        break;
    case ESP_RST_PANIC:
        snprintf(s_reset, sizeof(s_reset), "Panic");
        break;
    case ESP_RST_INT_WDT:
        snprintf(s_reset, sizeof(s_reset), "Interrupt watchdog");
        break;
    case ESP_RST_TASK_WDT:
        snprintf(s_reset, sizeof(s_reset), "Task watchdog");
        break;
    case ESP_RST_WDT:
        snprintf(s_reset, sizeof(s_reset), "Watchdog");
        break;
    case ESP_RST_DEEPSLEEP:
        snprintf(s_reset, sizeof(s_reset), "Deep sleep");
        break;
    case ESP_RST_BROWNOUT:
        snprintf(s_reset, sizeof(s_reset), "Brownout");
        break;
    case ESP_RST_SDIO:
        snprintf(s_reset, sizeof(s_reset), "SDIO");
        break;
    case ESP_RST_USB:
        snprintf(s_reset, sizeof(s_reset), "USB reset");
        break;
    case ESP_RST_JTAG:
        snprintf(s_reset, sizeof(s_reset), "JTAG reset");
        break;
    case ESP_RST_EFUSE:
        snprintf(s_reset, sizeof(s_reset), "eFuse error");
        break;
    case ESP_RST_PWR_GLITCH:
        snprintf(s_reset, sizeof(s_reset), "Power glitch");
        break;
    case ESP_RST_CPU_LOCKUP:
        snprintf(s_reset, sizeof(s_reset), "CPU lockup");
        break;
    default:
        snprintf(s_reset, sizeof(s_reset), "Unknown");
        break;
    }

    char marker[64];
    snprintf(marker, sizeof(marker), "\n--- boot: %s ---\n", s_reset);
    ring_append(marker, static_cast<int>(strlen(marker)));
    read_coredump(reason);
    ESP_LOGW(TAG, "This boot: %s", s_reset);
    if (strcmp(s_panic, "none") != 0) {
        ESP_LOGW(TAG, "Last panic: %s", s_panic);
    }
}

extern "C" const char *boot_log_reset_name(void)
{
    return s_reset[0] ? s_reset : "Unknown";
}

extern "C" const char *boot_log_panic(void)
{
    return s_panic[0] ? s_panic : "none";
}

extern "C" const char *boot_log_previous(void)
{
    size_t length = strlen(s_previous);
    if (length > 900) {
        return s_previous + (length - 900);
    }
    return s_previous;
}
