#ifndef BIOS_HARDWARE_H
#define BIOS_HARDWARE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ================================================================
 * L1: Core Definitions — PC Hardware Programming Interface
 * Covers: CMOS RTC, 8042 Keyboard Controller, A20 Gate,
 *         PCI Configuration Space, PIC 8259A, PIT 8253/8254
 * Refs: IBM PC/AT Technical Reference, PCI Local Bus Spec 3.0,
 *       Intel 8259A/8254 datasheets
 * ================================================================ */

/* --- CMOS / RTC (Motorola MC146818A) --- */
#define CMOS_ADDR_PORT      0x70
#define CMOS_DATA_PORT      0x71
#define CMOS_NMI_DISABLE    0x80

#define RTC_REG_SECONDS      0x00
#define RTC_REG_MINUTES      0x02
#define RTC_REG_HOURS        0x04
#define RTC_REG_DAY_OF_WEEK  0x06
#define RTC_REG_DAY_OF_MONTH 0x07
#define RTC_REG_MONTH        0x08
#define RTC_REG_YEAR         0x09
#define RTC_REG_STATUS_A     0x0A
#define RTC_REG_STATUS_B     0x0B
#define RTC_REG_STATUS_C     0x0C
#define RTC_REG_STATUS_D     0x0D
#define RTC_REG_CENTURY      0x32

#define RTC_STATB_24HR       0x02
#define RTC_STATB_BINARY     0x04
#define RTC_STATB_SET        0x80

#define CMOS_DIAG_STATUS     0x0E
#define CMOS_SHUTDOWN_STATUS 0x0F
#define CMOS_BASE_MEM_LO     0x15
#define CMOS_BASE_MEM_HI     0x16
#define CMOS_EXT_MEM_LO      0x17
#define CMOS_EXT_MEM_HI      0x18
#define CMOS_CHECKSUM_HI     0x2E
#define CMOS_CHECKSUM_LO     0x2F

typedef struct {
    uint8_t  seconds;
    uint8_t  minutes;
    uint8_t  hours;
    uint8_t  day_of_week;
    uint8_t  day_of_month;
    uint8_t  month;
    uint16_t year;
    bool     is_24hr;
    bool     is_binary;
} RTCTime;

typedef struct {
    uint8_t  cmos_ram[256];
    RTCTime  system_time;
    uint16_t base_memory_kb;
    uint32_t extended_memory_kb;
    uint8_t  century_register;
    bool     rtc_valid;
} CMOSState;

/* --- Keyboard Controller (Intel 8042) --- */
#define KB_DATA_PORT        0x60
#define KB_STATUS_PORT      0x64
#define KB_CMD_PORT         0x64

#define KBC_STATUS_OUT_FULL 0x01
#define KBC_STATUS_IN_FULL  0x02
#define KBC_STATUS_CMD      0x08
#define KBC_STATUS_TIMEOUT  0x40

#define KBC_CMD_READ_CONF   0x20
#define KBC_CMD_WRITE_CONF  0x60
#define KBC_CMD_DISABLE_KBD 0xAD
#define KBC_CMD_ENABLE_KBD  0xAE
#define KBC_CMD_READ_OUT    0xD0
#define KBC_CMD_WRITE_OUT   0xD1
#define KBC_CMD_SELF_TEST   0xAA

#define KBC_CONF_INT1_EN     0x01
#define KBC_CONF_DISABLE_KBD 0x10
#define KBC_CONF_DISABLE_MOUSE 0x20
#define KBC_CONF_XLATE       0x40

typedef struct {
    uint8_t  config_byte;
    uint8_t  output_port;
    bool     kbd_enabled;
    bool     mouse_enabled;
    bool     self_test_passed;
} KBCState;

/* --- A20 Gate --- */
#define A20_PORT_92         0x92
#define A20_ENABLE_BIT      0x02

typedef enum {
    A20_METHOD_KBC,
    A20_METHOD_PORT92,
    A20_METHOD_BIOS,
    A20_METHOD_NONE
} A20Method;

/* --- PCI Configuration Space --- */
#define PCI_CONFIG_ADDR     0x0CF8
#define PCI_CONFIG_DATA     0x0CFC
#define PCI_MAX_DEVICES     256

#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_CLASS_CODE      0x09
#define PCI_HEADER_TYPE     0x0E
#define PCI_BAR0            0x10
#define PCI_INTERRUPT_LINE  0x3C
#define PCI_INTERRUPT_PIN   0x3D

#define PCI_CLASS_STORAGE_IDE    0x0101
#define PCI_CLASS_NETWORK_ETH    0x0200
#define PCI_CLASS_DISPLAY_VGA    0x0300
#define PCI_CLASS_BRIDGE_HOST    0x0600
#define PCI_CLASS_SERIAL_USB     0x0C03

typedef struct {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t  revision;
    uint8_t  prog_if;
    uint8_t  sub_class;
    uint8_t  base_class;
    uint8_t  header_type;
    uint32_t bar[6];
    uint8_t  interrupt_line;
    uint8_t  interrupt_pin;
    uint8_t  bus;
    uint8_t  device;
    uint8_t  function;
    bool     multi_function;
} PCIDevice;

typedef struct {
    PCIDevice devices[PCI_MAX_DEVICES];
    uint32_t   count;
} PCIBus;

/* --- PIC 8259A --- */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

#define PIC_ICW1_ICW4     0x01
#define PIC_ICW1_INIT     0x10
#define PIC_ICW4_8086     0x01
#define PIC_OFFSET_MASTER  0x20
#define PIC_OFFSET_SLAVE   0x28

typedef struct {
    uint8_t  master_mask;
    uint8_t  slave_mask;
    uint8_t  master_offset;
    uint8_t  slave_offset;
    bool     initialized;
    bool     auto_eoi;
} PICState;

/* --- PIT 8253/8254 --- */
#define PIT_CH0_DATA 0x40
#define PIT_CH2_DATA 0x42
#define PIT_CMD_PORT 0x43
#define PIT_BASE_FREQ 1193182ULL

#define PIT_CH0_SEL    0x00
#define PIT_LO_HI      0x30
#define PIT_MODE3      0x06
#define PIT_CH0_DEFAULT 0x36

typedef struct {
    uint16_t ch0_reload;
    uint16_t ch0_current;
    uint64_t tick_count;
} PITState;

/* ===== Function declarations ===== */
void     cmos_init(CMOSState *cmos);
uint8_t  cmos_read(CMOSState *cmos, uint8_t reg);
void     cmos_write(CMOSState *cmos, uint8_t reg, uint8_t value);
bool     cmos_read_rtc_time(CMOSState *cmos, RTCTime *time);
void     cmos_rtc_to_timestamp(const RTCTime *time, uint64_t *unix_ts);
uint16_t cmos_get_base_memory(const CMOSState *cmos);
uint32_t cmos_get_extended_memory(const CMOSState *cmos);
bool     cmos_verify_checksum(const CMOSState *cmos);
void     cmos_update_checksum(CMOSState *cmos);

void     kbc_init(KBCState *kbc);
uint8_t  kbc_read_status(const KBCState *kbc);
void     kbc_write_command(KBCState *kbc, uint8_t cmd);
void     kbc_write_data(KBCState *kbc, uint8_t data);
bool     kbc_self_test(KBCState *kbc);
void     kbc_enable_keyboard(KBCState *kbc);
void     kbc_disable_keyboard(KBCState *kbc);

void     a20_enable_via_kbc(KBCState *kbc);
void     a20_disable_via_kbc(KBCState *kbc);
void     a20_enable_fast(void);
bool     a20_check_status(void);

void     pci_bus_init(PCIBus *bus);
bool     pci_read_device_header(PCIBus *bus, uint8_t b, uint8_t d, uint8_t f, PCIDevice *dev);
void     pci_enumerate_bus(PCIBus *bus, uint8_t bus_num);
void     pci_enumerate_all(PCIBus *bus);
void     pci_find_by_class(const PCIBus *bus, uint8_t base, uint8_t sub, PCIDevice *results, uint32_t max, uint32_t *found);
void     pci_print_device(const PCIDevice *dev);

void     pic_init(PICState *pic, bool auto_eoi);
void     pic_remap(PICState *pic, uint8_t m_off, uint8_t s_off);
void     pic_set_mask(PICState *pic, uint8_t m_mask, uint8_t s_mask);
void     pic_send_eoi(PICState *pic, uint8_t irq);

void     pit_init(PITState *pit);
void     pit_set_channel(PITState *pit, uint8_t ch, uint16_t reload, uint8_t mode);
uint64_t pit_get_tick_count(void);
void     pit_tick_update(PITState *pit, uint64_t ticks);

#endif /* BIOS_HARDWARE_H */
