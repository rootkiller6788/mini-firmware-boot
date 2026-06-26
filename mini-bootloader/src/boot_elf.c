#include "boot_elf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void boot_elf_init(BootElfContext *ctx)
{
    memset(ctx, 0, sizeof(BootElfContext));
}

bool boot_elf_parse_header(BootElfContext *ctx)
{
    if (ctx == NULL || ctx->image == NULL || ctx->image_size < 52) {
        fprintf(stderr, "[elf] Invalid context or image too small\n");
        return false;
    }

    uint8_t *p = ctx->image;

    if (p[0] != ELF_MAG0 || p[1] != ELF_MAG1 ||
        p[2] != ELF_MAG2 || p[3] != ELF_MAG3) {
        fprintf(stderr, "[elf] Bad ELF magic: %02X %02X %02X %02X\n",
                p[0], p[1], p[2], p[3]);
        return false;
    }

    ctx->elf_class = p[4];
    ctx->elf_data  = p[5];

    if (ctx->elf_class != ELFCLASS32 && ctx->elf_class != ELFCLASS64) {
        fprintf(stderr, "[elf] Unsupported ELF class: %u\n", ctx->elf_class);
        return false;
    }
    if (ctx->elf_data != ELFDATA2LSB) {
        fprintf(stderr, "[elf] Only LE ELF supported (data=%u)\n", ctx->elf_data);
        return false;
    }

    if (ctx->elf_class == ELFCLASS32) {
        if (ctx->image_size < sizeof(Elf32_Ehdr)) return false;
        memcpy(&ctx->ehdr32, p, sizeof(Elf32_Ehdr));
    } else {
        if (ctx->image_size < sizeof(Elf64_Ehdr)) return false;
        memcpy(&ctx->ehdr64, p, sizeof(Elf64_Ehdr));
    }

    printf("[elf] ELF%u header: type=%u machine=%u entry=0x%X\n",
           ctx->elf_class == ELFCLASS32 ? 32 : 64,
           ctx->elf_class == ELFCLASS32 ? ctx->ehdr32.e_type : ctx->ehdr64.e_type,
           ctx->elf_class == ELFCLASS32 ? ctx->ehdr32.e_machine : ctx->ehdr64.e_machine,
           ctx->elf_class == ELFCLASS32 ? ctx->ehdr32.e_entry : (uint32_t)ctx->ehdr64.e_entry);

    return true;
}

bool boot_elf_load_segments(BootElfContext *ctx, uint8_t *dest,
                            uint32_t dest_size)
{
    if (ctx == NULL || dest == NULL) return false;

    uint16_t phnum;
    uint32_t phoff, phentsize;

    if (ctx->elf_class == ELFCLASS32) {
        phnum = ctx->ehdr32.e_phnum;
        phoff = ctx->ehdr32.e_phoff;
        phentsize = ctx->ehdr32.e_phentsize;
    } else {
        phnum = ctx->ehdr64.e_phnum;
        phoff = (uint32_t)ctx->ehdr64.e_phoff;
        phentsize = ctx->ehdr64.e_phentsize;
    }

    if (phnum == 0) {
        printf("[elf] No program headers (static object?)\n");
        return true;
    }

    printf("[elf] Loading %u program header(s)...\n", phnum);
    uint32_t load_count = 0;

    for (uint16_t i = 0; i < phnum; i++) {
        uint32_t p_type, p_offset, p_vaddr, p_filesz, p_memsz, p_flags;

        if (ctx->elf_class == ELFCLASS32) {
            Elf32_Phdr phdr;
            uint32_t hdr_off = phoff + (uint32_t)i * phentsize;
            if (hdr_off + sizeof(Elf32_Phdr) > ctx->image_size) break;
            memcpy(&phdr, ctx->image + hdr_off, sizeof(Elf32_Phdr));
            p_type = phdr.p_type; p_offset = phdr.p_offset;
            p_vaddr = phdr.p_vaddr; p_filesz = phdr.p_filesz;
            p_memsz = phdr.p_memsz; p_flags = phdr.p_flags;
        } else {
            Elf64_Phdr phdr;
            uint64_t hdr_off64 = phoff + (uint64_t)i * phentsize;
            if (hdr_off64 + sizeof(Elf64_Phdr) > ctx->image_size) break;
            memcpy(&phdr, ctx->image + hdr_off64, sizeof(Elf64_Phdr));
            p_type = phdr.p_type; p_offset = (uint32_t)phdr.p_offset;
            p_vaddr = (uint32_t)phdr.p_vaddr; p_filesz = (uint32_t)phdr.p_filesz;
            p_memsz = (uint32_t)phdr.p_memsz; p_flags = phdr.p_flags;
        }

        if (p_type != PT_LOAD) continue;

        const char *perm = "";
        if ((p_flags & PF_X)) perm = "RX";
        else if ((p_flags & PF_W)) perm = "RW";
        else if ((p_flags & PF_R)) perm = "R";

        printf("[elf]   PT_LOAD[%u]: vaddr=0x%08X filesz=%u memsz=%u %s\n",
               i, p_vaddr, p_filesz, p_memsz, perm);

        if (p_filesz > 0 && p_offset < ctx->image_size) {
            uint32_t copy_sz = p_filesz;
            if (p_offset + copy_sz > ctx->image_size)
                copy_sz = ctx->image_size - p_offset;

            uint32_t dest_off = p_vaddr;
            if (ctx->load_base != 0) dest_off += ctx->load_base;
            if (dest_off + copy_sz <= dest_size) {
                memcpy(dest + dest_off, ctx->image + p_offset, copy_sz);
            }
        }

        if (p_memsz > p_filesz) {
            uint32_t bss_off = p_vaddr + p_filesz;
            uint32_t bss_sz  = p_memsz - p_filesz;
            if (ctx->load_base != 0) bss_off += ctx->load_base;
            if (bss_off + bss_sz <= dest_size) {
                memset(dest + bss_off, 0, bss_sz);
            }
            printf("[elf]   BSS zero-filled: %u bytes\n", bss_sz);
        }

        load_count++;
    }

    printf("[elf] Loaded %u segment(s)\n", load_count);
    return load_count > 0;
}

static bool elf_get_section_string(BootElfContext *ctx, uint32_t sh_name_idx,
                                    char *out, uint32_t out_size)
{
    if (ctx->shstrtab == NULL || out == NULL || out_size == 0) return false;
    uint32_t max_len = out_size - 1;
    strncpy(out, ctx->shstrtab + sh_name_idx, max_len);
    out[max_len] = '\0';
    return true;
}

bool boot_elf_find_section(BootElfContext *ctx, const char *name,
                            void *shdr_out, uint32_t *offset, uint32_t *size)
{
    if (ctx == NULL || name == NULL) return false;

    uint16_t shnum, shentsize, shstrndx;
    uint32_t shoff;

    if (ctx->elf_class == ELFCLASS32) {
        shnum = ctx->ehdr32.e_shnum; shoff = ctx->ehdr32.e_shoff;
        shentsize = ctx->ehdr32.e_shentsize; shstrndx = ctx->ehdr32.e_shstrndx;
    } else {
        shnum = ctx->ehdr64.e_shnum; shoff = (uint32_t)ctx->ehdr64.e_shoff;
        shentsize = ctx->ehdr64.e_shentsize; shstrndx = ctx->ehdr64.e_shstrndx;
    }

    if (shnum == 0 || shoff == 0) return false;

    uint32_t str_hdr_off = shoff + (uint32_t)shstrndx * shentsize;
    uint32_t str_off = 0, str_sz = 0;

    if (ctx->elf_class == ELFCLASS32) {
        if (str_hdr_off + sizeof(Elf32_Shdr) > ctx->image_size) return false;
        Elf32_Shdr shstr_hdr;
        memcpy(&shstr_hdr, ctx->image + str_hdr_off, sizeof(Elf32_Shdr));
        str_off = shstr_hdr.sh_offset; str_sz = shstr_hdr.sh_size;
    } else {
        if (str_hdr_off + sizeof(Elf64_Shdr) > ctx->image_size) return false;
        Elf64_Shdr shstr_hdr;
        memcpy(&shstr_hdr, ctx->image + str_hdr_off, sizeof(Elf64_Shdr));
        str_off = (uint32_t)shstr_hdr.sh_offset; str_sz = (uint32_t)shstr_hdr.sh_size;
    }

    if (str_off + str_sz > ctx->image_size) return false;
    ctx->shstrtab = (char *)(ctx->image + str_off);

    for (uint16_t i = 0; i < shnum; i++) {
        uint32_t hdr_off = shoff + (uint32_t)i * shentsize;
        uint32_t sh_name_idx = 0, sh_type_val __attribute__((unused)) = 0;
        uint32_t sh_offset = 0, sh_size = 0;

        if (ctx->elf_class == ELFCLASS32) {
            if (hdr_off + sizeof(Elf32_Shdr) > ctx->image_size) break;
            Elf32_Shdr shdr;
            memcpy(&shdr, ctx->image + hdr_off, sizeof(Elf32_Shdr));
            sh_name_idx = shdr.sh_name; sh_type_val = shdr.sh_type;
            sh_offset = shdr.sh_offset; sh_size = shdr.sh_size;
        } else {
            if (hdr_off + sizeof(Elf64_Shdr) > ctx->image_size) break;
            Elf64_Shdr shdr;
            memcpy(&shdr, ctx->image + hdr_off, sizeof(Elf64_Shdr));
            sh_name_idx = shdr.sh_name; sh_type_val = shdr.sh_type;
            sh_offset = (uint32_t)shdr.sh_offset; sh_size = (uint32_t)shdr.sh_size;
        }

        char sec_name[64];
        if (!elf_get_section_string(ctx, sh_name_idx, sec_name, sizeof(sec_name)))
            continue;

        if (strcmp(sec_name, name) == 0) {
            if (shdr_out != NULL && ctx->elf_class == ELFCLASS32)
                memcpy(shdr_out, ctx->image + hdr_off, sizeof(Elf32_Shdr));
            else if (shdr_out != NULL)
                memcpy(shdr_out, ctx->image + hdr_off, sizeof(Elf64_Shdr));
            if (offset) *offset = sh_offset;
            if (size)   *size   = sh_size;
            printf("[elf] Section '%s': offset=0x%X size=%u\n", name, sh_offset, sh_size);
            return true;
        }
    }
    return false;
}

bool boot_elf_find_symbol(BootElfContext *ctx, const char *name,
                           uint64_t *value, uint64_t *size)
{
    if (ctx == NULL || name == NULL) return false;

    uint32_t symtab_off = 0, symtab_sz = 0;
    uint32_t strtab_off = 0, strtab_sz = 0;

    if (!boot_elf_find_section(ctx, ".symtab", NULL, &symtab_off, &symtab_sz)) {
        fprintf(stderr, "[elf] No .symtab section\n"); return false;
    }
    if (!boot_elf_find_section(ctx, ".strtab", NULL, &strtab_off, &strtab_sz)) {
        fprintf(stderr, "[elf] No .strtab section\n"); return false;
    }

    if (symtab_off + symtab_sz > ctx->image_size ||
        strtab_off + strtab_sz > ctx->image_size) return false;

    char *strtab = (char *)(ctx->image + strtab_off);
    uint8_t *symtab = ctx->image + symtab_off;
    uint32_t entsize = (ctx->elf_class == ELFCLASS32) ?
                        sizeof(Elf32_Sym) : sizeof(Elf64_Sym);
    uint32_t nsyms = symtab_sz / entsize;

    for (uint32_t i = 0; i < nsyms; i++) {
        uint32_t st_name = 0;
        uint64_t st_value = 0, st_size = 0;

        if (ctx->elf_class == ELFCLASS32) {
            Elf32_Sym sym;
            memcpy(&sym, symtab + i * entsize, sizeof(Elf32_Sym));
            st_name = sym.st_name; st_value = sym.st_value; st_size = sym.st_size;
        } else {
            Elf64_Sym sym;
            memcpy(&sym, symtab + i * entsize, sizeof(Elf64_Sym));
            st_name = sym.st_name; st_value = sym.st_value; st_size = sym.st_size;
        }

        if (st_name >= strtab_sz) continue;
        const char *sym_name = strtab + st_name;

        if (strcmp(sym_name, name) == 0) {
            if (value) *value = st_value;
            if (size)  *size  = st_size;
            printf("[elf] Symbol '%s' = 0x%llX\n", name, (unsigned long long)st_value);
            return true;
        }
    }
    fprintf(stderr, "[elf] Symbol '%s' not found\n", name);
    return false;
}

bool boot_elf_apply_relocations(BootElfContext *ctx, uint8_t *base,
                                 uint32_t base_offset)
{
    if (ctx == NULL || base == NULL) return false;

    uint32_t rel_off = 0, rel_sz = 0;
    const char *rel_secs[] = {".rel.dyn", ".rel.text", ".rela.dyn", ".rela.text", NULL};
    bool found = false;
    for (int si = 0; rel_secs[si] != NULL; si++) {
        if (boot_elf_find_section(ctx, rel_secs[si], NULL, &rel_off, &rel_sz)) {
            found = true; break;
        }
    }
    if (!found) { printf("[elf] No relocation sections\n"); return true; }
    if (rel_off + rel_sz > ctx->image_size) return false;

    uint32_t rel_count = rel_sz / sizeof(Elf32_Rel);
    Elf32_Rel *relocs = (Elf32_Rel *)(ctx->image + rel_off);
    uint32_t applied = 0;

    printf("[elf] Applying %u relocations...\n", rel_count);
    for (uint32_t i = 0; i < rel_count; i++) {
        uint32_t r_offset = relocs[i].r_offset;
        uint8_t  r_type   = ELF32_R_TYPE(relocs[i].r_info);
        uint32_t S = base_offset, P = r_offset, A = 0;

        if (r_offset + 4 <= ctx->image_size)
            memcpy(&A, base + r_offset, 4);

        uint32_t patch_addr = r_offset, patch_val = 0;
        switch (r_type) {
            case R_386_NONE:  continue;
            case R_386_32:    patch_val = S + A;      break;
            case R_386_PC32:  patch_val = S + A - P;  break;
            case R_386_GOTPC: patch_val = S + A - P;  break;
            case R_386_PLT32: patch_val = S + A - P;  break;
            default:
                fprintf(stderr, "[elf] Unsupported reloc type %u @ 0x%X\n", r_type, r_offset);
                continue;
        }
        if (patch_addr + 4 <= ctx->image_size) {
            memcpy(base + patch_addr, &patch_val, 4);
            applied++;
        }
    }
    printf("[elf] Applied %u/%u relocations\n", applied, rel_count);
    return true;
}

bool boot_elf_validate(const BootElfContext *ctx)
{
    if (ctx == NULL || ctx->image == NULL) return false;
    const uint8_t *p = ctx->image;
    if (p[0] != ELF_MAG0 || p[1] != ELF_MAG1 ||
        p[2] != ELF_MAG2 || p[3] != ELF_MAG3) return false;
    if (p[4] != ELFCLASS32 && p[4] != ELFCLASS64) return false;
    if (p[5] != ELFDATA2LSB && p[5] != ELFDATA2MSB) return false;
    if (p[6] != EV_CURRENT && p[6] != 0) return false;
    return true;
}

void boot_elf_print_header(const BootElfContext *ctx)
{
    if (ctx == NULL) return;

    printf("\n=== ELF Header (ELF%u %s) ===\n",
           ctx->elf_class == ELFCLASS32 ? 32 : 64,
           ctx->elf_data == ELFDATA2LSB ? "LE" : "BE");

    if (ctx->elf_class == ELFCLASS32) {
        printf("Type:     %u  Machine: 0x%04X  Entry: 0x%08X\n",
               ctx->ehdr32.e_type, ctx->ehdr32.e_machine, ctx->ehdr32.e_entry);
        printf("PH off:   0x%08X (%u entries)\n",
               ctx->ehdr32.e_phoff, ctx->ehdr32.e_phnum);
        printf("SH off:   0x%08X (%u entries, strndx=%u)\n",
               ctx->ehdr32.e_shoff, ctx->ehdr32.e_shnum, ctx->ehdr32.e_shstrndx);
    } else {
        printf("Type:     %u  Machine: 0x%04X  Entry: 0x%016llX\n",
               ctx->ehdr64.e_type, ctx->ehdr64.e_machine,
               (unsigned long long)ctx->ehdr64.e_entry);
        printf("PH off:   0x%016llX (%u entries)\n",
               (unsigned long long)ctx->ehdr64.e_phoff, ctx->ehdr64.e_phnum);
        printf("SH off:   0x%016llX (%u entries, strndx=%u)\n",
               (unsigned long long)ctx->ehdr64.e_shoff, ctx->ehdr64.e_shnum,
               ctx->ehdr64.e_shstrndx);
    }
}