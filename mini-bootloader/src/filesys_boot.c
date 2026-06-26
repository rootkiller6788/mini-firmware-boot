#include "filesys_boot.h"
#include <stdio.h>
#include <string.h>

void bootfs_mount(BootFS *fs, const uint8_t *disk_image, uint32_t disk_size,
                  BootFSType type, const uint8_t *boot_sector)
{
    memset(fs, 0, sizeof(BootFS));
    fs->type       = type;
    fs->disk_image = (uint8_t *)disk_image;
    fs->disk_size  = disk_size;

    switch (type) {
        case BOOTFS_FAT32: {
            if (boot_sector == NULL) {
                memset(&fs->sb.fat32, 0, sizeof(FAT32BootSector));
                fs->sb.fat32.bytes_per_sector   = 512;
                fs->sb.fat32.sectors_per_cluster = 8;
                fs->sb.fat32.reserved_sectors   = 32;
                fs->sb.fat32.fat_count          = 2;
                fs->sb.fat32.total_sectors      = disk_size / 512;
                fs->sb.fat32.fat_size           = 1024;
                fs->sb.fat32.root_cluster       = 2;
                fs->sb.fat32.data_start_sector =
                    (uint32_t)(fs->sb.fat32.reserved_sectors
                    + fs->sb.fat32.fat_count * fs->sb.fat32.fat_size);
            } else {
                memcpy(&fs->sb.fat32, boot_sector, sizeof(FAT32BootSector));
            }
            break;
        }
        case BOOTFS_EXT2:
        case BOOTFS_EXT4: {
            if (boot_sector == NULL) {
                memset(&fs->sb.ext2, 0, sizeof(EXT2SuperBlock));
                fs->sb.ext2.inodes_count     = 1024;
                fs->sb.ext2.blocks_count     = 8192;
                fs->sb.ext2.block_size       = 4096;
                fs->sb.ext2.blocks_per_group = 1024;
                fs->sb.ext2.inodes_per_group = 256;
                fs->sb.ext2.first_ino        = 1;
                fs->sb.ext2.inode_size       = 128;
            } else {
                memcpy(&fs->sb.ext2, boot_sector + 1024, sizeof(EXT2SuperBlock));
            }
            break;
        }
        default:
            break;
    }

    const char *type_str;
    switch (type) {
        case BOOTFS_FAT32: type_str = "FAT32"; break;
        case BOOTFS_EXT2:  type_str = "EXT2";  break;
        case BOOTFS_EXT4:  type_str = "EXT4";  break;
        default:           type_str = "NONE";  break;
    }
    printf("[bootfs] Mounted %s (%u MB)\n", type_str, disk_size / (1024 * 1024));
}

bool bootfs_read_file(BootFS *fs, const char *path, uint8_t *buffer,
                      uint32_t *size)
{
    if (fs == NULL || path == NULL || buffer == NULL || size == NULL) {
        return false;
    }

    DirEntry entry;
    if (!bootfs_find_file(fs, path, &entry)) {
        fprintf(stderr, "[bootfs] File not found: %s\n", path);
        return false;
    }

    printf("[bootfs] Reading '%s' (%u bytes)\n", path, entry.size);

    uint32_t bytes_to_read = entry.size;
    if (bytes_to_read > *size) bytes_to_read = *size;

    uint32_t offset = entry.first_cluster * BOOTFS_CLUSTER_SIZE;
    if (fs->disk_image != NULL && offset < fs->disk_size) {
        memcpy(buffer, fs->disk_image + offset, bytes_to_read);
    } else {
        memset(buffer, 0, bytes_to_read);
    }

    *size = bytes_to_read;
    return true;
}

int bootfs_list_dir(BootFS *fs, const char *path, DirEntry *entries,
                    int max_entries)
{
    if (fs == NULL || entries == NULL || max_entries <= 0) {
        return 0;
    }

    static const char *default_names[] = {
        "vmlinuz", "initrd.img", "grub.cfg",
        "System.map", "config", "boot.ini"
    };

    static const uint32_t default_sizes[] = {
        5242880, 16777216, 4096,
        2097152, 32768, 1024
    };

    int count = (int)(sizeof(default_names) / sizeof(default_names[0]));
    if (count > max_entries) count = max_entries;

    for (int i = 0; i < count; i++) {
        strncpy(entries[i].name, default_names[i], BOOTFS_MAX_NAME - 1);
        entries[i].name[BOOTFS_MAX_NAME - 1] = '\0';
        entries[i].size          = default_sizes[i];
        entries[i].first_cluster = (uint32_t)(i + 3);
        entries[i].attributes    = 0;
        entries[i].is_directory  = false;
        entries[i].inode         = (uint32_t)(i + 1);
    }

    return count;
}

bool bootfs_find_file(BootFS *fs, const char *path, DirEntry *entry)
{
    if (fs == NULL || path == NULL || entry == NULL) {
        return false;
    }

    DirEntry entries[BOOTFS_MAX_DIRENTS];
    int count = bootfs_list_dir(fs, "/", entries, BOOTFS_MAX_DIRENTS);

    const char *filename = path;
    if (path[0] == '/') filename = path + 1;

    for (int i = 0; i < count; i++) {
        if (strcmp(entries[i].name, filename) == 0) {
            memcpy(entry, &entries[i], sizeof(DirEntry));
            return true;
        }
    }

    return false;
}

void bootfs_print_dir(const BootFS *fs, const char *path)
{
    DirEntry entries[BOOTFS_MAX_DIRENTS];
    int count = bootfs_list_dir(fs, path, entries, BOOTFS_MAX_DIRENTS);

    printf("=== Directory listing: %s ===\n", path);
    printf("%-24s %-12s %-12s %s\n", "Name", "Size", "Cluster", "Attrib");

    for (int i = 0; i < count; i++) {
        printf("%-24s %-12u %-12u 0x%02X\n",
               entries[i].name, entries[i].size,
               entries[i].first_cluster, entries[i].attributes);
    }

    printf("%d entries\n", count);
}

void bootfs_print_file_info(const DirEntry *entry)
{
    if (entry == NULL) return;

    printf("File: %s\n", entry->name);
    printf("  Size:     %u bytes\n", entry->size);
    printf("  Cluster:  %u\n", entry->first_cluster);
    printf("  Attrib:   0x%02X\n", entry->attributes);
    printf("  Directory: %s\n", entry->is_directory ? "yes" : "no");
    printf("  Inode:    %u\n", entry->inode);
}
