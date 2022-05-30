#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/bcache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  dir_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  bcache_sync();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size, bool is_dir) 
{
  block_sector_t inode_sector = 0, parent_sector;
  char *name;
  struct canon_path *cpath;
  bool success;
  struct dir *dir;
  if (!path_canonicalize(path, &cpath)) {
    return false;
  }
  if (canon_path_get_tokens_cnt(cpath) == 0) {
    // implies . or .. or /
    canon_path_release(cpath);
    return false;
  }
  name = canon_path_get_leaf(cpath);
  if (!strcmp(name, "..")) {
    success = false;
    canon_path_get_leaf(cpath);
  }
  dir = dir_open_canon_path(cpath, false);
  parent_sector = inode_get_inumber(dir_get_inode(dir));
  if (is_dir) {
    success = (dir != NULL
                  && free_map_allocate(1, &inode_sector)
                  // directories start with capacity of 10
                  && dir_create(inode_sector, 10, parent_sector)
                  && dir_add(dir, cpath, name, inode_sector, is_dir));
  }
  else {
    success = (dir != NULL
                  && free_map_allocate(1, &inode_sector)
                  && inode_create(inode_sector, initial_size, INODE_TYPE_FILE, parent_sector)
                  && dir_add(dir, cpath, name, inode_sector, is_dir));
  }
  if (!success && inode_sector != 0) {
    free_map_release (inode_sector, 1);
  }
  // this must be called last, because it will free {name}
  canon_path_release(cpath);
  dir_close(dir);
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_open(const char *path, bool allow_dir, struct file **out_file, struct dir **out_dir)
{
  char *name;
  struct canon_path *cpath;
  struct dir *dir;
  struct inode *inode;
  if (!path_canonicalize(path, &cpath)) {
    return false;
  }
  if (canon_path_get_tokens_cnt(cpath) == 0) {
    // implies . or .. or /
    if (allow_dir) {
      if (canon_path_is_absolute(cpath)) {
        struct dir *d;
        canon_path_release(cpath);
        d = dir_open_root();
        *out_dir = d;
        return true;
      }
      else {
        struct dir *d;
        canon_path_release(cpath);
        if (thread_current()->process_info) {
          if (!(d = dir_reopen(thread_current()->process_info->cwd))) {
            return false;
          }
          else {
            *out_dir = d;
            return true;
          }
        }
        else {
          return false;
        }
      }
    }
    else {
      canon_path_release(cpath);
      return false;
    }
  }
  name = canon_path_get_leaf(cpath);
  dir = dir_open_canon_path(cpath, false);

  if (dir != NULL) {
    if (!strcmp(name, "..")) {
      struct dir *d;
      d = dir_open_canon_path(cpath, true);
      inode = dir_get_inode(d);
      dir_close(d);
    }
    else if (!dir_lookup(dir, name, &inode)) {
      canon_path_release(cpath);
      return false; 
    }
  }
  else {
    /* no parent directory */
    canon_path_release(cpath);
    return false;
  }
  dir_close (dir);
  // this must be done at the very last because it will free {name}
  canon_path_release(cpath);
  if (inode_is_directory(inode)) {
    struct dir *d;
    if (allow_dir && (d = dir_open(inode))) {
      *out_dir = d;
      return true;
    }
    else {
      return false;
    }
  }
  else {
    struct file *f;
    if ((f = file_open(inode))) {
      *out_file = f;
      return true;
    }
    else {
      return false;
    }
  } 
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *path) 
{
  char *name;
  struct canon_path *cpath;
  bool success;
  struct dir *dir;
  if (!path_canonicalize(path, &cpath)) {
    return false;
  }
  if (canon_path_get_tokens_cnt(cpath) == 0) {
    // implies . or .. or /
    success = false;
    canon_path_release(cpath);
    return success;
  }
  name = canon_path_get_leaf(cpath);
  if (!strcmp(name, "..")) {
    // cannot delete one's parent directory
    success = false;
    canon_path_get_leaf(cpath);
  }
  dir = dir_open_canon_path(cpath, false);
  success = dir != NULL && dir_remove(dir, name);
  dir_close(dir);
  // this must be done at the very last because it will free {name}
  canon_path_release(cpath);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16, ROOT_DIR_SECTOR))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
