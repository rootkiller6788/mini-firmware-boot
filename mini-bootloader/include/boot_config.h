#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

/*
 * L2: Boot configuration — bootloaders parse config files (grub.cfg,
 *     syslinux.cfg) to present a menu and select kernel/initrd.
 * L6: Boot script parser — tokenizer + key-value + menu entries.
 */

#define BOOTCFG_MAX_LINE     512
#define BOOTCFG_MAX_ENTRIES  32
#define BOOTCFG_MAX_KEY      64
#define BOOTCFG_MAX_VALUE    256
#define BOOTCFG_MAX_ARGS     16
#define BOOTCFG_MAX_ARG_LEN  128

/* Menu entry */
typedef struct {
    char     title[BOOTCFG_MAX_VALUE];
    char     kernel[BOOTCFG_MAX_VALUE];
    char     initrd[BOOTCFG_MAX_VALUE];
    char     args[BOOTCFG_MAX_ARGS][BOOTCFG_MAX_ARG_LEN];
    uint32_t arg_count;
    uint32_t index;
    bool     is_default;
} BootMenuEntry;

/* Key-value pair */
typedef struct {
    char key[BOOTCFG_MAX_KEY];
    char value[BOOTCFG_MAX_VALUE];
} BootConfigKV;

/* Boot configuration */
typedef struct {
    BootMenuEntry entries[BOOTCFG_MAX_ENTRIES];
    uint32_t      entry_count;
    uint32_t      default_index;
    uint32_t      timeout_seconds;
    char          theme[BOOTCFG_MAX_VALUE];
    BootConfigKV  globals[32];
    uint32_t      global_count;
} BootConfig;

/* ── API ────────────────────────────────────────────────── */
void  bootcfg_init(BootConfig *cfg);
bool  bootcfg_parse(BootConfig *cfg, const char *config_text);
bool  bootcfg_parse_file(BootConfig *cfg, const char *filepath);

/* Menu entry management */
bool  bootcfg_add_entry(BootConfig *cfg, const char *title,
                        const char *kernel, const char *initrd,
                        const char *args);
const BootMenuEntry *bootcfg_get_entry(const BootConfig *cfg, uint32_t index);
const BootMenuEntry *bootcfg_get_default(const BootConfig *cfg);

/* Configuration queries */
const char *bootcfg_get_global(const BootConfig *cfg, const char *key);
bool  bootcfg_set_global(BootConfig *cfg, const char *key, const char *value);
bool  bootcfg_set_timeout(BootConfig *cfg, uint32_t seconds);
bool  bootcfg_set_default(BootConfig *cfg, uint32_t index);

/* Tokenizer (used internally, exposed for testing) */
int   bootcfg_tokenize(const char *line, char *tokens[], int max_tokens);

/* Display */
void  bootcfg_print_menu(const BootConfig *cfg);
void  bootcfg_print_entry(const BootMenuEntry *entry);

#endif