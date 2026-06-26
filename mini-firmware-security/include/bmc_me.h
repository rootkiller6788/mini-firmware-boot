#ifndef BMC_ME_H
#define BMC_ME_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define BMC_IPMI_MAX_CMD_LEN        256
#define BMC_IPMI_MAX_RESP_LEN       256
#define BMC_KCS_DATA_PORT           0xCA2
#define BMC_KCS_CMD_PORT            0xCA3
#define BMC_KCS_STATUS_PORT         0xCA4

#define BMC_IPMI_NETFN_APP          0x06
#define BMC_IPMI_NETFN_STORAGE      0x0A
#define BMC_IPMI_NETFN_TRANSPORT    0x0C
#define BMC_IPMI_NETFN_OEM          0x30

#define BMC_IPMI_CMD_GET_DEVICE_ID  0x01
#define BMC_IPMI_CMD_COLD_RESET     0x02
#define BMC_IPMI_CMD_GET_SEL_TIME  0x48
#define BMC_IPMI_CMD_ACTIVATE_SOL  0x20

#define BMC_STATUS_NORMAL           0x00
#define BMC_STATUS_BUSY             0x01
#define BMC_STATUS_ERROR            0x02

#define ME_HFS_BIOS_BOOT_DONE       (1 << 0)
#define ME_HFS_MFG_MODE             (1 << 4)
#define ME_HFS_ALT_DISABLED         (1 << 7)
#define ME_HFS_FW_INIT_COMPLETE     (1 << 3)
#define ME_HFS_ERROR                (1 << 2)

#define ME_FWSTS_ME_STATE_S0        0x05
#define ME_FWSTS_ME_STATE_M0        0x01
#define ME_FWSTS_ME_STATE_M3        0x03

#define PSP_CMD_SOFTWARE_VERSION    0x01
#define PSP_CMD_SECURITY_STATE      0x02
#define PSP_CMD_SET_FUSE            0x03

typedef struct {
    bool     ipmi_interface;
    uint16_t kcs_data_port;
    uint16_t kcs_cmd_port;
    uint16_t kcs_status_port;
    bool     sol_enabled;
    uint16_t sol_port;
    bool     vmedia_mounted;
    bool     vmedia_encrypted;
    uint8_t  last_status;
} BMCController;

typedef struct {
    uint16_t firmware_version;
    uint32_t hfs;
    bool     manufacturing_mode;
    bool     alt_disable;
    bool     jtag_disable;
    bool     fw_init_complete;
    uint8_t  error_code;
} IntelME;

typedef struct {
    uint16_t firmware_version;
    uint32_t security_state;
    bool     jtag_disabled;
    bool     manufacturing_mode;
    bool     secure_boot_enabled;
} PSP;

void bmc_init(BMCController *bmc);
bool bmc_ipmi_command(BMCController *bmc, uint8_t netfn,
                      uint8_t command, const uint8_t *data,
                      size_t data_len, uint8_t *response,
                      size_t *response_len);
bool bmc_sol_redirect(BMCController *bmc, bool enable);
bool bmc_vmedia_mount(BMCController *bmc, bool encrypted);
bool bmc_vmedia_unmount(BMCController *bmc);
bool bmc_check_health(BMCController *bmc);

void me_init(IntelME *me);
bool me_check_manufacturing_mode(IntelME *me);
bool me_lock_jtag(IntelME *me);
bool me_hap_disable(IntelME *me);
bool me_verify_firmware(IntelME *me, const uint8_t *expected_hash);
bool me_detect_supply_chain_risk(IntelME *me);
bool me_secure_boot_check(IntelME *me);

bool psp_init(PSP *psp);
bool psp_disable_jtag(PSP *psp);
bool psp_exit_manufacturing_mode(PSP *psp);
bool psp_check_security(PSP *psp);

#endif
