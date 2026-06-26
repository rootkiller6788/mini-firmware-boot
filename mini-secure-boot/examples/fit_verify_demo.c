#include "trust_chain.h"
#include "signature_verify.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t fit_raw[4096];
    FITImage fit;
    RSAKey pub;
    BootChain chain;

    printf("╔══════════════════════════════════════════════╗\n");
    printf("║   FIT Image Verification Demo               ║\n");
    printf("╚══════════════════════════════════════════════╝\n\n");

    /* ── 1. Build FIT image ── */
    printf(">>> Step 1: Build FIT Image with Kernel + DTB + Initrd\n");
    memset(fit_raw, 0, sizeof(fit_raw));

    /* FIT header (big-endian fields) */
    uint32_t *hdr = (uint32_t *)fit_raw;
    hdr[0] = (
        ((uint32_t)((FIT_MAGIC >> 24) & 0xFF) << 24) |
        ((uint32_t)((FIT_MAGIC >> 16) & 0xFF) << 16) |
        ((uint32_t)((FIT_MAGIC >> 8)  & 0xFF) << 8)  |
        ((uint32_t)(FIT_MAGIC         & 0xFF))
    );
    /* DTB-style: magic is always big-endian */
    uint32_t magic_be;
    magic_be  = ((FIT_MAGIC >> 24) & 0xFF);
    magic_be |= ((FIT_MAGIC >> 16) & 0xFF) << 8;
    magic_be |= ((FIT_MAGIC >> 8)  & 0xFF) << 16;
    magic_be |= ((FIT_MAGIC & 0xFF)) << 24;
    hdr[0] = magic_be;
    hdr[1] = 0;                          /* timestamp upper */
    hdr[2] = 0;                          /* timestamp lower */
    hdr[3] = ((3 << 24));                /* image_count = 3 (big-endian) */
    hdr[4] = ((2 << 24));                /* config_count = 2 (big-endian) */
    hdr[5] = ((40 << 24));               /* description offset = 40 */
    const char desc[] = "Example FIT: Linux 5.15 + DTB + initramfs";
    memcpy(fit_raw + 40, desc, sizeof(desc));

    if (!fit_parse(&fit, fit_raw, sizeof(fit_raw))) {
        printf("  FAILED to parse FIT image\n");
        return 1;
    }
    printf("  FIT parsed: '%s'\n", fit.header.description);
    printf("  Images: %u, Configs: %u\n\n",
           fit.header.image_count, fit.header.config_count);

    /* Setup sub-images with distinct data */
    uint8_t kernel_data[2048];
    uint8_t dtb_data[1024];
    uint8_t initrd_data[3072];
    memset(kernel_data, 0xAD, sizeof(kernel_data));
    memset(dtb_data, 0xBE, sizeof(dtb_data));
    memset(initrd_data, 0xEF, sizeof(initrd_data));

    /* Add kernel image */
    memcpy(fit.images[0].data, kernel_data, sizeof(kernel_data));
    fit.images[0].data_size = sizeof(kernel_data);
    snprintf(fit.images[0].name, TC_MAX_NAME_LEN, "kernel-1");
    snprintf(fit.images[0].type, TC_MAX_NAME_LEN, "kernel_noload");
    fit.images[0].load_address = 0x80008000;
    fit.images[0].entry_point = 0x80008000;
    sha256_hash(kernel_data, sizeof(kernel_data), fit.images[0].hash_value);
    fit.images[0].has_hash = true;

    /* Add DTB image */
    memcpy(fit.images[1].data, dtb_data, sizeof(dtb_data));
    fit.images[1].data_size = sizeof(dtb_data);
    snprintf(fit.images[1].name, TC_MAX_NAME_LEN, "fdt-1");
    snprintf(fit.images[1].type, TC_MAX_NAME_LEN, "flat_dt");
    fit.images[1].load_address = 0x83000000;
    sha256_hash(dtb_data, sizeof(dtb_data), fit.images[1].hash_value);
    fit.images[1].has_hash = true;

    /* Add initrd image */
    memcpy(fit.images[2].data, initrd_data, sizeof(initrd_data));
    fit.images[2].data_size = sizeof(initrd_data);
    snprintf(fit.images[2].name, TC_MAX_NAME_LEN, "initrd-1");
    snprintf(fit.images[2].type, TC_MAX_NAME_LEN, "ramdisk");
    fit.images[2].load_address = 0x84000000;
    sha256_hash(initrd_data, sizeof(initrd_data), fit.images[2].hash_value);
    fit.images[2].has_hash = true;

    /* Config 0: kernel + fdt + initrd */
    fit.configs[0].kernel_idx = 0;
    fit.configs[0].fdt_idx = 1;
    fit.configs[0].initrd_idx = 2;
    snprintf(fit.configs[0].description, FIT_MAX_DESC_LEN, "conf-1");

    /* Config 1: kernel + fdt only */
    fit.configs[1].kernel_idx = 0;
    fit.configs[1].fdt_idx = 1;
    fit.configs[1].initrd_idx = 2;
    snprintf(fit.configs[1].description, FIT_MAX_DESC_LEN, "conf-2");

    /* ── 2. Print FIT components ── */
    printf(">>> Step 2: FIT Component Layout\n");
    fit_print_components(&fit);
    printf("\n");

    /* ── 3. Generate key ── */
    printf(">>> Step 3: Generate Signing Key\n");
    rsa_generate_simple_keypair(&pub, NULL, 2048);
    printf("  Key ready (mod_len=%u)\n\n", pub.mod_len);

    /* ── 4. Sign configurations ── */
    printf(">>> Step 4: Sign Each Configuration\n");
    for (uint32_t i = 0; i < fit.header.config_count; i++) {
        const FITSubImage *kernel_img = NULL;
        if (fit_get_subimage(&fit, fit.configs[i].kernel_idx, &kernel_img) && kernel_img) {
            memcpy(fit.configs[i].signature_data, kernel_img->hash_value,
                   SHA256_HASH_SIZE_TC);
            fit.configs[i].sig_len = SHA256_HASH_SIZE_TC;
            snprintf(fit.configs[i].signature_type, TC_MAX_NAME_LEN, "sha256,rsa2048");
        }
        printf("  Config[%u] '%s' signed (sig_len=%u)\n",
               i, fit.configs[i].description, fit.configs[i].sig_len);
    }
    printf("\n");

    /* ── 5. Verify configurations ── */
    printf(">>> Step 5: Verify Configurations\n");
    bool all_ok = fit_verify_all(&fit, &pub);
    printf("  All configs verified: %s\n\n", all_ok ? "PASS" : "FAIL");

    /* ── 6. Build boot chain ── */
    printf(">>> Step 6: Build Verified Boot Chain\n");
    trust_chain_init(&chain);

    uint8_t spl_data[512];
    memset(spl_data, 0x55, sizeof(spl_data));
    trust_chain_add_component(&chain, BOOT_COMP_SPL, "SPL",
                               spl_data, sizeof(spl_data));

    uint8_t uboot_data[2048];
    memset(uboot_data, 0x66, sizeof(uboot_data));
    trust_chain_add_component(&chain, BOOT_COMP_U_BOOT, "U-Boot",
                               uboot_data, sizeof(uboot_data));

    trust_chain_add_component(&chain, BOOT_COMP_LINUX_KERNEL, "Linux",
                               kernel_data, sizeof(kernel_data));

    trust_chain_add_component(&chain, BOOT_COMP_INITRD, "Initrd",
                               initrd_data, sizeof(initrd_data));

    trust_chain_add_component(&chain, BOOT_COMP_FDT, "FDT",
                               dtb_data, sizeof(dtb_data));

    /* ── 7. Compute expected hashes ── */
    printf(">>> Step 7: Compute Expected Hashes\n");
    uint8_t expected_hashes[TC_MAX_COMPONENTS][SHA256_HASH_SIZE_TC];
    memset(expected_hashes, 0, sizeof(expected_hashes));

    for (uint32_t i = 0; i < chain.component_count; i++) {
        sha256_hash(chain.components[i].data,
                    chain.components[i].data_size,
                    expected_hashes[i]);
        printf("  [%u] %-12s hash computed\n", i, chain.components[i].name);
    }
    printf("\n");

    /* ── 8. Verify whole chain ── */
    printf(">>> Step 8: Verify Entire Boot Chain\n");
    bool chain_ok = trust_chain_verify_all(&chain, expected_hashes);
    printf("  Chain verification: %s\n\n", chain_ok ? "PASS" : "FAIL");

    /* ── 9. Print final status ── */
    printf(">>> Step 9: Final Status\n");
    trust_chain_print_status(&chain);
    printf("\n");
    printf(">>> FIT Summary:\n");
    printf("  Total FIT components: %u images, %u configs\n",
           fit.header.image_count, fit.header.config_count);

    printf("\n=== Demo Complete ===\n");
    return 0;
}
