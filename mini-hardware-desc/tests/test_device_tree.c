#include "device_tree.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void w32(uint8_t *b, size_t off, uint32_t v) {
    b[off]=(v>>24)&0xFF; b[off+1]=(v>>16)&0xFF;
    b[off+2]=(v>>8)&0xFF; b[off+3]=v&0xFF;
}

static void build_minimal_dtb(uint8_t **dtb_out, size_t *size_out) {
    /* Strings block */
    char sbuf[512]; memset(sbuf, 0, sizeof(sbuf));
    char *sp = sbuf;
#define SO(s) ((uint32_t)((s)-sbuf))
    strcpy(sp, "#address-cells");     uint32_t o_ac = SO(sp); sp+=15;
    strcpy(sp, "#size-cells");        uint32_t o_sc = SO(sp); sp+=12;
    strcpy(sp, "compatible");         uint32_t o_cm = SO(sp); sp+=11;
    strcpy(sp, "reg");                uint32_t o_rg = SO(sp); sp+=4;
    strcpy(sp, "interrupts");         uint32_t o_ir = SO(sp); sp+=12;
    strcpy(sp, "phandle");            uint32_t o_ph = SO(sp); sp+=8;
    strcpy(sp, "interrupt-controller"); uint32_t o_ic = SO(sp); sp+=21;
    strcpy(sp, "#interrupt-cells");   uint32_t o_icl = SO(sp); sp+=18;
    strcpy(sp, "ns16550a");           uint32_t o_ns = SO(sp); sp+=9;
    strcpy(sp, "simple-bus");         uint32_t o_sb = SO(sp); sp+=11;
    strcpy(sp, "arm,gic-400");        uint32_t o_gi = SO(sp); sp+=13;
#undef SO
    size_t ssz = (size_t)(sp - sbuf);
    size_t ssz_a = (ssz + 3) & ~(size_t)3;

    /* Structure block */
    uint8_t db[2048];
    size_t dp = 0;

    w32(db,dp,FDT_BEGIN_NODE); dp+=4; db[dp]='\0'; dp+=4;
    /* #address-cells = <1> */
    w32(db,dp,FDT_PROP); dp+=4; w32(db,dp,4); dp+=4; w32(db,dp,o_ac); dp+=4; w32(db,dp,1); dp+=4;
    /* #size-cells = <1> */
    w32(db,dp,FDT_PROP); dp+=4; w32(db,dp,4); dp+=4; w32(db,dp,o_sc); dp+=4; w32(db,dp,1); dp+=4;

    /* soc node */
    w32(db,dp,FDT_BEGIN_NODE); dp+=4; strcpy((char*)(db+dp),"soc"); dp+=4;
    /* #address-cells=<1>, #size-cells=<1>, compatible="simple-bus" */
    w32(db,dp,FDT_PROP); dp+=4; w32(db,dp,4); dp+=4; w32(db,dp,o_ac); dp+=4; w32(db,dp,1); dp+=4;
    w32(db,dp,FDT_PROP); dp+=4; w32(db,dp,4); dp+=4; w32(db,dp,o_sc); dp+=4; w32(db,dp,1); dp+=4;
    uint32_t sbl = (uint32_t)(strlen("simple-bus")+1);
    w32(db,dp,FDT_PROP); dp+=4; w32(db,dp,sbl); dp+=4; w32(db,dp,o_cm); dp+=4;
    memcpy(db+dp,"simple-bus",sbl); dp+=sbl; dp=(dp+3)&~(size_t)3;

    /* serial@1000 */
    w32(db,dp,FDT_BEGIN_NODE); dp+=4; strcpy((char*)(db+dp),"serial@1000"); dp+=12;
    uint32_t nsl = (uint32_t)(strlen("ns16550a")+1);
    w32(db,dp,FDT_PROP); dp+=4; w32(db,dp,nsl); dp+=4; w32(db,dp,o_cm); dp+=4;
    memcpy(db+dp,"ns16550a",nsl); dp+=nsl; dp=(dp+3)&~(size_t)3;
    /* reg = <0x1000 0x100> */
    w32(db,dp,FDT_PROP); dp+=4; w32(db,dp,8); dp+=4; w32(db,dp,o_rg); dp+=4;
    w32(db,dp,0x1000); dp+=4; w32(db,dp,0x100); dp+=4;
    /* interrupts = <1> */
    w32(db,dp,FDT_PROP); dp+=4; w32(db,dp,4); dp+=4; w32(db,dp,o_ir); dp+=4; w32(db,dp,1); dp+=4;
    /* phandle = <1> */
    w32(db,dp,FDT_PROP); dp+=4; w32(db,dp,4); dp+=4; w32(db,dp,o_ph); dp+=4; w32(db,dp,1); dp+=4;
    w32(db,dp,FDT_END_NODE); dp+=4;

    /* interrupt-controller (21 bytes incl null, padded to 24) */
    w32(db,dp,FDT_BEGIN_NODE); dp+=4; strcpy((char*)(db+dp),"interrupt-controller"); dp+=21; dp=(dp+3)&~(size_t)3;
    uint32_t gil = (uint32_t)(strlen("arm,gic-400")+1);
    w32(db,dp,FDT_PROP); dp+=4; w32(db,dp,gil); dp+=4; w32(db,dp,o_cm); dp+=4;
    memcpy(db+dp,"arm,gic-400",gil); dp+=gil; dp=(dp+3)&~(size_t)3;
    /* #interrupt-cells = <2> */
    w32(db,dp,FDT_PROP); dp+=4; w32(db,dp,4); dp+=4; w32(db,dp,o_icl); dp+=4; w32(db,dp,2); dp+=4;
    /* interrupt-controller flag */
    w32(db,dp,FDT_PROP); dp+=4; w32(db,dp,0); dp+=4; w32(db,dp,o_ic); dp+=4;
    /* phandle = <2> */
    w32(db,dp,FDT_PROP); dp+=4; w32(db,dp,4); dp+=4; w32(db,dp,o_ph); dp+=4; w32(db,dp,2); dp+=4;
    w32(db,dp,FDT_END_NODE); dp+=4;

    w32(db,dp,FDT_END_NODE); dp+=4;  /* soc */
    w32(db,dp,FDT_END_NODE); dp+=4;  /* root */
    w32(db,dp,FDT_END); dp+=4;
    size_t st_sz = dp;

    /* Layout: header(40) + rsvmap(48) + struct + strings */
    size_t rsv_off = 40;
    size_t st_off = rsv_off + 48;
    size_t str_off = st_off + st_sz;
    size_t total = str_off + ssz_a;

    uint8_t *dtb = calloc(1, total + 16);
    assert(dtb);

    FDTHeader *h = (FDTHeader*)dtb;
    h->magic             = fdt_bswap32(FDT_MAGIC);
    h->totalsize         = fdt_bswap32((uint32_t)total);
    h->off_dt_struct     = fdt_bswap32((uint32_t)st_off);
    h->off_dt_strings    = fdt_bswap32((uint32_t)str_off);
    h->off_mem_rsvmap    = fdt_bswap32((uint32_t)rsv_off);
    h->version           = fdt_bswap32(FDT_SUPPORTED_VERSION);
    h->last_comp_version = fdt_bswap32(FDT_COMPAT_VERSION);
    h->boot_cpuid_phys   = fdt_bswap32(0);
    h->size_dt_strings   = fdt_bswap32((uint32_t)ssz_a);
    h->size_dt_struct    = fdt_bswap32((uint32_t)st_sz);

    /* Reservation block: 2 entries */
    uint64_t rsv[] = {fdt_read_u64((uint8_t*)&(uint64_t){fdt_bswap32(2)}),
                      0x10000000ULL,  /* addr=0x80000000 */
                      fdt_read_u64((uint8_t*)&(uint64_t){fdt_bswap32(3)}),
                      0x04000000ULL,  /* addr=0xC0000000 */
                      0, 0};
    (void)rsv; /* skip complex rsv write - just zero it */
    memset(dtb + rsv_off, 0, 48);
    /* Actually write decent values */
    uint8_t *rp = dtb + rsv_off;
    w32(rp, 0, fdt_bswap32(2)); w32(rp, 4, fdt_bswap32(0));
    w32(rp, 8, fdt_bswap32(0x10000000));
    w32(rp, 12, fdt_bswap32(0)); /* size=0 -> not valid, but just testing parsing */

    /* Copy struct and strings */
    memcpy(dtb + st_off, db, st_sz);
    memcpy(dtb + str_off, sbuf, ssz);

    *dtb_out = dtb;
    *size_out = total;
}

static void test_parse(void) {
    printf("  [L1] parse ... ");
    uint8_t *d; size_t s; build_minimal_dtb(&d, &s);
    FDTTree t;
    assert(fdt_parse(&t, d, s));
    assert(t.parsed);
    assert(t.root != NULL);
    assert(!fdt_parse(NULL, NULL, 0));
    printf("OK\n"); fdt_free_tree(&t); free(d);
}

static void test_nav(void) {
    printf("  [L2] nav ... ");
    uint8_t *d; size_t s; build_minimal_dtb(&d, &s);
    FDTTree t; fdt_parse(&t, d, s);
    assert(fdt_find_node_by_path(&t, "/"));
    FDTNode *soc = fdt_find_node_by_path(&t, "/soc");
    assert(soc && !strcmp(soc->name, "soc"));
    FDTNode *uart = fdt_find_node_by_path(&t, "/soc/serial@1000");
    assert(uart && !strcmp(uart->name, "serial@1000"));
    assert(!fdt_find_node_by_path(&t, "/nonexistent"));
    FDTNode *c = fdt_find_compatible(t.root, "ns16550a");
    assert(c && !strcmp(c->name, "serial@1000"));
    assert(!fdt_find_compatible(t.root, "bad"));
    assert(fdt_count_children(t.root) >= 1);
    FDTNode *fc = fdt_first_child(t.root);
    assert(fc && fdt_get_parent(fc) == t.root);
    printf("OK\n"); fdt_free_tree(&t); free(d);
}

static void test_props(void) {
    printf("  [L2] props ... ");
    uint8_t *d; size_t s; build_minimal_dtb(&d, &s);
    FDTTree t; fdt_parse(&t, d, s);
    FDTNode *soc = fdt_find_node_by_path(&t, "/soc");
    uint32_t v;
    assert(fdt_read_prop_u32(soc, "#address-cells", &v) && v == 1);
    char buf[64];
    FDTNode *uart = fdt_find_node_by_path(&t, "/soc/serial@1000");
    assert(fdt_read_prop_string(uart, "compatible", buf, sizeof(buf)));
    assert(!strcmp(buf, "ns16550a"));
    size_t pl;
    assert(fdt_get_prop_value(uart, "reg", &pl) && pl == 8);
    printf("OK\n"); fdt_free_tree(&t); free(d);
}

static void test_phandle(void) {
    printf("  [L2] phandle ... ");
    uint8_t *d; size_t s; build_minimal_dtb(&d, &s);
    FDTTree t; fdt_parse(&t, d, s);
    FDTNode *n = fdt_find_node_by_phandle(&t, 1);
    assert(n && !strcmp(n->name, "serial@1000") && fdt_get_phandle(n) == 1);
    n = fdt_find_node_by_phandle(&t, 2);
    assert(n && !strcmp(n->name, "interrupt-controller"));
    assert(!fdt_find_node_by_phandle(&t, 999));
    printf("OK\n"); fdt_free_tree(&t); free(d);
}

static void test_validate(void) {
    printf("  [L4] validate ... ");
    uint8_t *d; size_t s; build_minimal_dtb(&d, &s);
    FDTTree t; fdt_parse(&t, d, s);
    assert(fdt_validate(&t));
    assert(!fdt_validate(NULL));
    printf("OK\n"); fdt_free_tree(&t); free(d);
}

static void test_addr(void) {
    printf("  [L5] addr xlat ... ");
    uint8_t *d; size_t s; build_minimal_dtb(&d, &s);
    FDTTree t; fdt_parse(&t, d, s);
    uint64_t pa;
    FDTNode *uart = fdt_find_node_by_path(&t, "/soc/serial@1000");
    assert(fdt_translate_address(&t, uart, 0x1000, &pa) && pa == 0x1000);
    assert(fdt_get_address_cells(t.root) == 1);
    assert(fdt_get_size_cells(t.root) == 1);
    printf("OK\n"); fdt_free_tree(&t); free(d);
}

static void test_endian(void) {
    printf("  [L1] endian ... ");
    assert(fdt_bswap32(0x12345678) == 0x78563412);
    uint8_t b[8]={0x12,0x34,0x56,0x78,0x9A,0xBC,0xDE,0xF0};
    assert(fdt_read_u32(b) == 0x12345678);
    assert(fdt_read_u64(b) == 0x123456789ABCDEF0ULL);
    printf("OK\n");
}

int main(void) {
    printf("=== FDT Tests ===\n\n");
    test_endian(); test_parse(); test_nav(); test_props();
    test_phandle(); test_validate(); test_addr();
    printf("\n=== All FDT tests passed ===\n");
    return 0;
}
