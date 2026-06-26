#ifndef PE_COFF_H
#define PE_COFF_H

#include <stdbool.h>
#include <stdint.h>

/* PE machine types */
#define IMAGE_FILE_MACHINE_I386  0x014C
#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_FILE_MACHINE_ARM64 0xAA64
#define IMAGE_FILE_MACHINE_IA64  0x0200

/* PE characteristics */
#define IMAGE_FILE_EXECUTABLE_IMAGE      0x0002
#define IMAGE_FILE_LINE_NUMS_STRIPPED    0x0004
#define IMAGE_FILE_LARGE_ADDRESS_AWARE   0x0020
#define IMAGE_FILE_32BIT_MACHINE         0x0100
#define IMAGE_FILE_DLL                   0x2000

/* Subsystems */
#define IMAGE_SUBSYSTEM_EFI_APPLICATION       10
#define IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE      11
#define IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER    12
#define IMAGE_SUBSYSTEM_WINDOWS_GUI            2
#define IMAGE_SUBSYSTEM_WINDOWS_CUI            3

/* Section characteristics */
#define IMAGE_SCN_CNT_CODE              0x00000020
#define IMAGE_SCN_CNT_INITIALIZED_DATA  0x00000040
#define IMAGE_SCN_MEM_EXECUTE           0x20000000
#define IMAGE_SCN_MEM_READ              0x40000000
#define IMAGE_SCN_MEM_WRITE             0x80000000

/* Relocation types for AMD64 */
#define IMAGE_REL_BASED_ABSOLUTE  0
#define IMAGE_REL_BASED_HIGH      1
#define IMAGE_REL_BASED_LOW       2
#define IMAGE_REL_BASED_HIGHLOW   3
#define IMAGE_REL_BASED_DIR64     10

/* PE signature */
#define PE_SIGNATURE  0x00004550
#define COFF_OBJECT_HEADER_OFFSET  0x3C
#define PE_OPTIONAL_HEADER_MAGIC_PE32     0x010B
#define PE_OPTIONAL_HEADER_MAGIC_PE32PLUS 0x020B

/* Data directories */
#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16
#define IMAGE_DIRECTORY_ENTRY_EXPORT           0
#define IMAGE_DIRECTORY_ENTRY_IMPORT           1
#define IMAGE_DIRECTORY_ENTRY_RESOURCE         2
#define IMAGE_DIRECTORY_ENTRY_EXCEPTION        3
#define IMAGE_DIRECTORY_ENTRY_SECURITY         4
#define IMAGE_DIRECTORY_ENTRY_BASERELOC        5
#define IMAGE_DIRECTORY_ENTRY_DEBUG            6
#define IMAGE_DIRECTORY_ENTRY_ARCHITECTURE     7
#define IMAGE_DIRECTORY_ENTRY_TLS              9
#define IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG     10

/* ===== PE/COFF structures ===== */
#pragma pack(push, 1)

typedef struct {
    uint16_t machine;
    uint16_t num_sections;
    uint32_t timestamp;
    uint32_t symbol_table_offset;
    uint32_t num_symbols;
    uint16_t optional_header_size;
    uint16_t characteristics;
} PECoffHeader;

typedef struct {
    uint32_t virtual_address;
    uint32_t size;
} PEDataDirectory;

typedef struct {
    uint16_t magic;
    uint8_t  major_linker_version;
    uint8_t  minor_linker_version;
    uint32_t size_of_code;
    uint32_t size_of_initialized_data;
    uint32_t size_of_uninitialized_data;
    uint32_t address_of_entry_point;
    uint32_t base_of_code;
    uint32_t base_of_data;
    uint64_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint16_t major_os_version;
    uint16_t minor_os_version;
    uint16_t major_image_version;
    uint16_t minor_image_version;
    uint16_t major_subsystem_version;
    uint16_t minor_subsystem_version;
    uint32_t win32_version;
    uint32_t size_of_image;
    uint32_t size_of_headers;
    uint32_t checksum;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint64_t size_of_stack_reserve;
    uint64_t size_of_stack_commit;
    uint64_t size_of_heap_reserve;
    uint64_t size_of_heap_commit;
    uint32_t loader_flags;
    uint32_t num_data_directories;
    PEDataDirectory data_directories[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} PEOptionalHeaderPlus;

typedef struct {
    uint64_t name;
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t size_of_raw_data;
    uint32_t pointer_to_raw_data;
    uint32_t pointer_to_relocations;
    uint32_t pointer_to_linenumbers;
    uint16_t num_relocations;
    uint16_t num_linenumbers;
    uint32_t characteristics;
} PESectionHeader;

typedef struct {
    uint32_t virtual_address;
    uint32_t size_of_block;
} PEBaseRelocationBlock;

typedef struct {
    uint16_t offset : 12;
    uint16_t type   : 4;
} PEBaseRelocationEntry;

typedef struct {
    uint64_t image_base;
    uint64_t entry_point;
    uint64_t section_alignment;
    uint64_t image_size;
    void     *loaded_base;
} PELoadedImage;

typedef struct {
    uint16_t machine;
    uint16_t num_sections;
    uint64_t entry_point;
    uint64_t image_base;
    uint64_t size_of_image;
    uint16_t subsystem;
    uint16_t characteristics;
    uint64_t section_alignment;
    PESectionHeader sections[32];
    uint32_t num_loaded_sections;
    void     *raw_data;
    size_t   raw_size;
} PEHeader;

#pragma pack(pop)

/* ===== Functions ===== */
int  pecoff_load_image(const char *path, PEHeader *header);
bool pecoff_validate_header(const PEHeader *header);
int  pecoff_relocate(PEHeader *header, void *load_base);
void *pecoff_find_entry(const PEHeader *header, void *load_base);
void pecoff_print_header(const PEHeader *header);
int  pecoff_load_from_buffer(const uint8_t *data, size_t size, PEHeader *header);
bool pecoff_is_valid_pe_machine(uint16_t machine);

#endif /* PE_COFF_H */
