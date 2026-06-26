#include "acpi_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Build fake FADT */
static void build_fake_fadt(uint8_t **out, uint32_t *len) {
    FADT *f = calloc(1, sizeof(FADT));
    assert(f);
    memcpy(f->header.signature, "FACP", 4);
    f->header.length = sizeof(FADT);
    f->header.revision = 6;
    f->preferred_pm_profile = ACPI_PM_PROFILE_DESKTOP;
    f->sci_int = 9;
    f->smi_cmd = 0xB2;
    f->acpi_enable = 0xA0;
    f->acpi_disable = 0xA1;
    f->pm_tmr_blk = 0x408;
    f->pm_tmr_len = 4;
    f->iapc_boot_arch = ACPI_FADT_LEGACY_DEVICES | ACPI_FADT_8042;
    f->flags = ACPI_FADT_WBINVD | ACPI_FADT_RESET_REG_SUP;
    f->reset_reg.address = 0xCF9;
    f->reset_value = 0x06;
    f->dsdt = 0x10000;
    f->x_dsdt = 0;
    /* Compute checksum */
    uint8_t sum = 0;
    for (uint32_t i = 0; i < sizeof(FADT); i++) sum += ((uint8_t*)f)[i];
    f->header.checksum = (uint8_t)(256 - (sum - f->header.checksum));
    *out = (uint8_t*)f;
    *len = sizeof(FADT);
}

/* Build fake MADT */
static void build_fake_madt(uint8_t **out, uint32_t *len) {
    size_t sz = sizeof(MADT) + sizeof(MADTLAPIC) * 2 + sizeof(MADTIOAPIC) + sizeof(MADTIntSrcOverride);
    uint8_t *buf = calloc(1, sz);
    assert(buf);
    MADT *m = (MADT*)buf;
    memcpy(m->header.signature, "APIC", 4);
    m->header.length = (uint32_t)sz;
    m->header.revision = 4;
    m->lapic_address = 0xFEE00000;
    m->flags = 1;
    /* LAPIC 0 */
    size_t off = sizeof(MADT);
    MADTLAPIC *l0 = (MADTLAPIC*)(buf + off);
    l0->hdr.type = MADT_TYPE_LAPIC; l0->hdr.length = sizeof(MADTLAPIC);
    l0->acpi_processor_id = 0; l0->apic_id = 0; l0->flags = 1;
    off += sizeof(MADTLAPIC);
    /* LAPIC 1 */
    MADTLAPIC *l1 = (MADTLAPIC*)(buf + off);
    l1->hdr.type = MADT_TYPE_LAPIC; l1->hdr.length = sizeof(MADTLAPIC);
    l1->acpi_processor_id = 1; l1->apic_id = 2; l1->flags = 1;
    off += sizeof(MADTLAPIC);
    /* IOAPIC */
    MADTIOAPIC *io = (MADTIOAPIC*)(buf + off);
    io->hdr.type = MADT_TYPE_IOAPIC; io->hdr.length = sizeof(MADTIOAPIC);
    io->io_apic_id = 1; io->io_apic_address = 0xFEC00000; io->global_system_interrupt_base = 0;
    off += sizeof(MADTIOAPIC);
    /* IRQ override: IRQ0 -> GSI2 */
    MADTIntSrcOverride *ov = (MADTIntSrcOverride*)(buf + off);
    ov->hdr.type = MADT_TYPE_INT_SRC_OVERRIDE; ov->hdr.length = sizeof(MADTIntSrcOverride);
    ov->bus_source = 0; ov->irq_source = 0; ov->global_system_interrupt = 2; ov->flags = 0;
    /* checksum */
    uint8_t sum = 0;
    for (uint32_t i = 0; i < m->header.length; i++) sum += buf[i];
    m->header.checksum = (uint8_t)(256 - (sum - m->header.checksum));
    *out = buf; *len = (uint32_t)sz;
}

/* Build fake MCFG */
static void build_fake_mcfg(uint8_t **out, uint32_t *len) {
    size_t sz = sizeof(MCFG) + sizeof(MCFGEntry);
    uint8_t *buf = calloc(1, sz);
    MCFG *m = (MCFG*)buf;
    memcpy(m->header.signature, "MCFG", 4);
    m->header.length = (uint32_t)sz; m->header.revision = 1;
    m->reserved = 0;
    m->entries[0].base_address = 0xE0000000ULL;
    m->entries[0].pci_segment_group = 0;
    m->entries[0].start_bus = 0; m->entries[0].end_bus = 255;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < (uint32_t)sz; i++) sum += buf[i];
    m->header.checksum = (uint8_t)(256 - (sum - m->header.checksum));
    *out = buf; *len = (uint32_t)sz;
}

/* Build fake HPET */
static void build_fake_hpet(uint8_t **out, uint32_t *len) {
    HPET *h = calloc(1, sizeof(HPET));
    memcpy(h->header.signature, "HPET", 4);
    h->header.length = sizeof(HPET); h->header.revision = 1;
    h->event_timer_block_id = 0x8086A201;
    h->base_address.address = 0xFED00000ULL;
    h->hpet_number = 0;
    h->min_clock_tick = 14318; /* 14.318 us period */
    h->page_protection = 0;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < sizeof(HPET); i++) sum += ((uint8_t*)h)[i];
    h->header.checksum = (uint8_t)(256 - (sum - h->header.checksum));
    *out = (uint8_t*)h; *len = sizeof(HPET);
}

static void test_checksum(void) {
    printf("  [L4] checksum ... ");
    uint8_t data[] = {0x01, 0x02, 0x03, 0xFA}; /* sum=256 -> 0 */
    assert(acpi_validate_checksum(data, 4));
    data[3] = 0xFB;
    assert(!acpi_validate_checksum(data, 4));
    assert(!acpi_validate_checksum(NULL, 10));
    assert(!acpi_validate_checksum(data, 0));
    printf("OK\n");
}

static void test_fadt(void) {
    printf("  [L3] FADT ... ");
    uint8_t *raw; uint32_t len; build_fake_fadt(&raw, &len);
    FADTInfo info;
    assert(acpi_parse_fadt(raw, len, &info));
    assert(info.has_fadt && info.pm_profile == ACPI_PM_PROFILE_DESKTOP);
    assert(info.sci_int == 9 && info.smi_cmd == 0xB2);
    assert(info.pm_tmr_blk == 0x408 && info.pm_tmr_len == 4);
    assert(info.dsdt_address == 0x10000);
    assert(info.has_reset_reg && info.reset_address == 0xCF9);
    assert(info.iapc_boot_arch & ACPI_FADT_8042);
    assert(!info.hw_reduced);
    /* Bad data */
    assert(!acpi_parse_fadt(NULL, len, &info));
    assert(!acpi_parse_fadt(raw, 4, &info));
    assert(!acpi_parse_fadt(raw, len, NULL));
    /* PM profile names */
    assert(!strcmp(acpi_pm_profile_name(ACPI_PM_PROFILE_DESKTOP), "Desktop"));
    assert(!strcmp(acpi_pm_profile_name(255), "Reserved"));
    printf("OK\n");
    free(raw);
}

static void test_madt(void) {
    printf("  [L5] MADT ... ");
    uint8_t *raw; uint32_t len; build_fake_madt(&raw, &len);
    MADTInfo info;
    assert(acpi_parse_madt(raw, len, &info));
    assert(info.has_madt && info.lapic_address == 0xFEE00000);
    assert(info.pic_cascade);
    assert(info.lapic_count == 2);
    assert(info.lapics[0].apic_id == 0 && (info.lapics[0].flags & 1));
    assert(info.lapics[1].apic_id == 2);
    assert(info.ioapic_count == 1);
    assert(info.ioapics[0].io_apic_address == 0xFEC00000);
    assert(info.override_count == 1);
    assert(info.overrides[0].irq == 0 && info.overrides[0].gsi == 2);
    /* IRQ to GSI: default identity */
    assert(acpi_madt_irq_to_gsi(&info, 8) == 8);
    /* IRQ 0 remapped to GSI 2 */
    assert(acpi_madt_irq_to_gsi(&info, 0) == 2);
    /* Null guards */
    assert(acpi_madt_irq_to_gsi(NULL, 0) == 0);
    MADTInfo empty; memset(&empty, 0, sizeof(empty));
    assert(acpi_madt_irq_to_gsi(&empty, 5) == 5);
    printf("OK\n");
    free(raw);
}

static void test_mcfg(void) {
    printf("  [L2] MCFG ... ");
    uint8_t *raw; uint32_t len; build_fake_mcfg(&raw, &len);
    MCFGInfo info;
    assert(acpi_parse_mcfg(raw, len, &info));
    assert(info.has_mcfg && info.entry_count == 1);
    assert(info.buses[0].base == 0xE0000000ULL);
    assert(info.buses[0].bus_start == 0 && info.buses[0].bus_end == 255);
    /* ECAM address: bus=0, dev=1, func=0 */
    uint64_t ecam = acpi_mcfg_get_ecam_base(&info, 0, 1, 0);
    assert(ecam == 0xE0000000ULL + (1 << 15));
    /* Out of range */
    assert(acpi_mcfg_get_ecam_base(&info, 255, 31, 7) != 0);
    assert(!acpi_parse_mcfg(NULL, len, &info));
    printf("OK\n");
    free(raw);
}

static void test_hpet(void) {
    printf("  [L2] HPET ... ");
    uint8_t *raw; uint32_t len; build_fake_hpet(&raw, &len);
    HPETInfo info;
    assert(acpi_parse_hpet(raw, len, &info));
    assert(info.has_hpet && info.min_clock_tick_fs == 14318);
    assert(info.base_address == 0xFED00000ULL);
    /* ms_to_ticks: 1ms at 14318 fs period -> ~69.8M ticks */
    uint64_t ticks = acpi_hpet_ms_to_ticks(&info, 1);
    assert(ticks > 60000000 && ticks < 80000000);
    assert(acpi_hpet_ms_to_ticks(NULL, 1) == 0);
    HPETInfo e2; memset(&e2, 0, sizeof(e2));
    assert(acpi_hpet_ms_to_ticks(&e2, 1) == 0);
    printf("OK\n"); free(raw);
}

static void test_table_type_name(void) {
    printf("  [L1] table names ... ");
    assert(strstr(acpi_table_type_name("FACP"), "FADT"));
    assert(strstr(acpi_table_type_name("APIC"), "MADT"));
    assert(strstr(acpi_table_type_name("MCFG"), "PCI"));
    assert(!strcmp(acpi_table_type_name(NULL), "Unknown"));
    printf("OK\n");
}

int main(void) {
    printf("=== ACPI Tests ===\n\n");
    test_checksum(); test_table_type_name();
    test_fadt(); test_madt(); test_mcfg(); test_hpet();
    printf("\n=== All ACPI tests passed ===\n");
    return 0;
}
