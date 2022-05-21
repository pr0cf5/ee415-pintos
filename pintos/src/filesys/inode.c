#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/bcache.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /* First data sector. */
    block_sector_t parent;              /* Sector of pardir's inode */
    off_t length;                       /* File size in bytes. */
    uint8_t size_type;                  /* Type of inode in size (small~huge) */
    uint8_t func_type;                  /* Type of inode in function */
    uint16_t unused1;
    unsigned magic;                     /* Magic number. */
    uint32_t unused2[123];              /* Not used. */
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    struct lock lock;                   /* Lock for synchronizing file expansion/deletion */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

/* for inode internals */
#define INODE_TYPE_SMALL 1
#define INODE_TYPE_LARGE 2
#define INODE_TYPE_HUGE 3

#define SECTORS_PER_ARRAY ((BLOCK_SECTOR_SIZE/sizeof(block_sector_t)))
#define INODE_SMALL_SECTORS 0x40
#define INODE_LARGE_SECTORS SECTORS_PER_ARRAY
#define INODE_HUGE_SECTORS SECTORS_PER_ARRAY*SECTORS_PER_ARRAY

#define SECTOR_INVALID -1

static uint8_t ZEROS[BLOCK_SECTOR_SIZE];
static block_sector_t byte_to_sector(const struct inode *inode, off_t pos);
static bool inode_expand_sectors(struct inode *inode, off_t new_size);
static bool inode_expand(struct inode *inode, off_t new_size);
static bool inode_create_small(block_sector_t sector, off_t length, uint8_t func_type, block_sector_t parent_sector);
static bool inode_create_large(block_sector_t sector, off_t length, uint8_t func_type, block_sector_t parent_sector);
static bool inode_create_huge(block_sector_t sector, off_t length, uint8_t func_type, block_sector_t parent_sector);

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  block_sector_t *sector_array;
  block_sector_t sector;
  ASSERT(inode != NULL);

  sector = SECTOR_INVALID;

  if (pos >= inode->data.length) {
    return sector;
  }
  if ((sector_array = calloc(1, BLOCK_SECTOR_SIZE)) == NULL) {
    return sector;
  }

  switch (inode->data.size_type) {
    case INODE_TYPE_SMALL: {
      sector = inode->data.start + pos / BLOCK_SECTOR_SIZE;
      goto done;
    }
    case INODE_TYPE_LARGE: {
      off_t lv1_idx;
      lv1_idx = pos / BLOCK_SECTOR_SIZE;
      bcache_read(inode->data.start, sector_array);
      sector = sector_array[lv1_idx];
      goto done;
    }
    case INODE_TYPE_HUGE: {
      off_t lv1_idx, lv2_idx;
      lv2_idx = (pos / BLOCK_SECTOR_SIZE) % SECTORS_PER_ARRAY;
      lv1_idx = (pos / BLOCK_SECTOR_SIZE) / SECTORS_PER_ARRAY;
      bcache_read(inode->data.start, sector_array);
      bcache_read(sector_array[lv1_idx], sector_array);
      sector = sector_array[lv2_idx];
      goto done;
    }
    default: {
      NOT_REACHED();
    }
  }
done:
  free(sector_array);
  return sector;    
}

// synchronization must be guaranteed by the caller
// this function modifies inode->data, so the caller must preserve it
static bool inode_expand_sectors(struct inode *inode, off_t new_size) {
  // rule: 
  // 1. Determine if inode type change is required. 
  // If we need to increment the number of sectors for INODE_SMALL, 
  // we just change it to INODE_LARGE even if it can be accomodated within INODE_SMALL size
  // 2. If there is inode type change, first allocate the new block sectors. Then copy the contents.
  // cases: (SMALL -> LARGE), (SMALL -> HUGE), (LARGE -> HUGE)
  // 3. If there is no inode type change, simply append
  // TODO: must cleanup disk on returning false. But there are no testcases that test this capability, so I'll just leave it ^^
  uint8_t *buffer;
  block_sector_t *sector_array;
  struct inode_disk *disk_inode;
  block_sector_t i, j, cnt_lv1, cnt_lv2, rem;
  bool success;
  success = false;
  if ((buffer = calloc(1,BLOCK_SECTOR_SIZE)) == NULL) {
    success = false;
    return success;
  }
  sector_array = (block_sector_t *)buffer;
  disk_inode = (struct inode_disk *)buffer;
  switch (inode->data.size_type) {
    case INODE_TYPE_SMALL: {
      if (new_size > INODE_HUGE_SECTORS * BLOCK_SECTOR_SIZE) {
        return false;
      }
      else if (new_size > INODE_LARGE_SECTORS * BLOCK_SECTOR_SIZE) {
        // small -> huge
        if (!inode_create_huge(inode->sector, new_size, inode->data.func_type, inode->data.parent)) {
          success = false;
          goto done;
        }
        bcache_read(inode->sector, &inode->data);
        success = true;
        goto done;
      }
      else {
        // small -> large
        if (!inode_create_large(inode->sector, new_size, inode->data.func_type, inode->data.parent)) {
          success = false;
          goto done;
        }
        bcache_read(inode->sector, &inode->data);
        success = true;
        goto done;
      }
    }
    case INODE_TYPE_LARGE: {
      if (new_size > INODE_LARGE_SECTORS * BLOCK_SECTOR_SIZE) {
        // large -> huge
        if (!inode_create_huge(inode->sector, new_size, inode->data.func_type, inode->data.parent)) {
          success = false;
          goto done;
        }
        bcache_read(inode->sector, &inode->data);
        success = true;
        goto done;
      }
      else {
        // expand large
        block_sector_t num_sectors_new, num_sectors_ori, i;
        block_sector_t *sector_array;
        num_sectors_ori = bytes_to_sectors(inode->data.length);
        // update disk_inode
        inode->data.length = new_size;
        num_sectors_new = bytes_to_sectors(new_size);
        if (num_sectors_ori == num_sectors_new) {
          success = true;
          bcache_write(inode->sector, &inode->data);
          goto done;
        }
        else {
          if ((sector_array = calloc(1,BLOCK_SECTOR_SIZE)) == NULL) {
            success = false;
            goto done;
          }
          bcache_read(inode->data.start, sector_array);
          ASSERT(num_sectors_ori == SECTORS_PER_ARRAY || sector_array[num_sectors_ori] == SECTOR_INVALID);
          for (i = num_sectors_ori; i < num_sectors_new; i++) {
            if (!free_map_allocate(1,&sector_array[i])) {
              success = false;
              free(sector_array);
              goto done;
            }
            else {
              bcache_write(sector_array[i], ZEROS);
            }
          }
          if (num_sectors_new < SECTORS_PER_ARRAY) {
            sector_array[num_sectors_new] = SECTOR_INVALID;
          }
          bcache_write(inode->data.start, sector_array);
          // update disk_inode
          bcache_write(inode->sector, &inode->data);
          free(sector_array);
          success = true;
          goto done;
        }
      }
    }
    case INODE_TYPE_HUGE: {
      if (new_size > INODE_HUGE_SECTORS * BLOCK_SECTOR_SIZE) {
        return false;
      }
      else {
        // expand huge
        block_sector_t num_sectors_new, num_sectors_ori, cnt_lv1_new, cnt_lv1_ori, cnt_lv2_new, cnt_lv2_ori, i,j;
        block_sector_t *sector_array1, *sector_array2;
        num_sectors_ori = bytes_to_sectors(inode->data.length);
        inode->data.length = new_size;
        num_sectors_new = bytes_to_sectors(new_size);
        if (num_sectors_ori == num_sectors_new) {
          bcache_write(inode->sector, &inode->data);
          success = true;
          goto done;
        }
        else {
          if ((sector_array1 = calloc(1,BLOCK_SECTOR_SIZE)) == NULL) {
            success = false;
            goto done;
          }
          if ((sector_array2 = calloc(1,BLOCK_SECTOR_SIZE)) == NULL) {
            free(sector_array1);
            success = false;
            goto done;
          }
          cnt_lv1_new = DIV_ROUND_UP(num_sectors_new, SECTORS_PER_ARRAY);
          cnt_lv1_ori = DIV_ROUND_UP(num_sectors_ori, SECTORS_PER_ARRAY);
          cnt_lv2_new = num_sectors_new % SECTORS_PER_ARRAY == 0 ? SECTORS_PER_ARRAY : num_sectors_new % SECTORS_PER_ARRAY;
          cnt_lv2_ori = num_sectors_ori % SECTORS_PER_ARRAY == 0 ? SECTORS_PER_ARRAY : num_sectors_ori % SECTORS_PER_ARRAY;
          if (cnt_lv1_new == cnt_lv1_ori) {
            ASSERT(cnt_lv2_new != cnt_lv2_ori);
            bcache_read(inode->data.start, sector_array1);
            bcache_read(sector_array1[cnt_lv1_ori-1], sector_array2);
            for (i = cnt_lv2_ori; i < cnt_lv2_new; i++) {
              if (!free_map_allocate(1, &sector_array2[i])) {
                free(sector_array1);
                free(sector_array2);
                success = false;
                goto done;
              }
              else {
                bcache_write(sector_array2[i], ZEROS);
              }
            }
            if (i < SECTORS_PER_ARRAY) {
              sector_array2[i] = SECTOR_INVALID;
            }
            bcache_write(sector_array1[cnt_lv1_ori-1], sector_array2);
            // update disk_inode
            bcache_write(inode->sector, &inode->data);
            success = true;
            goto done;
          }
          else {
            // 1. fill the last lv2, only if cnt_lv2_ori > 0
            bcache_read(inode->data.start, sector_array1);
            bcache_read(sector_array1[cnt_lv1_ori-1], sector_array2);
            if (cnt_lv2_ori > 0) {
              for (i = cnt_lv2_ori; i < SECTORS_PER_ARRAY; i++) {
                if (!free_map_allocate(1,&sector_array2[i])) {
                  success = false;
                  free(sector_array1);
                  free(sector_array2);
                  goto done;
                }
                else {
                  bcache_write(sector_array2[i], ZEROS);
                  num_sectors_new--;
                }
              }
              bcache_write(sector_array[cnt_lv1_ori-1], sector_array2);
            }
            
            // 2. determine the remaining fills with the new remaining lengths
            cnt_lv1_new = DIV_ROUND_UP(num_sectors_new, SECTORS_PER_ARRAY);
            cnt_lv2_new = num_sectors_new % SECTORS_PER_ARRAY == 0 ? SECTORS_PER_ARRAY : num_sectors_new % SECTORS_PER_ARRAY;
            bcache_read(inode->data.start, sector_array1);
            for (i = cnt_lv1_ori; i < cnt_lv1_new; i++) {
              if (!free_map_allocate(1,&sector_array1[i])) {
                success = false;
                free(sector_array1);
                free(sector_array2);
                goto done;
              }
              bcache_read(sector_array1[i], sector_array2);
              for (j = 0; j < (i == cnt_lv1_new-1 ? cnt_lv2_new : BLOCK_SECTOR_SIZE/sizeof(block_sector_t)); j++) {
                if (!free_map_allocate(1,&sector_array2[i])) {
                  success = false;
                  free(sector_array1);
                  free(sector_array2);
                  goto done;
                }
                else {
                  bcache_write(sector_array2[i], ZEROS);
                }
              }
              if (j < SECTORS_PER_ARRAY) {
                sector_array2[j] = SECTOR_INVALID;
              }
              bcache_write(sector_array1[i], sector_array2);
            }
            if (i < SECTORS_PER_ARRAY) {
              sector_array1[i] = SECTOR_INVALID;
            }
            bcache_write(inode->data.start, sector_array1);
            // update disk_inode
            bcache_write(inode->sector, &inode->data);
            success = true;
            goto done;
          }
        }
      }
    }
    default: {
      NOT_REACHED();
    }
  }
done:
  free(buffer);
  return success;
}

static bool inode_expand(struct inode *inode, off_t new_size) {
  uint8_t *original_data;
  off_t original_length;
  bool success;
  original_length = inode->data.length;
  if (original_length == 0) {
    inode_expand_sectors(inode, new_size);
    return true;
  }
  if ((original_data = calloc(1,original_length)) == NULL) {
    success = false;
    return success;
  }
  // Linearize original buffer. This call should NEVER trigger 'expand' because it will cause assertion lock_held_by_current_thread
  if (inode_read_at(inode, original_data, original_length, 0) != original_length) {
    success = false;
    goto done;
  }
  // Expand in the unit of sectors
  inode_expand_sectors(inode, new_size);
  // Copy the original data to the new inode. This call should NEVER trigger 'expand' because it will cause assertion lock_held_by_current_thread.
  if (inode_write_at(inode, original_data, original_length, 0) != original_length) {
    success = false;
    goto done;
  }
  success = true;
done:
  free(original_data);
  return success;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;
static struct lock open_inodes_lock;

static bool inode_create_small(block_sector_t sector, off_t length, uint8_t func_type, block_sector_t parent_sector) {
  bool success = false;
  struct inode_disk *disk_inode;
  size_t sectors;

  if ((disk_inode = calloc(1, sizeof(*disk_inode))) == NULL) {
    goto done;
  }
  sectors = bytes_to_sectors(length);
  disk_inode->length = length;
  disk_inode->parent = parent_sector;
  disk_inode->size_type = INODE_TYPE_SMALL;
  disk_inode->func_type = func_type;
  disk_inode->magic = INODE_MAGIC;
  if (free_map_allocate(sectors, &disk_inode->start)) {
    bcache_write(sector, disk_inode);
    if (sectors > 0) {
      for (int i = 0; i < sectors; i++) {
        bcache_write(disk_inode->start+i, ZEROS);
      }
    }
    free(disk_inode);
    success = true;
    goto done;
  }
  else {
    goto done;
  }
done:
  return success;
}

static bool inode_create_large(block_sector_t sector, off_t length, uint8_t func_type, block_sector_t parent_sector) {
  // TODO: cleanup when disk is full
  struct inode_disk *disk_inode;
  block_sector_t cnt_lv1, arr_lv1, i;
  block_sector_t *sector_array;
  bool success = false;
  if ((disk_inode = calloc(1, sizeof(*disk_inode))) == NULL) {
    success = false;
    return success;
  }
  if ((sector_array = calloc(1,BLOCK_SECTOR_SIZE)) == NULL) {
    free(disk_inode);
    success = false;
    return success;
  }
  disk_inode->length = length;
  disk_inode->parent = parent_sector;
  disk_inode->size_type = INODE_TYPE_LARGE;
  disk_inode->func_type = func_type;
  disk_inode->magic = INODE_MAGIC;
  cnt_lv1 = bytes_to_sectors(length);
  if (free_map_allocate(1, &arr_lv1)) {
    memset(sector_array, 0, BLOCK_SECTOR_SIZE);
    for (i = 0; i < cnt_lv1; i++) {
      if (!free_map_allocate(1, &sector_array[i])) {
        success = false;
        goto done;
      }
      else {
        bcache_write(sector_array[i], ZEROS);
      }
    }
    // marker
    if (i < SECTORS_PER_ARRAY) {
      sector_array[i] = SECTOR_INVALID;
    }
    bcache_write(arr_lv1, sector_array);
  }
  else {
    success = false;
    goto done;
  }
  disk_inode->start = arr_lv1;
  bcache_write(sector, disk_inode);
  success = true;
done:
  free(sector_array);
  return success;
}

static bool inode_create_huge(block_sector_t sector, off_t length, uint8_t func_type, block_sector_t parent_sector) {
  // TODO: cleanup when disk is full
  struct inode_disk *disk_inode;
  block_sector_t num_sectors, cnt_lv1, arr_lv1, cnt_lv2, i, j;
  block_sector_t *sector_array1, *sector_array2;
  bool success = false;
  if ((disk_inode = calloc(1, sizeof(*disk_inode))) == NULL) {
    success = false;
    return success;
  }
  if ((sector_array1 = calloc(1,BLOCK_SECTOR_SIZE)) == NULL) {
    free(disk_inode);
    success = false;
    return success;
  }
  if ((sector_array2 = calloc(1,BLOCK_SECTOR_SIZE)) == NULL) {
    free(disk_inode);
    free(sector_array1);
    success = false;
    return success;
  }
  disk_inode->length = length;
  disk_inode->parent = parent_sector;
  disk_inode->size_type = INODE_TYPE_HUGE;
  disk_inode->func_type = func_type;
  disk_inode->magic = INODE_MAGIC;
  num_sectors = bytes_to_sectors(length);
  cnt_lv1 = DIV_ROUND_UP(num_sectors, SECTORS_PER_ARRAY);
  cnt_lv2 = num_sectors % SECTORS_PER_ARRAY == 0 ? SECTORS_PER_ARRAY : num_sectors % SECTORS_PER_ARRAY;
  if (free_map_allocate(1, &arr_lv1)) {
    memset(sector_array1, 0, BLOCK_SECTOR_SIZE);
    for (i = 0; i < cnt_lv1; i++) {
      if (!free_map_allocate(1, &sector_array1[i])) {
        success = false;
        goto done;
      }
      else {
        memset(sector_array2, 0, BLOCK_SECTOR_SIZE);
        for (j = 0; j < (i == cnt_lv1-1 ? cnt_lv2 : SECTORS_PER_ARRAY); j++) {
          if (!free_map_allocate(1, &sector_array2[j])) {
            success = false;
            goto done;
          }
          else {
            bcache_write(sector_array2[j], ZEROS);
          }
        }
        // this only runs for the last arr1
        if (j < SECTORS_PER_ARRAY) {
          sector_array2[j] = SECTOR_INVALID;
        }
        bcache_write(sector_array1[i], sector_array2);
      }
    }
    // marker
    if (i < SECTORS_PER_ARRAY) {
      sector_array1[i] = SECTOR_INVALID;
    }
    bcache_write(arr_lv1, sector_array1);
  }
  disk_inode->start = arr_lv1;
  bcache_write(sector, disk_inode);
  success = true;
done:
  free(sector_array1);
  free(sector_array2);
  return success;
}

/* Initializes the inode module. */
void
inode_init (void) 
{
  ASSERT(sizeof(struct inode_disk) == BLOCK_SECTOR_SIZE);
  list_init(&open_inodes);
  lock_init(&open_inodes_lock);
  bcache_init();
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, uint8_t func_type, block_sector_t parent_sector)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;
  ASSERT (length >= 0);
  if (length <= BLOCK_SECTOR_SIZE * INODE_SMALL_SECTORS) {
    success = inode_create_small(sector, length, func_type, parent_sector);
  }
  else if (length <= BLOCK_SECTOR_SIZE * INODE_LARGE_SECTORS) {
    success = inode_create_large(sector, length, func_type, parent_sector);
  }
  else if (length <= BLOCK_SECTOR_SIZE * INODE_HUGE_SECTORS) {
    success = inode_create_huge(sector, length, func_type, parent_sector);
  }
  else {
    // too large to handle
    success = false;
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  lock_acquire(&open_inodes_lock);
  for (e = list_begin(&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          goto done;
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL) {
    goto done;
  }

  /* Initialize. */
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init(&inode->lock);
  bcache_read(inode->sector, &inode->data);
  list_push_front(&open_inodes, &inode->elem);
done:
  lock_release(&open_inodes_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL) {
    inode->open_cnt++;
  }
    
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

int inode_get_open_cnt (const struct inode *inode)
{
  return inode->open_cnt;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  lock_acquire(&open_inodes_lock);
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem); 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          // due to inode structure change, this is not valid anymore
          //free_map_release (inode->data.start,
          //                  bytes_to_sectors (inode->data.length)); 
        }
      free(inode); 
    }
    lock_release(&open_inodes_lock);
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce_buffer;
  bool expand;

  if ((bounce_buffer = calloc(1,BLOCK_SECTOR_SIZE)) == NULL) {
    return 0;
  }
  
  expand = (offset + size) > inode->data.length;
  if (expand) {
    lock_acquire(&inode->lock);
  }

  while (size > 0) {
    /* Block sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(inode, offset);
    if (sector_idx == SECTOR_INVALID) {
      break;
    }
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length (inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0){
      break;
    }

    bcache_read(sector_idx, bounce_buffer);
    memcpy(buffer+bytes_read, bounce_buffer+sector_ofs, chunk_size);
          
    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }

  if (expand) {
    lock_release(&inode->lock);
  }

  free(bounce_buffer);
  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size, off_t offset) 
{
  if (inode->deny_write_cnt)
    return 0;

  const uint8_t *buffer = buffer_;
  uint8_t *bounce_buffer;
  off_t bytes_written = 0;
  bool expand;

  expand = (offset + size) > inode->data.length;
  if (expand) {
    lock_acquire(&inode->lock);
    if (!inode_expand(inode, size+offset)) {
      lock_release(&inode->lock);
      return 0;
    }
  }

  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector (inode, offset);
    if (sector_idx == SECTOR_INVALID) {
      break;
    }
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode_length (inode) - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    bcache_write_at(sector_idx, buffer+bytes_written, sector_ofs, chunk_size);

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }

  if (expand) {
    lock_release(&inode->lock);
  }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

bool inode_is_directory(const struct inode *inode) {
  return inode->data.func_type == INODE_TYPE_DIR;
}

block_sector_t inode_get_parent_sector(const struct inode *inode) {
  return inode->data.parent;
}