#include <debug.h>
#include <stdint.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "vm/swap.h"

static struct block *swap_block;
static struct lock swap_lock;
static struct bitmap *swap_free_map;
size_t swap_free_map_size;

void swap_init() {
    lock_init(&swap_lock);
    swap_block = block_get_role(BLOCK_SWAP);
    if (swap_block == NULL) {
        ASSERT(0);
    }
    swap_free_map_size = block_size(swap_block);
    swap_free_map = bitmap_create(swap_free_map_size);
}

void swap_in(size_t swap_idx, void *paddr_) {
    uint8_t *paddr = (uint8_t *)paddr_;
    lock_acquire(&swap_lock);
    bitmap_set_multiple(swap_free_map, swap_idx, PGSIZE/BLOCK_SECTOR_SIZE, false);
    for (size_t i = 0; i < PGSIZE/BLOCK_SECTOR_SIZE; i++) {
        block_read(swap_block, swap_idx + i, &paddr[i*BLOCK_SECTOR_SIZE]);
    }
done:
    lock_release(&swap_lock);
}

int swap_out(void *paddr_) {
    uint8_t *paddr = (uint8_t *)paddr_;
    size_t swap_idx;
    lock_acquire(&swap_lock);
    if (bitmap_count(swap_free_map, 0, swap_free_map_size, false) < PGSIZE/BLOCK_SECTOR_SIZE) {
        NOT_REACHED();
        goto done;
    }
    if ((swap_idx = bitmap_scan_and_flip(swap_free_map, 0, PGSIZE/BLOCK_SECTOR_SIZE, false)) == BITMAP_ERROR) {
        NOT_REACHED();
        goto done;
    }
    for (size_t i = 0; i < PGSIZE/BLOCK_SECTOR_SIZE; i++) {
        block_write(swap_block, swap_idx + i, &paddr[i*BLOCK_SECTOR_SIZE]);
    }
done:
    lock_release(&swap_lock);
    return swap_idx;
}

void swap_free(uint32_t swap_idx) {
    lock_acquire(&swap_lock);
    bitmap_set_multiple(swap_free_map, swap_idx, PGSIZE/BLOCK_SECTOR_SIZE, false);
    lock_release(&swap_lock);
}