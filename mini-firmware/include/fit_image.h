#ifndef FIT_IMAGE_H
#define FIT_IMAGE_H

/*
 * fit_image.h -- Flattened Image Tree (FIT) Parser
 *
 * Implements the FIT image format used in:
 *   - U-Boot (mainline FIT support since v2013.07)
 *   - Linux kernel (FIT image for ARM64 booting)
 *   - OP-TEE (Trusted Execution Environment)
 *
 * FIT = FDT (Flattened Device Tree) with image descriptions.
 * Supports multiple kernel, dtb, ramdisk, and firmware images in one blob.
 *
 * Knowledge coverage:
 *   L1: FDT header, FIT config nodes, image descriptions
 *   L3: FIT image parsing and validation pipeline
 *   L5: FDT traversal algorithm (Big-Endian byte order)
 *   L6: Multi-platform firmware packaging problem
 *   L7: U-Boot FIT image loading
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define FDT_MAGIC        0xD00DFEED
#define FIT_MAX_IMAGES   16
#define FIT_MAX_CONFIGS  8
#define FDT_MAX_PATH     256

/* ??? L1: Flattened Device Tree Header ??????????????????????????? */

/*
 * FDT Header (Big-Endian, per ePAPR spec v1.1).
 * All integer fields are stored in big-endian byte order regardless
 * of the host CPU endianness.
 */
typedef struct {
    uint32_t magic;             /* 0xD00DFEED                     */
    uint32_t totalsize;         /* Total size of the DT blob      */
    uint32_t off_dt_struct;     /* Offset to structure block      */
    uint32_t off_dt_strings;    /* Offset to strings block        */
    uint32_t off_mem_rsvmap;    /* Offset to memory reservation   */
    uint32_t version;           /* Format version (= 17)          */
    uint32_t last_comp_version; /* Min compatible version (= 16)  */
    uint32_t boot_cpuid_phys;   /* Physical CPU ID for boot       */
    uint32_t size_dt_strings;   /* Size of strings block          */
    uint32_t size_dt_struct;    /* Size of structure block        */
} FDTHeader;

/* FDT token types */
typedef enum {
    FDT_BEGIN_NODE = 0x00000001,
    FDT_END_NODE   = 0x00000002,
    FDT_PROP       = 0x00000003,
    FDT_NOP        = 0x00000004,
    FDT_END        = 0x00000009
} FDTToken;

/* ??? L1: FIT Image Description ???????????????????????????????????? */

typedef enum {
    FIT_TYPE_KERNEL    = 0,
    FIT_TYPE_RAMDISK   = 1,
    FIT_TYPE_FDT       = 2,   /* Flattened Device Tree (DTB)    */
    FIT_TYPE_FIRMWARE  = 3,   /* ARM Trusted Firmware, OP-TEE   */
    FIT_TYPE_STANDALONE = 4,  /* Standalone application         */
    FIT_TYPE_FPGA      = 5    /* FPGA bitstream                 */
} FITImageType;

typedef enum {
    FIT_COMP_NONE   = 0,
    FIT_COMP_GZIP   = 1,
    FIT_COMP_BZIP2  = 2,
    FIT_COMP_LZMA   = 3,
    FIT_COMP_LZO    = 4,
    FIT_COMP_LZ4    = 5,
    FIT_COMP_ZSTD   = 6
} FITCompression;

typedef struct {
    uint32_t        data_offset;      /* Offset within FIT blob      */
    uint32_t        data_size;        /* Size of image data (bytes)  */
    uint32_t        load_address;     /* Where to load into memory   */
    uint32_t        entry_point;      /* Execution entry point       */
    FITImageType    type;
    FITCompression  compression;
    char            description[64];  /* Human-readable description  */
    uint8_t         hash_sha256[32];  /* SHA-256 of image data       */
    bool            verified;         /* Has hash been verified?     */
} FITImageNode;

/* ??? L1: FIT Configuration ??????????????????????????????????????? */

/*
 * A configuration selects one kernel, optionally one or more FDTs,
 * and optionally a ramdisk. Multiple configs allow one FIT to boot
 * on different hardware variants.
 */
typedef struct {
    char            name[32];
    char            kernel_desc[64];   /* Description of kernel to use  */
    char            fdt_desc[64];      /* Description of FDT to use     */
    char            ramdisk_desc[64];  /* Description of ramdisk        */
    uint32_t        kernel_index;      /* Resolved index into images[]  */
    uint32_t        fdt_index;
    uint32_t        ramdisk_index;
    bool            is_default;        /* Default configuration         */
    char            compat_string[64]; /* For board matching            */
} FITConfigNode;

/* ??? L1: FIT Image ???????????????????????????????????????????????? */

typedef struct {
    FDTHeader       fdt_header;
    FITImageNode    images[FIT_MAX_IMAGES];
    uint32_t        image_count;
    FITConfigNode   configs[FIT_MAX_CONFIGS];
    uint32_t        config_count;
    char            description[64];
    uint32_t        timestamp;
    uint8_t        *raw_data;          /* Raw FIT blob data             */
    uint32_t        raw_size;
    bool            parsed;            /* TRUE after successful parse   */
} FITImage;

/* ??? L3/L5: FIT API ??????????????????????????????????????????????? */

/* Initialize and parse a FIT image from raw blob */
bool fit_parse(FITImage *fit, const uint8_t *blob, uint32_t blob_size);

/* Find an image by its description string */
const FITImageNode *fit_find_image(const FITImage *fit, const char *desc);

/* Find a configuration by name */
const FITConfigNode *fit_find_config(const FITImage *fit, const char *name);

/* Select the default configuration */
const FITConfigNode *fit_default_config(const FITImage *fit);

/* Set image metadata (for building FIT images) */
bool fit_add_image(FITImage *fit, const char *description,
                   FITImageType type, uint32_t load_addr,
                   uint32_t entry, const uint8_t *data, uint32_t size);

/* Add a configuration node */
bool fit_add_config(FITImage *fit, const char *name,
                    const char *kernel_desc, const char *fdt_desc,
                    bool is_default);

/* Validate SHA-256 hashes for all images */
bool fit_verify_hashes(FITImage *fit);

/* ??? L5: FDT traversal ?????????????????????????????????????????? */

/*
 * FDT structure block traversal.
 *
 * The structure block is a stream of tokens:
 *   FDT_BEGIN_NODE (name) { properties... children... } FDT_END_NODE
 *
 * All integers are big-endian. The algorithm walks recursively,
 * maintaining a stack of node names to construct full paths.
 *
 * Complexity: O(n) where n = structure block size.
 * Reference: Devicetree Specification v0.4, Chapter 5.
 */
bool fdt_walk_structure(const uint8_t *struct_blk, uint32_t struct_size,
                        const uint8_t *strings_blk);

/* Convert big-endian uint32 from FDT blob */
uint32_t fdt32_to_cpu(const uint8_t *p);

/* Print FIT image contents for diagnostics */
void fit_print_info(const FITImage *fit);

/* Print FDT header fields */
void fdt_print_header(const FDTHeader *hdr);

#endif /* FIT_IMAGE_H */
