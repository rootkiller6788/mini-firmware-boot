#ifndef FILESYS_BOOT_H
#define FILESYS_BOOT_H

#include <stdbool.h>
#include <stdint.h>

#define BOOTFS_MAX_NAME      256
#define BOOTFS_MAX_PATH      512
#define BOOTFS_MAX_DIRENTS   128
#define BOOTFS_SECTOR_SIZE   512
#define BOOTFS_CLUSTER_SIZE  4096

#define FAT32_SIGNATURE      0xAA55
#define FAT32_FAT_ENTRY_MASK 0x0FFFFFFF
#define FAT32_EOC             0x0FFFFFF8
#define FAT32_BAD_CLUSTER     0x0FFFFFF7

#define EXT2_SUPER_MAGIC       0xEF53
#define EXT2_ROOT_INO          2
#define EXT2_DIRECT_BLOCKS    12
#define EXT2_IND_BLOCK        12
#define EXT2_DIND_BLOCK       13
#define EXT2_TIND_BLOCK       14

#define BOOTFS_FILE_ATTR_RO   0x01
#define BOOTFS_FILE_ATTR_DIR  0x02
#define BOOTFS_FILE_ATTR_HID  0x08

typedef enum {
    BOOTFS_FAT32 = 0,
    BOOTFS_EXT2  = 1,
    BOOTFS_EXT4  = 2,
    BOOTFS_NONE  = 0xFF
} BootFSType;

typedef struct {
    char     name[BOOTFS_MAX_NAME];
    uint32_t size;
    uint32_t first_cluster;
    uint32_t attributes;
    bool     is_directory;
    uint32_t inode;
} DirEntry;

typedef struct {
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint32_t total_sectors;
    uint32_t fat_size;
    uint32_t root_cluster;
    uint32_t data_start_sector;
} FAT32BootSector;

typedef struct {
    uint32_t inodes_count;
    uint32_t blocks_count;
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t first_ino;
    uint16_t inode_size;
} EXT2SuperBlock;

typedef struct {
    uint16_t mode;
    uint16_t uid;
    uint32_t size;
    uint32_t blocks;
    uint32_t  block[15];
} EXT2Inode;

typedef struct {
    BootFSType type;
    union {
        FAT32BootSector fat32;
        EXT2SuperBlock  ext2;
    } sb;
    uint8_t *disk_image;
    uint32_t disk_size;
} BootFS;

void   bootfs_mount(BootFS *fs, const uint8_t *disk_image, uint32_t disk_size,
                    BootFSType type, const uint8_t *boot_sector);
bool   bootfs_read_file(BootFS *fs, const char *path, uint8_t *buffer,
                        uint32_t *size);
int    bootfs_list_dir(BootFS *fs, const char *path, DirEntry *entries,
                       int max_entries);
bool   bootfs_find_file(BootFS *fs, const char *path, DirEntry *entry);
void   bootfs_print_dir(const BootFS *fs, const char *path);
void   bootfs_print_file_info(const DirEntry *entry);

#endif
