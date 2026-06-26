#include "boot_config.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/*
 * L2: Boot configuration - bootloaders parse config files to present
 *     a menu and select kernel/initrd/args for boot.
 * L6: Boot script parser - tokenizer + key-value + menu block parser.
 *     Covers GRUB-style and syslinux-style config syntax.
 */

void bootcfg_init(BootConfig *cfg)
{
    memset(cfg, 0, sizeof(BootConfig));
    cfg->timeout_seconds = 10;
    cfg->default_index   = 0;
}

/*
 * L5: Tokenizer - splits a line into whitespace-separated tokens.
 * Supports double/single-quoted strings with backslash escapes.
 * Used internally and exposed for testing.
 * Complexity: O(n) in line length.
 */
int bootcfg_tokenize(const char *line, char *tokens[], int max_tokens)
{
    if (line == NULL || tokens == NULL || max_tokens <= 0) return 0;

    int count = 0;
    const char *p = line;

    while (*p && count < max_tokens) {
        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        char *out = tokens[count];
        int out_len = 0;

        if (*p == '"' || *p == '\'') {
            /* Quoted string with backslash escapes */
            char quote = *p++;
            while (*p && *p != quote && out_len < BOOTCFG_MAX_ARG_LEN - 1) {
                if (*p == '\\' && *(p + 1)) {
                    p++;
                    switch (*p) {
                        case 'n':  out[out_len++] = '\n'; break;
                        case 't':  out[out_len++] = '\t'; break;
                        default:   out[out_len++] = *p;   break;
                    }
                } else {
                    out[out_len++] = *p;
                }
                p++;
            }
            if (*p == quote) p++;  /* skip closing quote */
        } else {
            /* Unquoted token */
            while (*p && !isspace((unsigned char)*p)
                   && out_len < BOOTCFG_MAX_ARG_LEN - 1) {
                out[out_len++] = *p++;
            }
        }
        out[out_len] = '\0';
        if (out_len > 0) count++;
    }
    return count;
}

/*
 * L6: Config file parser (GRUB-style).
 * State machine: GLOBAL -> MENU_ENTRY -> GLOBAL.
 *
 * Supported syntax:
 *   menuentry "Title" { ... }
 *   linux /vmlinuz root=/dev/sda1 ro
 *   initrd /initrd.img
 *   set KEY=VALUE
 *   timeout=N
 *   default=N
 *   # comment
 */
bool bootcfg_parse(BootConfig *cfg, const char *config_text)
{
    if (cfg == NULL || config_text == NULL) return false;

    /* Make a mutable copy for strtok_r */
    char *buf = strdup(config_text);
    if (buf == NULL) return false;

    char *saveptr;
    char *line = strtok_r(buf, "\n", &saveptr);
    bool in_menuentry = false;
    BootMenuEntry *current_entry = NULL;

    while (line != NULL) {
        /* Trim leading whitespace */
        while (*line && isspace((unsigned char)*line)) line++;

        /* Skip empty lines and comments */
        if (*line == '\0' || *line == '#') {
            line = strtok_r(NULL, "\n", &saveptr);
            continue;
        }

        /* Trim trailing whitespace */
        char *end = line + strlen(line) - 1;
        while (end > line && isspace((unsigned char)*end)) *end-- = '\0';

        if (in_menuentry) {
            /* Inside a menuentry { ... } block */
            if (strcmp(line, "}") == 0) {
                in_menuentry = false;
                current_entry = NULL;
            } else if (current_entry != NULL) {
                char *tokens[BOOTCFG_MAX_ARGS];
                char tok_bufs[BOOTCFG_MAX_ARGS][BOOTCFG_MAX_ARG_LEN];
                int i;
                for (i = 0; i < BOOTCFG_MAX_ARGS; i++) tokens[i] = tok_bufs[i];

                int nt = bootcfg_tokenize(line, tokens, BOOTCFG_MAX_ARGS);
                if (nt >= 2) {
                    if (strcmp(tokens[0], "linux") == 0 ||
                        strcmp(tokens[0], "kernel") == 0) {
                        strncpy(current_entry->kernel, tokens[1],
                                BOOTCFG_MAX_VALUE - 1);
                        current_entry->arg_count = 0;
                        int j;
                        for (j = 2; j < nt && j < BOOTCFG_MAX_ARGS; j++) {
                            strncpy(current_entry->args[current_entry->arg_count],
                                    tokens[j], BOOTCFG_MAX_ARG_LEN - 1);
                            current_entry->arg_count++;
                        }
                    } else if (strcmp(tokens[0], "initrd") == 0) {
                        strncpy(current_entry->initrd, tokens[1],
                                BOOTCFG_MAX_VALUE - 1);
                    }
                }
            }
        } else {
            /* Global context: look for menuentry / set / timeout / default */
            if (strncmp(line, "menuentry", 9) == 0) {
                if (cfg->entry_count >= BOOTCFG_MAX_ENTRIES) {
                    fprintf(stderr, "[cfg] Max menu entries (%d) reached\n",
                            BOOTCFG_MAX_ENTRIES);
                    line = strtok_r(NULL, "\n", &saveptr);
                    continue;
                }
                /* Extract title from menuentry "Title" */
                char *q1 = strchr(line, '"');
                char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
                if (q1 && q2 && q2 - q1 > 1) {
                    current_entry = &cfg->entries[cfg->entry_count++];
                    current_entry->index = cfg->entry_count - 1;
                    memcpy(current_entry->title, q1 + 1,
                           (size_t)(q2 - q1 - 1));
                    current_entry->title[q2 - q1 - 1] = '\0';
                    in_menuentry = true;
                }
            } else if (strncmp(line, "set ", 4) == 0) {
                char *eq = strchr(line + 4, '=');
                if (eq) {
                    *eq = '\0';
                    bootcfg_set_global(cfg, line + 4, eq + 1);
                    *eq = '=';
                }
            } else if (strncmp(line, "timeout=", 8) == 0) {
                cfg->timeout_seconds = (uint32_t)atoi(line + 8);
            } else if (strncmp(line, "default=", 8) == 0) {
                cfg->default_index = (uint32_t)atoi(line + 8);
            }
        }

        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(buf);
    printf("[cfg] Parsed %u entries, timeout=%u, default=%u\n",
           cfg->entry_count, cfg->timeout_seconds, cfg->default_index);
    return true;
}

/* Parse boot config from a file path */
bool bootcfg_parse_file(BootConfig *cfg, const char *filepath)
{
    if (cfg == NULL || filepath == NULL) return false;

    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        fprintf(stderr, "[cfg] Cannot open config file: %s\n", filepath);
        return false;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    rewind(f);
    if (fsize <= 0) { fclose(f); return false; }

    char *buf = (char *)malloc((size_t)fsize + 1);
    if (buf == NULL) { fclose(f); return false; }

    size_t rd = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[rd] = '\0';

    bool ok = bootcfg_parse(cfg, buf);
    free(buf);
    return ok;
}

/*
 * Menu entry management
 */

bool bootcfg_add_entry(BootConfig *cfg, const char *title,
                       const char *kernel, const char *initrd,
                       const char *args)
{
    if (cfg == NULL || title == NULL) return false;
    if (cfg->entry_count >= BOOTCFG_MAX_ENTRIES) return false;

    BootMenuEntry *e = &cfg->entries[cfg->entry_count++];
    e->index = cfg->entry_count - 1;
    snprintf(e->title, BOOTCFG_MAX_VALUE, "%s", title);
    if (kernel) snprintf(e->kernel, BOOTCFG_MAX_VALUE, "%s", kernel);
    if (initrd) snprintf(e->initrd, BOOTCFG_MAX_VALUE, "%s", initrd);

    /* Parse args string into individual argument tokens */
    if (args && args[0]) {
        e->arg_count = 0;
        const char *p = args;
        while (*p && e->arg_count < BOOTCFG_MAX_ARGS) {
            while (*p && isspace((unsigned char)*p)) p++;
            if (!*p) break;
            const char *start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            size_t len = (size_t)(p - start);
            if (len >= BOOTCFG_MAX_ARG_LEN) len = BOOTCFG_MAX_ARG_LEN - 1;
            memcpy(e->args[e->arg_count], start, len);
            e->args[e->arg_count][len] = '\0';
            e->arg_count++;
        }
    }

    return true;
}

const BootMenuEntry *bootcfg_get_entry(const BootConfig *cfg, uint32_t index)
{
    if (cfg == NULL || index >= cfg->entry_count) return NULL;
    return &cfg->entries[index];
}

const BootMenuEntry *bootcfg_get_default(const BootConfig *cfg)
{
    if (cfg == NULL) return NULL;
    if (cfg->default_index < cfg->entry_count)
        return &cfg->entries[cfg->default_index];
    if (cfg->entry_count > 0)
        return &cfg->entries[0];  /* Fallback to first entry */
    return NULL;
}

/*
 * Global configuration queries
 */

const char *bootcfg_get_global(const BootConfig *cfg, const char *key)
{
    if (cfg == NULL || key == NULL) return NULL;
    uint32_t i;
    for (i = 0; i < cfg->global_count; i++) {
        if (strcmp(cfg->globals[i].key, key) == 0)
            return cfg->globals[i].value;
    }
    return NULL;
}

bool bootcfg_set_global(BootConfig *cfg, const char *key, const char *value)
{
    if (cfg == NULL || key == NULL || value == NULL) return false;

    /* Update existing key if present */
    uint32_t i;
    for (i = 0; i < cfg->global_count; i++) {
        if (strcmp(cfg->globals[i].key, key) == 0) {
            snprintf(cfg->globals[i].value, BOOTCFG_MAX_VALUE, "%s", value);
            return true;
        }
    }

    /* Add new key-value pair */
    if (cfg->global_count >= 32) return false;
    snprintf(cfg->globals[cfg->global_count].key, BOOTCFG_MAX_KEY, "%s", key);
    snprintf(cfg->globals[cfg->global_count].value, BOOTCFG_MAX_VALUE, "%s", value);
    cfg->global_count++;
    return true;
}

bool bootcfg_set_timeout(BootConfig *cfg, uint32_t seconds)
{
    if (cfg == NULL) return false;
    cfg->timeout_seconds = seconds;
    return true;
}

bool bootcfg_set_default(BootConfig *cfg, uint32_t index)
{
    if (cfg == NULL || index >= BOOTCFG_MAX_ENTRIES) return false;
    cfg->default_index = index;
    return true;
}

/*
 * Display functions
 */

void bootcfg_print_menu(const BootConfig *cfg)
{
    if (cfg == NULL) return;

    printf("\n=== GRUB Boot Menu (%u entries) ===\n", cfg->entry_count);
    printf("Timeout: %u seconds, Default entry: %u\n",
           cfg->timeout_seconds, cfg->default_index);

    uint32_t i;
    for (i = 0; i < cfg->entry_count; i++) {
        const BootMenuEntry *e = &cfg->entries[i];
        printf("%c %-32s kernel=%-20s initrd=%s\n",
               i == cfg->default_index ? '*' : ' ',
               e->title, e->kernel,
               e->initrd[0] ? e->initrd : "(none)");
        if (e->arg_count > 0) {
            printf("    Args:");
            uint32_t j;
            for (j = 0; j < e->arg_count; j++)
                printf(" %s", e->args[j]);
            printf("\n");
        }
    }
}

void bootcfg_print_entry(const BootMenuEntry *entry)
{
    if (entry == NULL) return;

    printf("Entry #%u: '%s'\n", entry->index, entry->title);
    printf("  Kernel: %s\n", entry->kernel[0] ? entry->kernel : "(none)");
    printf("  Initrd: %s\n", entry->initrd[0] ? entry->initrd : "(none)");
    printf("  Kernel args (%u):", entry->arg_count);
    uint32_t i;
    for (i = 0; i < entry->arg_count; i++)
        printf(" %s", entry->args[i]);
    printf("\n");
}
