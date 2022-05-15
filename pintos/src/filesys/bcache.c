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

#define BCACHE_MAX_ENTRIES 0x40

struct bcache_entry {
	struct lock lock;
	bool in_use;
	int64_t last_use;
	block_sector_t sector;
	uint8_t data[BLOCK_SECTOR_SIZE];
};

static struct bcache_entry *bcache;

static void bcache_entry_init(struct bcache_entry *e) {
	e->in_use = false;
	e->last_use = 0;
	lock_init(&e->lock);
}

// synchronization must be guaranteed by the caller
static void bcache_entry_occupy(struct bcache_entry *e, block_sector_t sector) {
	if (e->in_use) {
		block_write(fs_device, e->sector, e->data);
	}
	e->in_use = true;
	e->sector = sector;
	e->last_use = timer_ticks();
	block_read(fs_device, sector, e->data);
}

static void bcache_entry_release(struct bcache_entry *e) {
	if (e->in_use) {
		e->in_use = false;
		block_write(fs_device, e->sector, e->data);
	}
}

void bcache_init () {
	if ((bcache = (struct bcache_entry *)calloc(sizeof(*bcache), BCACHE_MAX_ENTRIES)) == NULL) {
		PANIC("bcache_init: calloc failed");
	}
	for (int i = 0; i < BCACHE_MAX_ENTRIES; i++) {
		bcache_entry_init(&bcache[i]);
	}
}

// bounce buffer must be provided by caller
void bcache_write_at(block_sector_t sector, void *in_, off_t offset, size_t length) {
	int i, lru_index, free_idx;
	struct bcache_entry *cur = NULL;
	int64_t min_last_use = INT64_MAX;
	uint8_t *in;
	in = in_;
	free_idx = -1;
	for (i = 0; i < BCACHE_MAX_ENTRIES; i++) {
		cur = &bcache[i];
		lock_acquire(&cur->lock);
		if (cur->in_use && cur->sector == sector) {
			memcpy(&cur->data[offset], in, length);
			lock_release(&cur->lock);
			return;
		}
		if (cur->in_use && cur->last_use < min_last_use) {
			min_last_use = cur->last_use;
			lru_index = i;
		}
		if (!cur->in_use) {
			free_idx = i;
		}
		lock_release(&cur->lock);
	}
	{
		// Evict the LRU entry
		if (free_idx == -1) {
			struct bcache_entry *victim = &bcache[lru_index];
			lock_acquire(&victim->lock);
			bcache_entry_occupy(victim, sector);
			memcpy(&victim->data[offset], in, length);
			lock_release(&victim->lock);
		}
		else {
			struct bcache_entry *victim = &bcache[free_idx];
			lock_acquire(&victim->lock);
			bcache_entry_occupy(victim, sector);
			memcpy(&victim->data[offset], in, length);
			lock_release(&victim->lock);
		}
	}
}

void bcache_write(block_sector_t sector, void *in_) {
	int i, lru_index, free_idx;
	struct bcache_entry *cur = NULL;
	int64_t min_last_use = INT64_MAX;
	uint8_t *in;
	in = in_;
	free_idx = -1;
	for (i = 0; i < BCACHE_MAX_ENTRIES; i++) {
		cur = &bcache[i];
		lock_acquire(&cur->lock);
		if (cur->in_use && cur->sector == sector) {
			memcpy(cur->data, in, sizeof(cur->data));
			lock_release(&cur->lock);
			return;
		}
		if (cur->in_use && cur->last_use < min_last_use) {
			min_last_use = cur->last_use;
			lru_index = i;
		}
		if (!cur->in_use) {
			free_idx = i;
		}
		lock_release(&cur->lock);
	}
	{
		// Evict the LRU entry
		if (free_idx == -1) {
			struct bcache_entry *victim = &bcache[lru_index];
			lock_acquire(&victim->lock);
			bcache_entry_occupy(victim, sector);
			memcpy(victim->data, in, sizeof(victim->data));
			lock_release(&victim->lock);
		}
		else {
			struct bcache_entry *victim = &bcache[free_idx];
			lock_acquire(&victim->lock);
			bcache_entry_occupy(victim, sector);
			memcpy(victim->data, in, sizeof(victim->data));
			lock_release(&victim->lock);
		}
	}
}

void bcache_read(block_sector_t sector, void *out_) {
	int i, lru_index, free_idx;
	struct bcache_entry *cur = NULL;
	int64_t min_last_use = INT64_MAX;
	uint8_t *out;
	out = out_;
	free_idx = -1;
	for (i = 0; i < BCACHE_MAX_ENTRIES; i++) {
		cur = &bcache[i];
		lock_acquire(&cur->lock);
		if (cur->in_use && cur->sector == sector) {
			memcpy(out, &cur->data, sizeof(cur->data));
			lock_release(&cur->lock);
			return;
		}
		if (cur->in_use && cur->last_use < min_last_use) {
			min_last_use = cur->last_use;
			lru_index = i;
		}
		else if (!cur->in_use) {
			free_idx = i;
		}
		lock_release(&cur->lock);
	}
	{
		// Evict the LRU entry
		if (free_idx == -1) {
			struct bcache_entry *victim = &bcache[lru_index];
			lock_acquire(&victim->lock);
			bcache_entry_occupy(victim, sector);
			memcpy(out, victim->data, sizeof(victim->data));
			lock_release(&victim->lock);
		}
		else {
			struct bcache_entry *victim = &bcache[free_idx];
			lock_acquire(&victim->lock);
			bcache_entry_occupy(victim, sector);
			memcpy(out, victim->data, sizeof(victim->data));
			lock_release(&victim->lock);
		}
	}
}

/* this function does not require internal synch because it is only called on timer interrupts and on filesys_done */
void bcache_sync() {
	for (int i = 0; i < BCACHE_MAX_ENTRIES; i++) {
		struct bcache_entry *e;
		e = &bcache[i];
		bcache_entry_release(e);
	}
}

