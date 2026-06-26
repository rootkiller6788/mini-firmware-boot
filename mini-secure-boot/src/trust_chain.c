#include "trust_chain.h"
#include "signature_verify.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *boot_component_names[] = {
    "SPL", "TPL", "U-Boot", "Linux_Kernel",
    "Initrd", "DT_FIT", "FDT", "UNKNOWN"
};

void trust_chain_init(BootChain *chain)
{
    if (!chain) return;
    memset(chain, 0, sizeof(BootChain));
    chain->boot_allowed = false;
}

bool trust_chain_add_component(BootChain *chain, BootComponent type,
                                const char *name, const uint8_t *data,
                                uint32_t data_size)
{
    if (!chain || !name || !data) return false;
    if (chain->component_count >= TC_MAX_COMPONENTS) return false;
    if (data_size > TC_MAX_COMPONENT_DATA) return false;

    ChainComponent *comp = &chain->components[chain->component_count];
    comp->type = type;
    snprintf(comp->name, TC_MAX_NAME_LEN, "%s", name);
    memcpy(comp->data, data, data_size);
    comp->data_size = data_size;
    comp->present = true;
    comp->status = TC_STATUS_UNVERIFIED;
    chain->component_count++;
    return true;
}

bool trust_chain_verify_component(BootChain *chain, uint32_t idx,
                                   const uint8_t *expected_hash)
{
    if (!chain || !expected_hash) return false;
    if (idx >= chain->component_count) return false;

    ChainComponent *comp = &chain->components[idx];
    if (!comp->present) {
        comp->status = TC_STATUS_MISSING;
        return false;
    }

    uint8_t computed_hash[SHA256_HASH_SIZE_TC];
    sha256_hash(comp->data, comp->data_size, computed_hash);

    if (memcmp(computed_hash, expected_hash, SHA256_HASH_SIZE_TC) == 0) {
        comp->status = TC_STATUS_VERIFIED;
        chain->verified_bitmap |= (1u << idx);
        return true;
    } else {
        comp->status = TC_STATUS_FAILED;
        return false;
    }
}

bool trust_chain_verify_all(BootChain *chain,
                             const uint8_t expected_hashes[][SHA256_HASH_SIZE_TC])
{
    if (!chain || !expected_hashes) return false;
    bool all_ok = true;

    for (uint32_t i = 0; i < chain->component_count; i++) {
        if (!chain->components[i].present) {
            chain->components[i].status = TC_STATUS_MISSING;
            all_ok = false;
            continue;
        }
        chain->verified_bitmap |= (1u << i);
    }

    for (uint32_t i = 0; i < chain->component_count; i++) {
        if (!trust_chain_verify_component(chain, i, expected_hashes[i])) {
            all_ok = false;
        }
    }

    chain->boot_allowed = all_ok;
    return all_ok;
}

bool trust_chain_boot_authorized(const BootChain *chain)
{
    return chain && chain->boot_allowed;
}

void trust_chain_print_status(const BootChain *chain)
{
    if (!chain) return;
    printf("╔══════════════════════════════════════════╗\n");
    printf("║      VERIFIED BOOT CHAIN STATUS          ║\n");
    printf("╠══════════════════════════════════════════╣\n");

    for (uint32_t i = 0; i < chain->component_count; i++) {
        const ChainComponent *c = &chain->components[i];
        const char *type_str = (c->type < BOOT_COMP_COUNT) ?
            boot_component_names[c->type] : "UNKNOWN";
        const char *status_str;
        switch (c->status) {
            case TC_STATUS_VERIFIED:   status_str = "VERIFIED";   break;
            case TC_STATUS_UNVERIFIED: status_str = "UNVERIFIED"; break;
            case TC_STATUS_REVOKED:    status_str = "REVOKED";    break;
            case TC_STATUS_MISSING:    status_str = "MISSING";    break;
            case TC_STATUS_FAILED:     status_str = "FAILED";     break;
            default:                   status_str = "UNKNOWN";    break;
        }

        printf("║ %-10s %-16s %s\n",
               type_str, c->name, status_str);
    }

    printf("╠══════════════════════════════════════════╣\n");
    printf("║ BOOT AUTHORIZED: %-23s ║\n",
           chain->boot_allowed ? "YES" : "NO");
    printf("╚══════════════════════════════════════════╝\n");
}

/* ─── FIT Image ──────────────────────────────────────────────────── */
static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

bool fit_parse(FITImage *fit, const uint8_t *data, uint32_t data_size)
{
    if (!fit || !data || data_size < 40) return false;
    memset(fit, 0, sizeof(FITImage));

    uint32_t magic = read_be32(data);
    if (magic != FIT_MAGIC) return false;

    fit->header.timestamp = (uint64_t)read_be32(data + 4);
    fit->header.image_count = read_be32(data + 8);
    fit->header.config_count = read_be32(data + 12);
    fit->header.timestamp <<= 32;
    fit->header.timestamp |= read_be32(data + 16);

    uint32_t desc_offset = read_be32(data + 20);
    if (desc_offset < 40 || desc_offset >= data_size) return false;
    snprintf(fit->header.description, FIT_MAX_DESC_LEN, "%s", data + desc_offset);

    if (fit->header.image_count > TC_MAX_FIT_IMAGES)
        fit->header.image_count = TC_MAX_FIT_IMAGES;
    if (fit->header.config_count > TC_MAX_FIT_CONFIGS)
        fit->header.config_count = TC_MAX_FIT_CONFIGS;

    fit->total_size = data_size;
    fit->parsed = true;

    for (uint32_t i = 0; i < fit->header.image_count; i++) {
        FITSubImage *img = &fit->images[i];
        snprintf(img->name, TC_MAX_NAME_LEN, "image_%u", i);
        snprintf(img->type, TC_MAX_NAME_LEN, "kernel");
        img->data_size = 1024;
        img->load_address = 0x80000000 + i * 0x1000000;
        img->entry_point = img->load_address;
        snprintf(img->compression, TC_MAX_NAME_LEN, "none");
        sha256_hash(data, data_size < 1024 ? data_size : 1024, img->hash_value);
        img->has_hash = true;
    }

    for (uint32_t i = 0; i < fit->header.config_count; i++) {
        FITConfiguration *cfg = &fit->configs[i];
        snprintf(cfg->description, FIT_MAX_DESC_LEN, "config_%u", i);
        cfg->kernel_idx = 0;
        cfg->fdt_idx = (fit->header.image_count > 1) ? 1 : 0;
        cfg->initrd_idx = (fit->header.image_count > 2) ? 2 : 0;
        snprintf(cfg->signature_type, TC_MAX_NAME_LEN, "rsa2048");
        cfg->sig_len = 0;
        cfg->verified = false;
    }

    return true;
}

bool fit_get_subimage(const FITImage *fit, uint32_t idx, const FITSubImage **out)
{
    if (!fit || !out || !fit->parsed) return false;
    if (idx >= fit->header.image_count) return false;
    *out = &fit->images[idx];
    return true;
}

bool fit_get_config(const FITImage *fit, uint32_t idx, const FITConfiguration **out)
{
    if (!fit || !out || !fit->parsed) return false;
    if (idx >= fit->header.config_count) return false;
    *out = &fit->configs[idx];
    return true;
}

bool fit_verify_config(const FITImage *fit, uint32_t config_idx,
                        const RSAKey *pub_key)
{
    if (!fit || !pub_key || !fit->parsed || config_idx >= fit->header.config_count)
        return false;

    const FITConfiguration *cfg = &fit->configs[config_idx];

    if (cfg->kernel_idx >= fit->header.image_count) return false;
    const FITSubImage *kernel = &fit->images[cfg->kernel_idx];
    if (!kernel->has_hash) return false;

    uint8_t computed[SHA256_HASH_SIZE_TC];
    sha256_hash(kernel->data, kernel->data_size, computed);

    if (cfg->sig_len > 0) {
        if (!rsa_sha256_verify(pub_key, computed, cfg->signature_data, cfg->sig_len))
            return false;
    } else {
        if (memcmp(computed, kernel->hash_value, SHA256_HASH_SIZE_TC) != 0)
            return false;
    }

    return true;
}

bool fit_verify_all(const FITImage *fit, const RSAKey *pub_key)
{
    if (!fit || !pub_key || !fit->parsed) return false;
    for (uint32_t i = 0; i < fit->header.config_count; i++) {
        if (!fit_verify_config(fit, i, pub_key)) return false;
    }
    return true;
}

void fit_print_components(const FITImage *fit)
{
    if (!fit || !fit->parsed) return;
    printf("=== FIT: %s ===\n", fit->header.description);
    printf("Images: %u  Configs: %u\n",
           fit->header.image_count, fit->header.config_count);

    for (uint32_t i = 0; i < fit->header.image_count; i++) {
        const FITSubImage *img = &fit->images[i];
        printf("  [%u] %-16s type=%-10s load=0x%08X size=%u\n",
               i, img->name, img->type, img->load_address, img->data_size);
    }

    for (uint32_t i = 0; i < fit->header.config_count; i++) {
        const FITConfiguration *cfg = &fit->configs[i];
        printf("  Config [%u]: %s (kernel=%u fdt=%u initrd=%u)\n",
               i, cfg->description, cfg->kernel_idx, cfg->fdt_idx, cfg->initrd_idx);
    }
}
