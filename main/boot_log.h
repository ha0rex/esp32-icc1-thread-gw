#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Snapshot the previous boot, then keep a log that survives the next restart. */
void boot_log_init(void);

const char *boot_log_reset_name(void);
const char *boot_log_panic(void);
const char *boot_log_previous(void);

#ifdef __cplusplus
}
#endif
