#ifndef FIRMWARE_VOLUME_H
#define FIRMWARE_VOLUME_H

#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * Firmware Volume (FV) ? UEFI PI Firmware Storage
 * Reference: UEFI PI Specification v1.8, Volume 3: Shared Architectural Elements
 *
 * L1: Core data structures ? FV Header, FFS File Header, Section Headers.
 * L2: Firmware Volumes are the storage abstraction used by PI firmware
 *     to organize code, data, and configuration in flash memory.
 * L3: FV scanning algorithm builds an in-memory catalog of FFS files
 *     organized by GUID and type for the DXE dispatcher.
 * L4: UEFI PI Vol 3 ?2-4 ? Firmware Volume and File System definitions.
 * ============================================================================ */

/* --- Firmware Volume Header ---
 * Every FV begins with this 56+ byte header. The FV contains FFS files
 * that hold PEIMs, DXE drivers, and other firmware components.
 */

#define FV_SIGNATURE  0x4856465F  /* "_FVH" */
#define FV_HEADER_SIZE 56

/* FV attributes (bitmask) */
#define FV_ATTR_READ_STATUS         (1 << 0)
#define FV_ATTR_WRITE_STATUS        (1 << 1)
#define FV_ATTR_LOCK_STATUS         (1 << 2)
#define FV_ATTR_WRITE_POLICY_RELIABLE (1 << 3)
#define FV_ATTR_READ_LOCK_CAP       (1 << 4)
#define FV_ATTR_READ_LOCK_STATUS    (1 << 5)
#define FV_ATTR_WRITE_LOCK_CAP      (1 << 6)
#define FV_ATTR_WRITE_LOCK_STATUS   (1 << 7)
#define FV_ATTR_ERASE_POLARITY      (1 << 8)
#define FV_ATTR_ALIGNMENT_CAP       (1 << 9)
#define FV_ATTR_ALIGNMENT_2         (1 << 10)
#define FV_ATTR_STICKY_WRITE        (1 << 13)
#define FV_ATTR_MEMORY_MAPPED       (1 << 14)
#define FV_ATTR_ERASE_POLARITY_TRUE (1 << 15)

typedef struct __attribute__((packed)) {
    uint8_t  zero_vector[16];      /* Must be all zeros */
    uint8_t  file_system_guid[16]; /* Identifies FFS version */
    uint64_t fv_length;            /* Total bytes of this FV */
    uint32_t signature;            /* "_FVH" */
    uint32_t attributes;           /* FV attribute bitmask */
    uint16_t header_length;        /* Size of this header in bytes */
    uint16_t checksum;             /* 16-bit sum of header = 0 */
    uint16_t ext_header_offset;    /* Offset of extended header or 0 */
    uint8_t  reserved;             /* Must be 0 */
    uint8_t  revision;             /* FV format revision */
    /* Block map follows: array of (uint32_t count, uint32_t size) pairs
     * terminated by (0, 0) */
    uint32_t block_map[];
} FVHeader;

/* --- GUID (Globally Unique Identifier) ---
 * 128-bit identifier used throughout UEFI to identify protocols,
 * file types, and firmware components. RFC 4122 format.
 */
typedef struct __attribute__((packed)) {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} EFI_GUID;

/* --- FFS File Header ---
 * Each file within a Firmware Volume is prefixed by this header.
 * Files are aligned to 8-byte boundaries.
 */
#define FFS_FILE_HEADER_SIZE  24

/* FFS File Types */
#define FFS_TYPE_RAW                      0x01
#define FFS_TYPE_FREEFORM                 0x02
#define FFS_TYPE_SECURITY_CORE            0x03
#define FFS_TYPE_PEI_CORE                 0x04
#define FFS_TYPE_DXE_CORE                 0x05
#define FFS_TYPE_PEIM                     0x06
#define FFS_TYPE_DRIVER                   0x07
#define FFS_TYPE_COMBINED_PEIM_DRIVER     0x08
#define FFS_TYPE_APPLICATION              0x09
#define FFS_TYPE_MM                       0x0A
#define FFS_TYPE_FIRMWARE_VOLUME_IMAGE    0x0B
#define FFS_TYPE_COMBINED_MM_DXE          0x0C
#define FFS_TYPE_MM_CORE                  0x0D
#define FFS_TYPE_MM_STANDALONE            0x0E
#define FFS_TYPE_MM_CORE_STANDALONE       0x0F
#define FFS_TYPE_PAD                      0xF0

/* FFS File Attributes */
#define FFS_ATTR_TAIL_PRESENT    0x01
#define FFS_ATTR_RECOVERY        0x02
#define FFS_ATTR_HEADER_EXTENSION 0x04
#define FFS_ATTR_CHECKSUM        0x08  /* File checksum present */

/* FFS File State */
#define FFS_STATE_CONSTRUCTED  0x00
#define FFS_STATE_VALID_FILE   0x07
#define FFS_STATE_INVALIDATED  0x3F
#define FFS_STATE_DELETED      0x08
#define FFS_STATE_ERASED       0xFF
#define FFS_STATE_MASK         0x7F

typedef struct __attribute__((packed)) {
    EFI_GUID name;             /* Unique identifier for this file */
    uint8_t  integrity_check[2]; /* Checksum reference */
    uint8_t  type;             /* FFS_TYPE_* */
    uint8_t  attributes;       /* File attributes bitmask */
    uint8_t  size[3];          /* Total file size (24-bit, little-endian) */
    uint8_t  state;            /* File state (FFS_STATE_*) */
} FFSFileHeader;

/* --- FFS File Sections ---
 * Each FFS file body contains one or more sections. Sections can be
 * compressed, GUID-defined, or raw binary data (PE32+, TE, etc.).
 */

#define FFS_SECTION_HEADER_SIZE  4

/* Section Types */
#define SECTION_TYPE_COMPRESSION          0x01
#define SECTION_TYPE_GUID_DEFINED         0x02
#define SECTION_TYPE_DISPOSABLE           0x03
#define SECTION_TYPE_PE32                 0x10
#define SECTION_TYPE_PIC                  0x11
#define SECTION_TYPE_TE                   0x12
#define SECTION_TYPE_DXE_DEPEX            0x13
#define SECTION_TYPE_VERSION              0x14
#define SECTION_TYPE_USER_INTERFACE       0x15
#define SECTION_TYPE_COMPATIBILITY16      0x16
#define SECTION_TYPE_FIRMWARE_VOLUME_IMAGE 0x17
#define SECTION_TYPE_FREEFORM_SUBTYPE_GUID 0x18
#define SECTION_TYPE_RAW                  0x19
#define SECTION_TYPE_PEI_DEPEX            0x1B
#define SECTION_TYPE_MM_DEPEX             0x1C

typedef struct __attribute__((packed)) {
    uint8_t  size[3];        /* 24-bit section size (includes header) */
    uint8_t  type;           /* SECTION_TYPE_* */
} FFSSectionHeader;

/* --- FV In-Memory Catalog ---
 * L3: Engineering structure for efficient file lookup.
 * Scanned at DXE load time to build a GUID?file mapping.
 */

#define FV_MAX_FILES  128

typedef struct {
    EFI_GUID file_guid;
    uint8_t  file_type;
    uint32_t file_offset;    /* Byte offset within FV of file header */
    uint32_t file_size;      /* Total bytes including header */
    uint8_t  state;
    bool     present;
} FVFileEntry;

typedef struct {
    FVHeader    fv_header;
    uint64_t    fv_base;     /* Physical base address of this FV */
    FVFileEntry files[FV_MAX_FILES];
    uint32_t    file_count;
    bool        parsed;
} FVCatalog;

/* --- SHA-256 Hash Context (for FV integrity) ---
 * L8: Advanced ? cryptographic verification of firmware volumes.
 * SHA-256 is used in UEFI Secure Boot and Measured Boot for
 * firmware integrity measurement. RFC 6234 implementation.
 */

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32
#define SHA256_STATE_WORDS 8

typedef struct {
    uint32_t state[SHA256_STATE_WORDS];
    uint64_t bit_count;
    uint8_t  buffer[SHA256_BLOCK_SIZE];
    uint32_t buffer_offset;
} SHA256Context;

/* --- Firmware Volume API --- */

/* Parse FV header from memory. Returns true if signature and checksum valid. */
bool fv_parse_header(FVHeader *fv_hdr, const uint8_t *fv_base);

/* Check if FV header is valid (signature "_FVH" and header checksum = 0). */
bool fv_is_valid(const FVHeader *fv_hdr);

/* Get FFS file size from 24-bit little-endian size field. */
uint32_t ffs_get_file_size(const FFSFileHeader *ffs_hdr);

/* Check if an FFS file is in a valid state for loading. */
bool ffs_is_valid_file(const FFSFileHeader *ffs_hdr);

/* Get the string name for an FFS file type. */
const char *ffs_type_name(uint8_t type);

/* Scan a Firmware Volume and populate a catalog of all valid FFS files.
 * This is the L3 engineering structure: the catalog enables O(1) GUID
 * lookup for the DXE dispatcher's DEPEX resolution. O(n) scan time. */
uint32_t fv_scan_files(FVCatalog *catalog, const uint8_t *fv_base);

/* Find a file within a scanned catalog by its GUID.
 * Returns index into catalog->files[], or -1 if not found. */
int32_t fv_find_by_guid(const FVCatalog *catalog, const EFI_GUID *guid);

/* Find all files of a given type. Returns number found (up to max_results). */
uint32_t fv_find_by_type(const FVCatalog *catalog, uint8_t file_type,
                         uint32_t *indices, uint32_t max_results);

/* Print FV header summary for debug output. */
void fv_print_header(const FVHeader *fv_hdr);

/* Print the catalog file listing. */
void fv_print_catalog(const FVCatalog *catalog);

/* Print a GUID in standard 8-4-4-4-12 hex format. */
void guid_print(const EFI_GUID *guid);

/* Compare two GUIDs for equality. Returns true if identical. */
bool guid_equal(const EFI_GUID *a, const EFI_GUID *b);

/* --- SHA-256 API ---
 * L5: SHA-256 hash algorithm per FIPS 180-4.
 * Used for FV integrity verification (measured boot).
 */

/* Initialize SHA-256 context. */
void sha256_init(SHA256Context *ctx);

/* Update hash with new input data. Can be called multiple times. */
void sha256_update(SHA256Context *ctx, const uint8_t *data, uint32_t length);

/* Finalize hash and produce 32-byte digest. */
void sha256_final(SHA256Context *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);

/* Compute SHA-256 of data in a single call (convenience wrapper). */
void sha256_hash(const uint8_t *data, uint32_t length,
                 uint8_t digest[SHA256_DIGEST_SIZE]);

/* Verify firmware volume integrity by computing SHA-256 over FV content.
 * L8: Measured Boot ? FV measurements are extended into TPM PCRs. */
bool fv_verify_integrity(const uint8_t *fv_data, uint64_t fv_length);

#endif /* FIRMWARE_VOLUME_H */
