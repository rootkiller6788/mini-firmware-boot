#include "firmware_volume.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * GUID Operations - L1/L3
 *
 * GUIDs are 128-bit identifiers in RFC 4122 format used throughout UEFI
 * to identify protocols, file types, and firmware components.
 *
 * Comparison is a 128-bit integer equality check: O(1).
 * ============================================================================ */

bool guid_equal(const EFI_GUID *a, const EFI_GUID *b)
{
    if (!a || !b) return false;
    return (a->data1 == b->data1) && (a->data2 == b->data2)
        && (a->data3 == b->data3)
        && (memcmp(a->data4, b->data4, 8) == 0);
}

void guid_print(const EFI_GUID *guid)
{
    if (!guid) { printf("(null)"); return; }
    printf("%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
           guid->data1, guid->data2, guid->data3,
           guid->data4[0], guid->data4[1],
           guid->data4[2], guid->data4[3],
           guid->data4[4], guid->data4[5],
           guid->data4[6], guid->data4[7]);
}

/* ============================================================================
 * Firmware Volume Header Parsing - L1/L3
 *
 * L1: FV Header layout per UEFI PI Vol 3 Section 2.3.
 * L3: Header validation checks signature and 16-bit header checksum.
 *
 * The FV header starts with a 16-byte zero vector (used for flash
 * initialization detection), followed by the file system GUID,
 * FV length, signature "_FVH", attributes, and a block map.
 * ============================================================================ */

bool fv_parse_header(FVHeader *fv_hdr, const uint8_t *fv_base)
{
    if (!fv_hdr || !fv_base) return false;

    memcpy(fv_hdr, fv_base, sizeof(FVHeader));

    /* Validate zero vector */
    for (int i = 0; i < 16; i++) {
        if (fv_hdr->zero_vector[i] != 0) return false;
    }

    return fv_is_valid(fv_hdr);
}

bool fv_is_valid(const FVHeader *fv_hdr)
{
    if (!fv_hdr) return false;

    /* Signature check */
    if (fv_hdr->signature != FV_SIGNATURE) return false;

    /* Header checksum: 16-bit sum of header bytes must be 0 */
    uint16_t sum = 0;
    const uint16_t *words = (const uint16_t *)fv_hdr;
    uint16_t hdr_words = fv_hdr->header_length / 2;
    if (hdr_words == 0) hdr_words = FV_HEADER_SIZE / 2;

    for (uint16_t i = 0; i < hdr_words; i++) {
        sum += words[i];
    }

    return (sum == 0);
}

/* ============================================================================
 * FFS File Size Decoding - L1/L3
 *
 * FFS file size is stored as a 24-bit little-endian value across 3 bytes.
 * L3: This compact encoding saves 1 byte per file header, significant
 *     when a firmware volume contains hundreds of files in limited flash.
 * ============================================================================ */

uint32_t ffs_get_file_size(const FFSFileHeader *ffs_hdr)
{
    if (!ffs_hdr) return 0;
    return ((uint32_t)ffs_hdr->size[0])
         | ((uint32_t)ffs_hdr->size[1] << 8)
         | ((uint32_t)ffs_hdr->size[2] << 16);
}

bool ffs_is_valid_file(const FFSFileHeader *ffs_hdr)
{
    if (!ffs_hdr) return false;

    /* State check: upper bit indicates validity */
    uint8_t state = ffs_hdr->state & FFS_STATE_MASK;
    return (state == FFS_STATE_VALID_FILE);
}

const char *ffs_type_name(uint8_t type)
{
    switch (type) {
    case FFS_TYPE_RAW:       return "RAW";
    case FFS_TYPE_FREEFORM:  return "FREEFORM";
    case FFS_TYPE_SECURITY_CORE: return "SEC_CORE";
    case FFS_TYPE_PEI_CORE:  return "PEI_CORE";
    case FFS_TYPE_DXE_CORE:  return "DXE_CORE";
    case FFS_TYPE_PEIM:      return "PEIM";
    case FFS_TYPE_DRIVER:    return "DRIVER";
    case FFS_TYPE_APPLICATION: return "APPLICATION";
    case FFS_TYPE_MM:        return "MM_MODULE";
    case FFS_TYPE_FIRMWARE_VOLUME_IMAGE: return "FV_IMAGE";
    case FFS_TYPE_PAD:       return "PAD";
    default:                 return "UNKNOWN";
    }
}

/* ============================================================================
 * Firmware Volume Scanning - L3/L5
 *
 * Scans a Firmware Volume and builds an in-memory catalog of all valid
 * FFS files. The catalog supports O(1) GUID lookup for the DXE
 * dispatcher's DEPEX resolution algorithm.
 *
 * L3: The catalog is the primary data structure for the DXE dispatcher.
 *     It enables the dispatcher to find drivers by GUID without
 *     re-scanning the FV on each dependency check.
 *
 * L5: Linear scan algorithm. Each FFS file header is at an 8-byte aligned
 *     offset. File size from header tells us where the next file begins.
 *     Complexity: O(n) where n = number of files in FV.
 * ============================================================================ */

uint32_t fv_scan_files(FVCatalog *catalog, const uint8_t *fv_base)
{
    if (!catalog || !fv_base) return 0;

    memset(catalog, 0, sizeof(FVCatalog));

    /* Parse FV header */
    if (!fv_parse_header(&catalog->fv_header, fv_base)) {
        printf("[FV] Invalid FV header at %p\n", (const void *)fv_base);
        return 0;
    }

    catalog->fv_base = (uint64_t)(uintptr_t)fv_base;
    catalog->file_count = 0;
    catalog->parsed = false;

    /* FFS files start at offset 0 within the FV (after header) */
    uint32_t offset = catalog->fv_header.header_length;
    uint64_t fv_end = catalog->fv_header.fv_length;

    /* Align offset to 8-byte boundary (FFS alignment requirement) */
    if (offset % 8 != 0) offset += 8 - (offset % 8);

    while (offset + FFS_FILE_HEADER_SIZE <= fv_end
           && catalog->file_count < FV_MAX_FILES) {

        const FFSFileHeader *ffs = (const FFSFileHeader *)(fv_base + offset);

        /* Check for end-of-volume marker (all 0xFF = erased flash) */
        if (ffs->state == FFS_STATE_ERASED) {
            offset += 8;  /* Skip pad */
            continue;
        }

        if (ffs_is_valid_file(ffs)) {
            FVFileEntry *entry = &catalog->files[catalog->file_count];
            entry->file_guid   = ffs->name;
            entry->file_type   = ffs->type;
            entry->file_offset = offset;
            entry->file_size   = ffs_get_file_size(ffs);
            entry->state       = ffs->state;
            entry->present     = true;
            catalog->file_count++;
        }

        /* Advance to next file */
        uint32_t fsize = ffs_get_file_size(ffs);
        if (fsize < FFS_FILE_HEADER_SIZE) break;  /* Invalid size */

        offset += fsize;
        if (offset % 8 != 0) offset += 8 - (offset % 8);
    }

    catalog->parsed = true;
    return catalog->file_count;
}

/* ============================================================================
 * FV File Lookup - L5
 *
 * O(n) linear scan through the catalog. For production firmware, a hash
 * table keyed on GUID would provide O(1) lookup for DEPEX resolution.
 * Complexity: O(n). Memory: O(1).
 * ============================================================================ */

int32_t fv_find_by_guid(const FVCatalog *catalog, const EFI_GUID *guid)
{
    if (!catalog || !guid) return -1;

    for (uint32_t i = 0; i < catalog->file_count; i++) {
        if (catalog->files[i].present
            && guid_equal(&catalog->files[i].file_guid, guid)) {
            return (int32_t)i;
        }
    }
    return -1;
}

uint32_t fv_find_by_type(const FVCatalog *catalog, uint8_t file_type,
                         uint32_t *indices, uint32_t max_results)
{
    if (!catalog || !indices || max_results == 0) return 0;

    uint32_t found = 0;
    for (uint32_t i = 0; i < catalog->file_count && found < max_results; i++) {
        if (catalog->files[i].present
            && catalog->files[i].file_type == file_type) {
            indices[found] = i;
            found++;
        }
    }
    return found;
}

/* ============================================================================
 * FV Debug Print Utilities
 * ============================================================================ */

void fv_print_header(const FVHeader *fv_hdr)
{
    if (!fv_hdr) return;

    printf("  FV Header:\n");
    printf("    Signature:    0x%08X (%s)\n", fv_hdr->signature,
           fv_hdr->signature == FV_SIGNATURE ? "OK" : "INVALID");
    printf("    Length:       %llu bytes (%.2f MB)\n",
           (unsigned long long)fv_hdr->fv_length,
           fv_hdr->fv_length / (1024.0 * 1024.0));
    printf("    Header Len:   %u bytes\n", fv_hdr->header_length);
    printf("    Attributes:   0x%08X\n", fv_hdr->attributes);
    printf("    Revision:     %u\n", fv_hdr->revision);
    printf("    Checksum:     0x%04X %s\n", fv_hdr->checksum,
           fv_is_valid(fv_hdr) ? "(VALID)" : "(INVALID)");
}

void fv_print_catalog(const FVCatalog *catalog)
{
    if (!catalog || !catalog->parsed) return;

    printf("  FV Catalog: %u files\n", catalog->file_count);
    for (uint32_t i = 0; i < catalog->file_count; i++) {
        const FVFileEntry *e = &catalog->files[i];
        printf("    [%2u] ", i);
        guid_print(&e->file_guid);
        printf(" type=%s off=0x%X size=%u\n",
               ffs_type_name(e->file_type), e->file_offset, e->file_size);
    }
}

/* ============================================================================
 * SHA-256 Implementation - L5/L8
 *
 * L5: SHA-256 hash algorithm per FIPS PUB 180-4 (Secure Hash Standard).
 * L8: Used for Measured Boot - measurements extended into TPM PCRs.
 *
 * This implementation uses the standard FIPS 180-4 algorithm with
 * 64 rounds per 512-bit block, big-endian message padding, and
 * 32-byte digest output. Process variable assignments follow the
 * standard naming: S0=Sigma0, S1=Sigma1, s0=sigma0, s1=sigma1.
 * ============================================================================ */

static const uint32_t sha256_h[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0c25, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define SHA256_CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHA256_ROR(x,n)   (((x) >> (n)) | ((x) << (32 - (n))))
#define SHA256_S0(x)      (SHA256_ROR(x, 2) ^ SHA256_ROR(x,13) ^ SHA256_ROR(x,22))
#define SHA256_S1(x)      (SHA256_ROR(x, 6) ^ SHA256_ROR(x,11) ^ SHA256_ROR(x,25))
#define SHA256_s0(x)      (SHA256_ROR(x, 7) ^ SHA256_ROR(x,18) ^ ((x) >> 3))
#define SHA256_s1(x)      (SHA256_ROR(x,17) ^ SHA256_ROR(x,19) ^ ((x) >> 10))

void sha256_init(SHA256Context *ctx)
{
    if (!ctx) return;
    ctx->bit_count = 0;
    ctx->buffer_offset = 0;
    for (int i = 0; i < 8; i++) ctx->state[i] = sha256_h[i];
    memset(ctx->buffer, 0, SHA256_BLOCK_SIZE);
}

static void sha256_transform(SHA256Context *ctx, const uint8_t data[SHA256_BLOCK_SIZE])
{
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, w[64];

    for (i = 0, j = 0; i < 16; i++, j += 4)
        w[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j+1] << 16)
             | ((uint32_t)data[j+2] << 8)  | ((uint32_t)data[j+3]);

    for (i = 16; i < 64; i++)
        w[i] = SHA256_s1(w[i-2]) + w[i-7] + SHA256_s0(w[i-15]) + w[i-16];

    a = ctx->state[0]; b = ctx->state[1];
    c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + SHA256_S1(e) + SHA256_CH(e,f,g) + sha256_k[i] + w[i];
        t2 = SHA256_S0(a) + SHA256_MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b;
    ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

void sha256_update(SHA256Context *ctx, const uint8_t *data, uint32_t length)
{
    if (!ctx || !data || length == 0) return;

    ctx->bit_count += (uint64_t)length * 8;

    for (uint32_t i = 0; i < length; i++) {
        ctx->buffer[ctx->buffer_offset++] = data[i];
        if (ctx->buffer_offset == SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, ctx->buffer);
            ctx->buffer_offset = 0;
        }
    }
}

void sha256_final(SHA256Context *ctx, uint8_t digest[SHA256_DIGEST_SIZE])
{
    if (!ctx || !digest) return;

    uint32_t i = ctx->buffer_offset;

    /* Padding: 0x80 then zeros, then 64-bit big-endian length */
    ctx->buffer[i++] = 0x80;
    if (i > 56) {
        while (i < SHA256_BLOCK_SIZE) ctx->buffer[i++] = 0;
        sha256_transform(ctx, ctx->buffer);
        i = 0;
    }
    while (i < 56) ctx->buffer[i++] = 0;

    uint64_t bits = ctx->bit_count;
    for (i = 0; i < 8; i++) {
        ctx->buffer[56 + i] = (uint8_t)(bits >> (56 - i * 8));
    }

    sha256_transform(ctx, ctx->buffer);

    /* Output digest in big-endian */
    for (i = 0; i < 8; i++) {
        digest[i*4]   = (uint8_t)(ctx->state[i] >> 24);
        digest[i*4+1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i*4+2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i*4+3] = (uint8_t)(ctx->state[i]);
    }
}

void sha256_hash(const uint8_t *data, uint32_t length, uint8_t digest[SHA256_DIGEST_SIZE])
{
    SHA256Context ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, length);
    sha256_final(&ctx, digest);
}

/* ============================================================================
 * Firmware Volume Integrity Verification - L8
 *
 * L8: Advanced topic - Measured Boot.
 * In UEFI Secure Boot / Measured Boot, each Firmware Volume's SHA-256
 * hash is computed and extended into a TPM PCR (Platform Configuration
 * Register). This creates a tamper-evident log of all firmware code
 * executed during boot, enabling remote attestation.
 *
 * The TPM PCR extension formula: PCR_new = SHA-256(PCR_old || measurement)
 *
 * This function computes the measurement (SHA-256 hash) of the FV
 * content, which would then be extended into PCR 0 (SRTM BIOS measurement).
 *
 * Reference: TCG PC Client Platform Firmware Profile Specification
 * ============================================================================ */

bool fv_verify_integrity(const uint8_t *fv_data, uint64_t fv_length)
{
    if (!fv_data || fv_length < FV_HEADER_SIZE) return false;

    uint8_t digest[SHA256_DIGEST_SIZE];

    /* Compute SHA-256 over entire FV content */
    sha256_hash(fv_data, (uint32_t)fv_length, digest);

    /* In a real implementation, this digest would be:
     * 1. Extended into PCR 0 of the TPM
     * 2. Logged in the TCG Event Log for the OS to verify
     * Here we just report the measurement */

    printf("[FV:INTEGRITY] SHA-256 measurement: ");
    for (int i = 0; i < SHA256_DIGEST_SIZE; i++) {
        printf("%02x", digest[i]);
    }
    printf("\n");

    return true;
}
