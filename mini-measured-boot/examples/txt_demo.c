#include <stdio.h>
#include <string.h>
#include "sha256.h"
#include "pcr_bank.h"
#include "event_log.h"
#include "crtm_drtm.h"

int main(void) {
    PCRBank bank;
    TCGEventLog srtm_log;
    TCGEventLog drtm_log;
    SRTMFlow srtm_flow;
    DRTMFlow drtm_flow;
    uint32_t i;

    printf("=== Intel TXT Launch Demo: SRTM vs DRTM ===\n\n");

    pcr_bank_init(&bank, TPM_ALG_SHA256);

    printf("--- Part 1: SRTM (Static Root of Trust) ---\n");
    event_log_init(&srtm_log, 0x0200);
    srtm_flow_init(&srtm_flow, &bank, &srtm_log);
    srtm_simulate(&srtm_flow);

    printf("\nPCR state after SRTM:\n");
    printf("  PCR0 (BIOS):        ");
    for (i = 0; i < 8; i++) printf("%02x", bank.pcr[0][i]);
    printf("...\n");
    printf("  PCR2 (Option ROM):  ");
    for (i = 0; i < 8; i++) printf("%02x", bank.pcr[2][i]);
    printf("...\n");
    printf("  PCR4 (Boot):       ");
    for (i = 0; i < 8; i++) printf("%02x", bank.pcr[4][i]);
    printf("...\n");
    printf("  PCR8 (OS Loader):  ");
    for (i = 0; i < 8; i++) printf("%02x", bank.pcr[8][i]);
    printf("...\n");

    printf("\n--- Part 2: DRTM (Dynamic Root of Trust) ---\n");
    printf("Runtimes: CPU executes GETSEC[SENTER] instruction.\n");
    printf("This triggers a late launch, creating DRTM PCRs.\n");

    event_log_init(&drtm_log, 0x0200);
    drtm_flow_init(&drtm_flow, &bank, &drtm_log);
    drtm_simulate(&drtm_flow);

    printf("\nDRTM PCR state:\n");
    printf("  PCR17 (SINIT ACM): ");
    for (i = 0; i < 8; i++) printf("%02x", bank.pcr[17][i]);
    printf("...\n");
    printf("  PCR18 (MLE):       ");
    for (i = 0; i < 8; i++) printf("%02x", bank.pcr[18][i]);
    printf("...\n");
    printf("  PCR19 (Trusted OS): ");
    for (i = 0; i < 8; i++) printf("%02x", bank.pcr[19][i]);
    printf("...\n");

    printf("\n--- Part 3: PCR Reset (DRTM only) ---\n");
    {
        bool ok;
        printf("Resetting DRTM PCR 17...\n");
        ok = pcr_reset(&bank, 17);
        printf("  PCR17 reset: %s\n", ok ? "OK" : "DENIED");
        ok = pcr_reset(&bank, 18);
        printf("  PCR18 reset: %s\n", ok ? "OK" : "DENIED");

        printf("Attempting to reset static PCR 0...\n");
        ok = pcr_reset(&bank, 0);
        printf("  PCR0 reset: %s (expected: DENIED)\n", ok ? "OK (unexpected)" : "DENIED");
    }

    printf("\n--- Part 4: PCR Assignment Summary ---\n");
    printf("%-8s %-25s %s\n", "PCR", "Purpose", "Type");
    printf("%-8s %-25s %s\n", "----", "-------------------------", "--------");
    printf("%-8s %-25s %s\n", "PCR0", "CRTM + BIOS", "SRTM (Static)");
    printf("%-8s %-25s %s\n", "PCR1", "Platform Config", "SRTM (Static)");
    printf("%-8s %-25s %s\n", "PCR2", "External ROM", "SRTM (Static)");
    printf("%-8s %-25s %s\n", "PCR4", "Boot Manager", "SRTM (Static)");
    printf("%-8s %-25s %s\n", "PCR7", "Secure Boot Policy", "SRTM (Static)");
    printf("%-8s %-25s %s\n", "PCR8", "OS Loader", "SRTM (Static)");
    printf("%-8s %-25s %s\n", "PCR17", "SINIT ACM", "DRTM (Dynamic)");
    printf("%-8s %-25s %s\n", "PCR18", "MLE", "DRTM (Dynamic)");
    printf("%-8s %-25s %s\n", "PCR19", "Trusted OS", "DRTM (Dynamic)");
    printf("%-8s %-25s %s\n", "PCR20", "OS Kernel", "DRTM (Dynamic)");
    printf("%-8s %-25s %s\n", "PCR22", "Application", "DRTM (Dynamic)");

    printf("\n=== TXT verification: %s ===\n",
           txt_verify_launch(&drtm_flow) ? "PASS" : "FAIL");

    return 0;
}
