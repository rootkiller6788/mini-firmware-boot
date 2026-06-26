#ifndef BOOT_COMPRESS_H
#define BOOT_COMPRESS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * L2: Decompression concept - bzImage is a gzip-compressed kernel.
 * L4: DEFLATE standard (RFC 1951), gzip format (RFC 1952).
 * L5: Huffman decoding + LZ77 decompression algorithm.
 */

#define GZIP_MAGIC1      0x1F
#define GZIP_MAGIC2      0x8B
#define GZIP_CM_DEFLATE  0x08
#define GZIP_FLAG_FTEXT  0x01
#define GZIP_FLAG_FHCRC  0x02
#define GZIP_FLAG_FEXTRA 0x04
#define GZIP_FLAG_FNAME  0x08
#define GZIP_FLAG_FCOMM  0x10

#define COMPRESS_WINDOW  32768
#define COMPRESS_MAX_MATCH 258
#define COMPRESS_MIN_MATCH 3
#define COMPRESS_HASH_SIZE 32768

#define HUFFMAN_MAX_BITS  15
#define HUFFMAN_MAX_CODES 288
#define HUFFMAN_DIST_CODES 32
#define HUFFMAN_LEN_ORDER  19

/* LZ77 output token */
typedef struct {
    uint16_t length;
    uint16_t distance;
    uint8_t  literal;
    bool     is_literal;
} LZ77Token;

/* gzip file header (RFC 1952 Section 2.2) */
typedef struct {
    uint8_t  id1;
    uint8_t  id2;
    uint8_t  cm;
    uint8_t  flg;
    uint32_t mtime;
    uint8_t  xfl;
    uint8_t  os;
    uint16_t xlen;
    uint8_t  *fextra;
    uint8_t  *fname;
    uint8_t  *fcomment;
    uint16_t hcrc;
} GzipHeader;

/* Huffman tree node (canonical Huffman for DEFLATE) */
typedef struct {
    int16_t  symbol;
    uint16_t bits;
    uint16_t code;
} HuffmanCode;

/* Decompression context */
typedef struct {
    uint8_t  *input;
    uint32_t  input_size;
    uint32_t  input_pos;
    uint32_t  bit_buf;
    int       bit_count;

    uint8_t  *output;
    uint32_t  output_size;
    uint32_t  output_pos;

    HuffmanCode lit_table[HUFFMAN_MAX_CODES];
    uint16_t    lit_count;
    HuffmanCode dist_table[HUFFMAN_DIST_CODES];
    uint16_t    dist_count;

    /* DEFLATE length and distance base tables */
    uint16_t length_base[29];
    uint8_t  length_extra[29];
    uint16_t dist_base[30];
    uint8_t  dist_extra[30];
} DecompressContext;

/* ── API ────────────────────────────────────────────────── */
void  decompress_init(DecompressContext *ctx);
bool  gzip_parse_header(DecompressContext *ctx);
bool  gzip_validate_header(const GzipHeader *hdr);
bool  deflate_decompress(DecompressContext *ctx);
bool  decompress_bzimage(const uint8_t *compressed, uint32_t comp_size,
                         uint8_t *output, uint32_t *output_size);
void  gzip_header_print(const GzipHeader *hdr);

/* LZ77 explicit decompression (educational, for non-DEFLATE data) */
bool  lz77_decompress(const LZ77Token *tokens, uint32_t token_count,
                      uint8_t *output, uint32_t *output_size, uint32_t max_out);
bool  rle_decompress(const uint8_t *input, uint32_t input_size,
                     uint8_t *output, uint32_t *output_size, uint32_t max_out);

#endif