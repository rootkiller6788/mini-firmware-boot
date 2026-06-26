#include "pe_coff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool pecoff_is_valid_pe_machine(uint16_t machine) {
    switch (machine) {
    case IMAGE_FILE_MACHINE_I386:
    case IMAGE_FILE_MACHINE_AMD64:
    case IMAGE_FILE_MACHINE_ARM64:
    case IMAGE_FILE_MACHINE_IA64:
        return true;
    default:
        return false;
    }
}

int pecoff_load_from_buffer(const uint8_t *data, size_t size, PEHeader *header) {
    if (!data || !header || size < sizeof(PECoffHeader) + 4) {
        return -1;
    }

    memset(header, 0, sizeof(PEHeader));

    /* Read PE offset from DOS header at offset 0x3C */
    uint32_t pe_offset = *(const uint32_t *)(data + COFF_OBJECT_HEADER_OFFSET);
    if (pe_offset + 4 + sizeof(PECoffHeader) > size) return -2;

    /* Verify 4-byte PE signature "PE\0\0" */
    const uint8_t *coff_start = data + pe_offset;
    if (*(const uint32_t *)coff_start != (uint32_t)PE_SIGNATURE) return -3;

    const PECoffHeader *coff = (const PECoffHeader *)(coff_start + 4);

    header->machine       = coff->machine;
    header->num_sections  = coff->num_sections;
    header->characteristics = coff->characteristics;

    /* Read optional header */
    const uint8_t *opt_start = coff_start + 4 + sizeof(PECoffHeader);
    uint16_t opt_magic = *(const uint16_t *)opt_start;

    if (opt_magic == PE_OPTIONAL_HEADER_MAGIC_PE32PLUS) {
        const PEOptionalHeaderPlus *opt = (const PEOptionalHeaderPlus *)opt_start;
        header->entry_point      = opt->address_of_entry_point;
        header->image_base       = opt->image_base;
        header->size_of_image    = opt->size_of_image;
        header->subsystem        = opt->subsystem;
        header->section_alignment = opt->section_alignment;
    } else if (opt_magic == PE_OPTIONAL_HEADER_MAGIC_PE32) {
        /* PE32: image_base is 32 bits, entry_point too */
        header->entry_point      = *(const uint32_t *)(opt_start + 16);
        header->image_base       = *(const uint32_t *)(opt_start + 28);
        header->size_of_image    = *(const uint32_t *)(opt_start + 56);
        header->subsystem        = *(const uint16_t *)(opt_start + 68);
        header->section_alignment = *(const uint32_t *)(opt_start + 32);
    } else {
        return -4;
    }

    /* Read section headers */
    if (header->num_sections > 32) header->num_sections = 32;
    const PESectionHeader *sec =
        (const PESectionHeader *)(opt_start + coff->optional_header_size);
    header->num_loaded_sections = header->num_sections;

    for (uint32_t i = 0; i < header->num_sections; i++) {
        memcpy(&header->sections[i], &sec[i], sizeof(PESectionHeader));
    }

    header->raw_data = (void *)data;
    header->raw_size = size;

    printf("  PE/COFF: loaded image, machine=0x%04X, sections=%u, entry=0x%llX\n",
           header->machine, header->num_sections,
           (unsigned long long)header->entry_point);
    return 0;
}

int pecoff_load_image(const char *path, PEHeader *header) {
    if (!path || !header) return -1;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("  PE/COFF: Failed to open '%s'\n", path);
        return -2;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0 || (size_t)file_size < sizeof(PECoffHeader)) {
        fclose(fp);
        return -3;
    }

    uint8_t *data = malloc((size_t)file_size);
    if (!data) {
        fclose(fp);
        return -4;
    }

    size_t read_size = fread(data, 1, (size_t)file_size, fp);
    fclose(fp);

    if (read_size != (size_t)file_size) {
        free(data);
        return -5;
    }

    int result = pecoff_load_from_buffer(data, (size_t)file_size, header);
    if (result != 0) {
        free(data);
        header->raw_data = NULL;
        header->raw_size = 0;
    }

    return result;
}

bool pecoff_validate_header(const PEHeader *header) {
    if (!header) return false;

    if (!pecoff_is_valid_pe_machine(header->machine)) {
        printf("  PE/COFF: Unsupported machine type 0x%04X\n", header->machine);
        return false;
    }

    if (header->subsystem != IMAGE_SUBSYSTEM_EFI_APPLICATION &&
        header->subsystem != IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE) {
        printf("  PE/COFF: Warning — subsystem 0x%04X is not EFI\n", header->subsystem);
    }

    if (header->num_sections == 0) {
        printf("  PE/COFF: No sections found\n");
        return false;
    }

    if (header->entry_point >= header->size_of_image) {
        printf("  PE/COFF: Entry point (0x%llX) beyond image size (0x%llX)\n",
               (unsigned long long)header->entry_point,
               (unsigned long long)header->size_of_image);
        return false;
    }

    printf("  PE/COFF: Header validation OK\n");
    return true;
}

int pecoff_relocate(PEHeader *header, void *load_base) {
    if (!header || !load_base) return -1;

    uint64_t load_delta = (uint64_t)(uintptr_t)load_base - header->image_base;
    if (load_delta == 0) {
        printf("  PE/COFF: Image loaded at preferred base, no relocation needed\n");
        return 0;
    }

    printf("  PE/COFF: Relocating image base 0x%llX -> 0x%p (delta=0x%llX)\n",
           (unsigned long long)header->image_base, load_base,
           (unsigned long long)load_delta);

    /* Find .reloc section */
    const PESectionHeader *reloc_sec = NULL;
    for (uint32_t i = 0; i < header->num_loaded_sections; i++) {
        if (strncmp((const char *)&header->sections[i].name, ".reloc", 6) == 0) {
            reloc_sec = &header->sections[i];
            break;
        }
    }

    if (!reloc_sec) {
        printf("  PE/COFF: No .reloc section found\n");
        return -2;
    }

    /* Apply relocations */
    uint32_t offset = 0;
    const uint8_t *raw = (const uint8_t *)header->raw_data;
    uint8_t *image = (uint8_t *)load_base;

    while (offset < reloc_sec->size_of_raw_data) {
        uint32_t file_offset = reloc_sec->pointer_to_raw_data + offset;
        const PEBaseRelocationBlock *block =
            (const PEBaseRelocationBlock *)(raw + file_offset);

        if (block->virtual_address == 0) break;

        uint32_t entry_count = (block->size_of_block - sizeof(PEBaseRelocationBlock)) / 2;
        const PEBaseRelocationEntry *entries =
            (const PEBaseRelocationEntry *)(raw + file_offset + sizeof(PEBaseRelocationBlock));

        for (uint32_t j = 0; j < entry_count; j++) {
            if (entries[j].type == IMAGE_REL_BASED_ABSOLUTE) continue;

            uint64_t *addr = (uint64_t *)(image + block->virtual_address + entries[j].offset);

            switch (entries[j].type) {
            case IMAGE_REL_BASED_DIR64:
                *addr += load_delta;
                break;
            case IMAGE_REL_BASED_HIGHLOW:
                *(uint32_t *)addr += (uint32_t)load_delta;
                break;
            default:
                printf("  PE/COFF: Warning — unhandled relocation type %u\n",
                       entries[j].type);
                break;
            }
        }

        offset += block->size_of_block;
    }

    printf("  PE/COFF: Relocation complete\n");
    return 0;
}

void *pecoff_find_entry(const PEHeader *header, void *load_base) {
    if (!header || !load_base) return NULL;

    uint64_t entry_rva = header->entry_point;
    void *entry = (uint8_t *)load_base + entry_rva;

    printf("  PE/COFF: Entry point at RVA 0x%llX -> VA 0x%p\n",
           (unsigned long long)entry_rva, entry);
    return entry;
}

void pecoff_print_header(const PEHeader *header) {
    if (!header) { printf("PE header is NULL\n"); return; }

    printf("\n=== PE/COFF Header ===\n");
    printf("  Machine:             0x%04X", header->machine);
    switch (header->machine) {
    case IMAGE_FILE_MACHINE_I386:  printf(" (x86)\n"); break;
    case IMAGE_FILE_MACHINE_AMD64: printf(" (x64)\n"); break;
    case IMAGE_FILE_MACHINE_ARM64: printf(" (ARM64)\n"); break;
    default: printf(" (Unknown)\n"); break;
    }

    printf("  Sections:            %u\n", header->num_sections);
    printf("  Entry Point RVA:     0x%08llX\n", (unsigned long long)header->entry_point);
    printf("  Image Base:          0x%016llX\n", (unsigned long long)header->image_base);
    printf("  Image Size:          0x%08X (%u bytes)\n",
           (unsigned)header->size_of_image, (unsigned)header->size_of_image);
    printf("  Subsystem:           0x%04X", header->subsystem);
    switch (header->subsystem) {
    case IMAGE_SUBSYSTEM_EFI_APPLICATION:      printf(" (EFI Application)\n"); break;
    case IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE:     printf(" (EFI Boot Service)\n"); break;
    case IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER:   printf(" (EFI Runtime Driver)\n"); break;
    case IMAGE_SUBSYSTEM_WINDOWS_GUI:           printf(" (Windows GUI)\n"); break;
    default: printf(" (Other)\n"); break;
    }

    printf("  Characteristics:    0x%04X", header->characteristics);
    if (header->characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) printf(" [EXECUTABLE]");
    if (header->characteristics & IMAGE_FILE_DLL)              printf(" [DLL]");
    printf("\n");
    printf("  Section Alignment:   0x%X\n", (unsigned)header->section_alignment);

    printf("\n  Section Table:\n");
    printf("  %-10s  %8s  %8s  %10s  %12s  %8s\n",
           "Name", "VirtAddr", "VirtSize", "RawOffset", "RawSize", "Flags");
    for (uint32_t i = 0; i < header->num_loaded_sections; i++) {
        const PESectionHeader *s = &header->sections[i];
        char name_buf[9] = {0};
        memcpy(name_buf, &s->name, 8);
        printf("  %-10s  %08X  %08X  %10X  %12X  %08X",
               name_buf, s->virtual_address, s->virtual_size,
               s->pointer_to_raw_data, s->size_of_raw_data, s->characteristics);
        if (s->characteristics & IMAGE_SCN_CNT_CODE)               printf(" [CODE]");
        if (s->characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA)   printf(" [DATA]");
        if (s->characteristics & IMAGE_SCN_MEM_EXECUTE)            printf(" [EXEC]");
        if (s->characteristics & IMAGE_SCN_MEM_READ)               printf(" [READ]");
        if (s->characteristics & IMAGE_SCN_MEM_WRITE)              printf(" [WRITE]");
        printf("\n");
    }
}
