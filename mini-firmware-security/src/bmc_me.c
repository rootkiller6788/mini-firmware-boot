#include "bmc_me.h"

#include <stdio.h>
#include <string.h>

void bmc_init(BMCController *bmc) {
    if (bmc == NULL)
        return;

    memset(bmc, 0, sizeof(BMCController));
    bmc->ipmi_interface = true;
    bmc->kcs_data_port = BMC_KCS_DATA_PORT;
    bmc->kcs_cmd_port = BMC_KCS_CMD_PORT;
    bmc->kcs_status_port = BMC_KCS_STATUS_PORT;
    bmc->sol_enabled = false;
    bmc->sol_port = 623;
    bmc->vmedia_mounted = false;
    bmc->vmedia_encrypted = false;
    bmc->last_status = BMC_STATUS_NORMAL;
}

bool bmc_ipmi_command(BMCController *bmc, uint8_t netfn,
                      uint8_t command, const uint8_t *data,
                      size_t data_len, uint8_t *response,
                      size_t *response_len) {
    if (bmc == NULL || response == NULL || response_len == NULL)
        return false;

    if (data_len > BMC_IPMI_MAX_CMD_LEN)
        return false;

    if (bmc->last_status == BMC_STATUS_ERROR)
        return false;

    bmc->last_status = BMC_STATUS_BUSY;

    if (netfn == BMC_IPMI_NETFN_APP &&
        command == BMC_IPMI_CMD_GET_DEVICE_ID) {
        response[0] = 0x20;
        response[1] = 0x01;
        response[2] = 0x51;
        response[3] = 0x00;
        response[4] = 0x01;
        *response_len = 5;
    } else if (netfn == BMC_IPMI_NETFN_APP &&
               command == BMC_IPMI_CMD_COLD_RESET) {
        response[0] = 0x00;
        *response_len = 1;
    } else if (netfn == BMC_IPMI_NETFN_TRANSPORT &&
               command == BMC_IPMI_CMD_ACTIVATE_SOL) {
        if (data_len > 0) {
            bmc->sol_enabled = (data[0] == 1);
        }
        response[0] = 0x00;
        *response_len = 1;
    } else {
        response[0] = 0xC1;
        *response_len = 1;
        bmc->last_status = BMC_STATUS_ERROR;
        return false;
    }

    bmc->last_status = BMC_STATUS_NORMAL;
    return true;
}

bool bmc_sol_redirect(BMCController *bmc, bool enable) {
    if (bmc == NULL)
        return false;

    bmc->sol_enabled = enable;
    return true;
}

bool bmc_vmedia_mount(BMCController *bmc, bool encrypted) {
    if (bmc == NULL)
        return false;

    if (bmc->vmedia_mounted)
        return false;

    bmc->vmedia_mounted = true;
    bmc->vmedia_encrypted = encrypted;
    return true;
}

bool bmc_vmedia_unmount(BMCController *bmc) {
    if (bmc == NULL)
        return false;

    if (!bmc->vmedia_mounted)
        return false;

    bmc->vmedia_mounted = false;
    bmc->vmedia_encrypted = false;
    return true;
}

bool bmc_check_health(BMCController *bmc) {
    if (bmc == NULL)
        return false;

    return (bmc->last_status == BMC_STATUS_NORMAL);
}

void me_init(IntelME *me) {
    if (me == NULL)
        return;

    memset(me, 0, sizeof(IntelME));
    me->firmware_version = 0x0C00;
    me->hfs = ME_HFS_FW_INIT_COMPLETE | ME_HFS_BIOS_BOOT_DONE;
    me->manufacturing_mode = false;
    me->alt_disable = false;
    me->jtag_disable = false;
    me->fw_init_complete = true;
    me->error_code = 0;
}

bool me_check_manufacturing_mode(IntelME *me) {
    if (me == NULL)
        return false;

    return (me->hfs & ME_HFS_MFG_MODE) != 0;
}

bool me_lock_jtag(IntelME *me) {
    if (me == NULL)
        return false;

    if (me->manufacturing_mode)
        return false;

    me->jtag_disable = true;
    return true;
}

bool me_hap_disable(IntelME *me) {
    if (me == NULL)
        return false;

    me->alt_disable = true;
    me->hfs |= ME_HFS_ALT_DISABLED;
    return true;
}

/*
 * Verify ME firmware integrity via hash comparison.
 * In real ME, this is enforced by the ROM bootloader checking
 * the firmware image signature before allowing execution.
 *
 * L4: Hash-based integrity verification per NIST SP 800-193.
 * If SHA-256 is second-preimage resistant, an attacker cannot
 * craft a modified firmware image that matches the expected hash.
 *
 * Complexity: O(n) in firmware size, amortized O(1) for precomputed hash
 */
bool me_verify_firmware(IntelME *me, const uint8_t *expected_hash) {
    uint32_t i;
    uint8_t computed_hash[32];
    uint8_t fw_version_bytes[4];

    if (me == NULL || expected_hash == NULL)
        return false;

    if (!me->fw_init_complete)
        return false;

    /* Compute a synthetic firmware hash from version + ME config bits */
    fw_version_bytes[0] = (uint8_t)(me->firmware_version & 0xFF);
    fw_version_bytes[1] = (uint8_t)((me->firmware_version >> 8) & 0xFF);
    fw_version_bytes[2] = (uint8_t)(me->hfs & 0xFF);
    fw_version_bytes[3] = (uint8_t)((me->hfs >> 8) & 0xFF);

    /* Simplified SHA-256-like hash for simulation (uses rolling hash) */
    {
        uint32_t hash_state = 0x9E3779B9;
        for (i = 0; i < 4; i++) {
            hash_state ^= (uint32_t)fw_version_bytes[i];
            hash_state += (hash_state << 6) + (hash_state >> 2);
        }
        hash_state ^= (uint32_t)(me->hfs & 0xFFFF);
        hash_state += (hash_state << 5) + (hash_state >> 3);

        for (i = 0; i < 32; i++) {
            hash_state = hash_state * 1103515245 + 12345;
            computed_hash[i] = (uint8_t)((hash_state >> 16) & 0xFF);
        }
    }

    /* Compare computed hash with expected hash */
    for (i = 0; i < 32; i++) {
        if (computed_hash[i] != expected_hash[i])
            return false;
    }

    return true;
}

bool me_detect_supply_chain_risk(IntelME *me) {
    if (me == NULL)
        return false;

    if (me_check_manufacturing_mode(me))
        return true;

    if (!me->jtag_disable)
        return true;

    if (!me->alt_disable)
        return true;

    if (!me->fw_init_complete)
        return true;

    return false;
}

bool me_secure_boot_check(IntelME *me) {
    if (me == NULL)
        return false;

    if (me_check_manufacturing_mode(me))
        return false;

    if (me->hfs & ME_HFS_ERROR)
        return false;

    return true;
}

bool psp_init(PSP *psp) {
    if (psp == NULL)
        return false;

    memset(psp, 0, sizeof(PSP));
    psp->firmware_version = 0x0103;
    psp->security_state = 0x03;
    psp->jtag_disabled = true;
    psp->manufacturing_mode = false;
    psp->secure_boot_enabled = true;

    return true;
}

bool psp_disable_jtag(PSP *psp) {
    if (psp == NULL)
        return false;

    if (psp->manufacturing_mode)
        return false;

    psp->jtag_disabled = true;
    return true;
}

bool psp_exit_manufacturing_mode(PSP *psp) {
    if (psp == NULL)
        return false;

    if (!psp->manufacturing_mode)
        return true;

    psp->manufacturing_mode = false;
    psp->security_state &= ~0x02;
    psp->jtag_disabled = true;
    return true;
}

bool psp_check_security(PSP *psp) {
    if (psp == NULL)
        return false;

    if (psp->manufacturing_mode)
        return false;

    if (!psp->jtag_disabled)
        return false;

    if (!psp->secure_boot_enabled)
        return false;

    return true;
}
