#include "grub_modules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void grub_module_list_init(GRUBModuleList *list)
{
    memset(list, 0, sizeof(GRUBModuleList));
    list->count = 0;
}

bool grub_load_module(GRUBModuleList *list, const char *name,
                      const GRUBModuleHeader *header,
                      bool (*init)(void), void (*fini)(void))
{
    if (list->count >= GRUB_MODULE_MAX) {
        fprintf(stderr, "Module list full (%d max)\n", GRUB_MODULE_MAX);
        return false;
    }

    if (name == NULL || header == NULL) return false;

    GRUBModule *mod = &list->modules[list->count];
    strncpy(mod->name, name, GRUB_MODULE_NAME_LEN - 1);
    mod->name[GRUB_MODULE_NAME_LEN - 1] = '\0';

    memcpy(&mod->header, header, sizeof(GRUBModuleHeader));
    mod->init       = init;
    mod->fini       = fini;
    mod->flags      = MODULE_FLAGS_NONE;
    mod->load_addr  = 0;
    mod->entry_point = 0;

    list->count++;

    printf("[grub] Loaded module: %s (type=0x%02X, size=%u)\n",
           name, header->type, header->size);
    return true;
}

bool grub_dependency_resolve(GRUBModuleList *list, int *order,
                             int *order_count)
{
    if (list->count == 0) {
        *order_count = 0;
        return true;
    }

    int n = list->count;
    ModuleNode *nodes = (ModuleNode *)malloc((size_t)n * sizeof(ModuleNode));
    if (nodes == NULL) return false;

    for (int i = 0; i < n; i++) {
        strncpy(nodes[i].name, list->modules[i].name, GRUB_MODULE_NAME_LEN - 1);
        nodes[i].name[GRUB_MODULE_NAME_LEN - 1] = '\0';
        nodes[i].index    = i;
        nodes[i].visited  = false;
        nodes[i].on_stack = false;
    }

    bool result = grub_topological_sort(list, nodes, n, order, order_count);
    free(nodes);
    return result;
}

bool grub_topological_sort(GRUBModuleList *list, ModuleNode *nodes,
                           int node_count, int *order, int *order_count)
{
    (void)nodes; /* kept for API compatibility, implementation uses list */
    bool *visited = (bool *)calloc((size_t)node_count, sizeof(bool));
    int  *result  = (int *)malloc((size_t)node_count * sizeof(int));
    int   ridx    = 0;

    if (visited == NULL || result == NULL) {
        free(visited);
        free(result);
        return false;
    }

    bool built[GRUB_MODULE_MAX];
    memset(built, 0, sizeof(built));

    for (int i = 0; i < node_count; i++) {
        int count = 0;
        for (int pass = 0; pass < node_count; pass++) {
            for (int j = 0; j < node_count; j++) {
                if (built[j]) continue;

                bool deps_met = true;
                for (uint8_t k = 0; k < list->modules[j].header.dep_count; k++) {
                    uint8_t dep = list->modules[j].header.dependencies[k];
                    if (dep < (uint8_t)node_count && !built[dep]) {
                        deps_met = false;
                        break;
                    }
                }

                if (deps_met) {
                    result[ridx++] = j;
                    built[j] = true;
                    count++;
                }
            }
            if (count == 0) break;
        }
    }

    if (ridx < node_count) {
        fprintf(stderr, "Circular dependency detected\n");
        free(visited);
        free(result);
        *order_count = 0;
        return false;
    }

    memcpy(order, result, (size_t)node_count * sizeof(int));
    *order_count = node_count;

    free(visited);
    free(result);
    return true;
}

bool grub_register_fs_driver(GRUBModuleList *list, const char *name,
                             const char *fs_type)
{
    (void)name; /* FS type is used for module naming */
    GRUBModuleHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.type = MODULE_TYPE_FILESYSTEM;
    hdr.size = sizeof(GRUBModuleHeader);
    hdr.dep_count = 0;
    hdr.seg_start = 0;
    hdr.seg_size  = 0;
    hdr.seg_align = 1;

    char mod_name[GRUB_MODULE_NAME_LEN];
    snprintf(mod_name, GRUB_MODULE_NAME_LEN, "%s_fs", fs_type);

    bool ok = grub_load_module(list, mod_name, &hdr, NULL, NULL);
    if (ok) {
        printf("[grub] Registered FS driver: %s (type=%s)\n", mod_name, fs_type);
    }

    return ok;
}

const GRUBModule *grub_find_module(const GRUBModuleList *list, const char *name)
{
    for (int i = 0; i < list->count; i++) {
        if (strcmp(list->modules[i].name, name) == 0) {
            return &list->modules[i];
        }
    }
    return NULL;
}

bool grub_module_is_loaded(const GRUBModule *mod)
{
    return mod != NULL && (mod->flags & MODULE_FLAGS_LOADED) != 0;
}

void grub_module_print(const GRUBModule *mod)
{
    if (mod == NULL) return;

    const char *type_str;
    switch (mod->header.type) {
        case MODULE_TYPE_FILESYSTEM: type_str = "filesystem"; break;
        case MODULE_TYPE_DISK:       type_str = "disk";       break;
        case MODULE_TYPE_VIDEO:      type_str = "video";      break;
        case MODULE_TYPE_CRYPTO:     type_str = "crypto";     break;
        case MODULE_TYPE_TERMINAL:   type_str = "terminal";   break;
        case MODULE_TYPE_BOOT:       type_str = "boot";       break;
        case MODULE_TYPE_MMAP:       type_str = "mmap";       break;
        default:                     type_str = "unknown";    break;
    }

    printf("  %-24s type=%-10s size=%u deps=%u loaded=%s @ 0x%08X\n",
           mod->name, type_str, mod->header.size,
           mod->header.dep_count,
           grub_module_is_loaded(mod) ? "yes" : "no",
           mod->load_addr);
}

void grub_module_list_print(const GRUBModuleList *list)
{
    printf("=== GRUB Modules (%d) ===\n", list->count);
    for (int i = 0; i < list->count; i++) {
        printf("[%2d] ", i);
        grub_module_print(&list->modules[i]);
    }
}
