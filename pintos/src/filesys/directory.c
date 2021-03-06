#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "filesys/dentry_cache.h"

static struct lock dir_lock;

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

static struct dir *dir_get_parent(struct dir *child) {
  block_sector_t parent_sector;
  parent_sector = inode_get_parent_sector(child->inode);
  return dir_open(inode_open(parent_sector));
}

static bool lookup_any(const struct dir *dir) {
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) {
      if (e.in_use) {
        return true;
      }
    }
  return false;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *path,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (path != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) {
      if (e.in_use && !strcmp (path, e.name)) {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
    }
  return false;
}

void dir_init() {
  lock_init(&dir_lock);
  dentry_cache_init();
}

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt, block_sector_t parent_sector)
{
  return inode_create (sector, entry_cnt * sizeof (struct dir_entry), INODE_TYPE_DIR, parent_sector);
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  ASSERT(inode_is_directory(inode));
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens the parent's *dir if is_dir is false
  If is_dir is true, assume the leaf is a directory and open it
*/
struct dir *dir_open_canon_path(struct canon_path *cpath, bool is_dir) {
  struct dir *current;
  size_t token_cnt, i;
  struct dir_entry ep;
  int inumber;
  struct inode *inode;
  size_t num_iters;
  lock_acquire(&dir_lock);
  if (canon_path_is_absolute(cpath)) {
    // query the dentry cache
    if (is_dir) {
      // finds the entry itself
      if (dentry_cache_query(cpath, 0, &inumber)) {
        inode = inode_open(inumber);
        ASSERT(inode_is_directory(inode));
        current = dir_open(inode);
        goto done_dontcache;
      }
    }
    // find the first parent
    if (dentry_cache_query(cpath, 1, &inumber)) {
      inode = inode_open(inumber);
      ASSERT(inode_is_directory(inode));
      current = dir_open(inode);
      goto done_dontcache;
    }
    else {
      current = dir_open_root();
    }
  }
  else {
    if (thread_current()->process_info) {
      current = dir_reopen(thread_current()->process_info->cwd);
    }
    else {
      // called in fsutils.c
      current = dir_open_root();
    }
  }
  token_cnt = canon_path_get_tokens_cnt(cpath);
  num_iters = is_dir ? token_cnt : token_cnt - 1;
  for (i = 0; i < num_iters ; i++) {
    char *token = canon_path_get_token(cpath, i);
    if (!strcmp(token, "..")) {
      struct dir *parent;
      if (!(parent = dir_get_parent(current))) {
        // this implies OOM
        current = NULL;
        goto done;
      }
      dir_close(current);
      current = parent;
    }
    else {
      if (!lookup(current, token, &ep, NULL)) {
        current = NULL;
        goto done;
      }
      dir_close(current);
      inode = inode_open(ep.inode_sector);
      if (!inode_is_directory(inode)) {
        current = NULL;
        goto done;
      }
      if (!(current = dir_open(inode))) {
        current = NULL;
        goto done;
      }
    }
  }

done:
  if (current && canon_path_is_absolute(cpath)) {
    if (is_dir) {
      dentry_cache_append(cpath, 0, inode_get_inumber(dir_get_inode(current)));
      dentry_cache_append(cpath, 1, inode_get_inumber(dir_get_inode(dir_get_parent(current))));
    }
    else {
      dentry_cache_append(cpath, 1, inode_get_inumber(dir_get_inode(current)));
    }
    
  }
  
done_dontcache:
  lock_release(&dir_lock);
  // at the point of return, current is open
  return current;
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *path,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (path != NULL);

  lock_acquire(&dir_lock);
  if (lookup (dir, path, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;
  lock_release(&dir_lock);
  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const struct canon_path *cpath, const char *name, block_sector_t inode_sector, bool is_dir)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  lock_acquire(&dir_lock);
  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL)) {
    goto done;
  }

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at(dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use) {
      break;
    }

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at(dir->inode, &e, sizeof e, ofs) == sizeof e;

  /* write to dentry cache */
  if (is_dir) {
    dentry_cache_append(cpath, 0, inode_sector);
  }
  dentry_cache_append(cpath, 1, inode_get_inumber(dir->inode));

 done:
 lock_release(&dir_lock);
  return success;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct dir *victim_dir;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  lock_acquire(&dir_lock);
  /* Find directory entry. */
  if (!lookup(dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open(e.inode_sector);
  if (inode == NULL) {
    goto done;
  }
    
  // check if inode is a file or a directory with no elements or non-root
  if (inode_is_directory(inode)) {
    victim_dir = dir_open(inode_reopen(inode));
    // the number 2 comes from inode_reopen of victim_dir and inode_open 
    if (inode_get_inumber(inode) == ROOT_DIR_SECTOR || lookup_any(victim_dir) || inode_get_open_cnt(inode) > 2) {
      dir_close(victim_dir);
      success = false;
      goto done;
    }
    dir_close(victim_dir);
  }

  /* remove dentry cache */
  dentry_cache_invalidate(inode_get_inumber(inode));

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove(inode);
  success = true;

 done:
  inode_close(inode);
  lock_release(&dir_lock);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;
  lock_acquire(&dir_lock);
  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          lock_release(&dir_lock);
          return true;
        } 
    }
  lock_release(&dir_lock);
  return false;
}