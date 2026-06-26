#ifndef LEGACY_BIOS_H
#define LEGACY_BIOS_H

#include <stdbool.h>
#include <stdint.h>

#define IVT_SIZE                256
#define IVT_ENTRY_SIZE          4
#define BDA_SEGMENT             0x0040
#define BIOS_BASE_MEMORY_KB     640
#define BIOS_EXTENDED_MEMORY_KB 0
#define EBDA_SEGMENT_BASE       0x9FC0
#define MBR_MAGIC               0xAA55
#define SECTOR_SIZE             512
#define MBR_CODE_SIZE           446

/* POST diagnostic codes written to port 0x80 */
#define POST_CPU_TEST           0x01
#define POST_CMOS_CHECKSUM      0x02
#define POST_DMA_INIT           0x03
#define POST_RAM_REFRESH        0x04
#define POST_KEYBOARD_INIT      0x05
#define POST_VIDEO_INIT         0x0C
#define POST_RAM_TEST_BASE      0x10
#define POST_RAM_TEST_EXTENDED  0x2E
#define POST_INT_VECTORS        0x3F
#define POST_BOOTSTRAP          0x7F
#define POST_COMPLETE           0xFF

/* Standard BIOS interrupt vectors */
#define INT_VIDEO_SERVICES      0x10
#define INT_EQUIPMENT_CHECK     0x11
#define INT_MEMORY_SIZE         0x12
#define INT_DISK_SERVICES       0x13
#define INT_SERIAL_SERVICES     0x14
#define INT_SYSTEM_SERVICES     0x15
#define INT_KEYBOARD            0x16
#define INT_PRINTER             0x17
#define INT_BOOTSTRAP           0x19
#define INT_TIME                0x1A
#define INT_DOS_SERVICES        0x21

/* Disk service sub-functions (AH) */
#define DISK_RESET              0x00
#define DISK_READ               0x02
#define DISK_WRITE              0x03
#define DISK_VERIFY             0x04
#define DISK_FORMAT             0x05

/* Video service sub-functions (AH) */
#define VIDEO_SET_MODE          0x00
#define VIDEO_SET_CURSOR        0x01
#define VIDEO_SET_POS           0x02
#define VIDEO_GET_POS           0x03
#define VIDEO_SCROLL_UP         0x06
#define VIDEO_SCROLL_DOWN       0x07
#define VIDEO_WRITE_CHAR        0x09
#define VIDEO_WRITE_TELETYPE    0x0E
#define VIDEO_GET_MODE          0x0F

/* Equipment list bits */
#define EQUIP_FPU_PRESENT       0x0002
#define EQUIP_MOUSE_PRESENT     0x0004
#define EQUIP_VGA_PRESENT       0x0008
#define EQUIP_DISK_DRIVES_MASK  0x00C0

typedef struct {
    uint16_t offset;
    uint16_t segment;
} IVTEntry;

typedef struct {
    IVTEntry vectors[IVT_SIZE];
} IVT;

typedef struct {
    uint16_t com_ports[4];
    uint16_t lpt_ports[4];
    uint16_t equipment_list;
    uint8_t  manufacturer_test;
    uint16_t memory_size_kb;
    uint8_t  reserved1[2];
    uint8_t  keyboard_shift_flags;
    uint8_t  keyboard_shift_flags2;
    uint8_t  alt_numpad;
    uint16_t keyboard_buffer_head;
    uint16_t keyboard_buffer_tail;
    uint8_t  keyboard_buffer[32];
    uint8_t  disk_motor_status;
    uint8_t  disk_status;
    uint8_t  reserved2[4];
    uint8_t  video_mode;
    uint16_t video_columns;
    uint16_t video_page_size;
    uint16_t video_page_start;
    uint8_t  cursor_pos[8][2];
    uint8_t  cursor_type_end;
    uint8_t  cursor_type_start;
    uint8_t  active_display_page;
    uint16_t crtc_port;
    uint8_t  port_3x8;
    uint8_t  port_3x9;
    uint8_t  timer_counter;
    uint8_t  reserved3[0x6B];
} BIOSDataArea;

typedef struct {
    uint16_t size_kb;
    uint8_t  data[1024];
} EBDA;

typedef struct {
    uint8_t  post_code;
    bool     cpu_ok;
    bool     ram_ok;
    bool     video_ok;
    bool     keyboard_ok;
    bool     disk_ok;
    bool     bootstrap_ok;
} POSTResult;

void     bios_init_ivt(IVT *ivt);
void     bios_set_interrupt(IVT *ivt, int int_num, uint16_t segment, uint16_t offset);
IVTEntry bios_get_interrupt(const IVT *ivt, int int_num);
void     bios_int10h_video(uint8_t ah, uint8_t al, uint8_t bh, uint8_t bl, uint16_t cx, uint16_t dx);
int      bios_int13h_disk(uint8_t ah, uint8_t dl, uint16_t cx, uint16_t dh,
                          uint16_t es, uint16_t bx, uint16_t num_sectors);
int      bios_int19h_bootstrap(IVT *ivt);
int      bios_post(IVT *ivt, BIOSDataArea *bda);
void     bios_print_bda(const BIOSDataArea *bda);
void     bios_print_post_result(const POSTResult *result);
void     bios_dump_ivt(const IVT *ivt);

#endif /* LEGACY_BIOS_H */
