#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0       /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1       /* Root directory file inode sector. */

/* Block device that contains the file system. */
extern struct block *fs_device;
struct file;
struct dir;

void filesys_init (bool format);
void filesys_done (void);
bool filesys_create (const char *path, off_t initial_size, bool is_dir);
bool filesys_open(const char *path, bool allow_dir, struct file **out_file, struct dir **out_dir);
bool filesys_remove (const char *path);

#endif /* filesys/filesys.h */
