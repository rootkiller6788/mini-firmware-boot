#ifndef GRUB_MODULES_H
#define GRUB_MODULES_H

#include <stdbool.h>
#include <stdint.h>

#define GRUB_MODULE_MAX        32
#define GRUB_MODULE_NAME_LEN   64
#define GRUB_MODULE_DEP_MAX    8
#define GRUB_MODULE_SEG_MAX    4

#define MODULE_TYPE_FILESYSTEM  0x01
#define MODULE_TYPE_DISK        0x02
#define MODULE_TYPE_VIDEO       0x04
#define MODULE_TYPE_CRYPTO      0x08
#define MODULE_TYPE_TERMINAL    0x10
#define MODULE_TYPE_BOOT        0x20
#define MODULE_TYPE_MMAP        0x40

#define MODULE_FLAGS_NONE       0x00
#define MODULE_FLAGS_REQUIRED   0x01
#define MODULE_FLAGS_LOADED     0x02
#define MODULE_FLAGS_ERROR      0x80

typedef struct {
    uint32_t type;
    uint32_t size;
    uint8_t  dependencies[GRUB_MODULE_DEP_MAX];
    uint8_t  dep_count;
    uint32_t seg_start;
    uint32_t seg_size;
    uint32_t seg_align;
} GRUBModuleHeader;

typedef struct {
    char            name[GRUB_MODULE_NAME_LEN];
    GRUBModuleHeader header;
    uint32_t        load_addr;
    uint32_t        entry_point;
    uint32_t        flags;
    bool            (*init)(void);
    void            (*fini)(void);
} GRUBModule;

typedef struct {
    GRUBModule modules[GRUB_MODULE_MAX];
    int        count;
} GRUBModuleList;

typedef struct {
    char name[GRUB_MODULE_NAME_LEN];
    int  index;
    bool visited;
    bool on_stack;
} ModuleNode;

void       grub_module_list_init(GRUBModuleList *list);
bool       grub_load_module(GRUBModuleList *list, const char *name,
                            const GRUBModuleHeader *header,
                            bool (*init)(void), void (*fini)(void));
bool       grub_dependency_resolve(GRUBModuleList *list, int *order,
                                   int *order_count);
bool       grub_register_fs_driver(GRUBModuleList *list, const char *name,
                                   const char *fs_type);
const GRUBModule *   grub_find_module(const GRUBModuleList *list,
                                      const char *name);
bool       grub_module_is_loaded(const GRUBModule *mod);
void       grub_module_print(const GRUBModule *mod);
void       grub_module_list_print(const GRUBModuleList *list);
bool       grub_topological_sort(GRUBModuleList *list, ModuleNode *nodes,
                                 int node_count, int *order, int *order_count);

#endif
