#ifndef BOOT_ELF_H
#define BOOT_ELF_H

#include <stdbool.h>
#include <stdint.h>

/* ── ELF constants ────────────────────────────────────────────────────
 *  ELF (Executable and Linkable Format) — 可执行与可链接格式
 *  Reference: System V ABI, TIS ELF Spec 1.2
 *  L2: Executable format concept — headers, segments, sections
 *  L4: ELF standard — e_ident, program headers, relocation types
 */

#define EI_NIDENT       16
#define ELF_MAG0        0x7F
#define ELF_MAG1        'E'
#define ELF_MAG2        'L'
#define ELF_MAG3        'F'

#define ELFCLASS32      1
#define ELFCLASS64      2
#define ELFDATA2LSB     1
#define ELFDATA2MSB     2
#define EV_CURRENT      1

#define ET_NONE         0
#define ET_REL          1
#define ET_EXEC         2
#define ET_DYN          3
#define ET_CORE         4

#define PT_NULL         0
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_PHDR         6

#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_NOBITS      8
#define SHT_REL         9
#define SHT_DYNSYM      11

#define SHF_WRITE       0x01
#define SHF_ALLOC       0x02
#define SHF_EXECINSTR   0x04

#define PF_X            0x01
#define PF_W            0x02
#define PF_R            0x04

#define ELF32_R_SYM(i)    ((i) >> 8)
#define ELF32_R_TYPE(i)   ((uint8_t)(i))
#define R_386_NONE        0
#define R_386_32          1
#define R_386_PC32        2
#define R_386_GOTPC       10
#define R_386_PLT32       11

#define ELF64_R_SYM(i)    ((i) >> 32)
#define ELF64_R_TYPE(i)   ((uint32_t)(i) & 0xFFFFFFFF)
#define R_X86_64_64       1
#define R_X86_64_PC32     2
#define R_X86_64_GOTPCREL 9
#define R_X86_64_PLT32    4

/* ── ELF data structures ───────────────────────────────────────────── */
typedef struct {
    uint8_t  ei_magic[4];
    uint8_t  ei_class;
    uint8_t  ei_data;
    uint8_t  ei_version;
    uint8_t  ei_osabi;
    uint8_t  ei_abiversion;
    uint8_t  ei_pad[7];
} ElfIdent;

typedef struct {
    ElfIdent e_ident;
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} Elf32_Shdr;

typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} Elf32_Sym;

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} Elf32_Rel;

typedef struct {
    ElfIdent e_ident;
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Shdr;

typedef struct {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct {
    uint64_t r_offset;
    uint64_t r_info;
} Elf64_Rela;

/* ── Bootloader ELF context ─────────────────────────────────────────── */
typedef struct {
    uint8_t *image;
    uint32_t image_size;
    uint8_t  elf_class;
    uint8_t  elf_data;
    union {
        Elf32_Ehdr ehdr32;
        Elf64_Ehdr ehdr64;
    };
    union {
        Elf32_Phdr *phdr32;
        Elf64_Phdr *phdr64;
    };
    uint16_t phdr_count;
    uint32_t load_base;
    uint32_t entry_point;
    char    *shstrtab;
} BootElfContext;

/* ── API ────────────────────────────────────────────────────────────── */
void  boot_elf_init(BootElfContext *ctx);
bool  boot_elf_parse_header(BootElfContext *ctx);
bool  boot_elf_load_segments(BootElfContext *ctx, uint8_t *dest,
                             uint32_t dest_size);
bool  boot_elf_find_section(BootElfContext *ctx, const char *name,
                            void *shdr_out, uint32_t *offset, uint32_t *size);
bool  boot_elf_find_symbol(BootElfContext *ctx, const char *name,
                           uint64_t *value, uint64_t *size);
bool  boot_elf_apply_relocations(BootElfContext *ctx, uint8_t *base,
                                 uint32_t base_offset);
void  boot_elf_print_header(const BootElfContext *ctx);
bool  boot_elf_validate(const BootElfContext *ctx);

#endif
