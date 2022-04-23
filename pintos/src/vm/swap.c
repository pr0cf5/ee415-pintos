#include <debug.h>
#include <stdint.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "vm/swap.h"

#define PAGE_SIZE 0x1000

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
    bitmap_set_multiple(swap_free_map, swap_idx, PAGE_SIZE/BLOCK_SECTOR_SIZE, false);
    for (size_t i = 0; i < PAGE_SIZE/BLOCK_SECTOR_SIZE; i++) {
        block_read(swap_block, swap_idx + i, &paddr[i*BLOCK_SECTOR_SIZE]);
    }
done:
    lock_release(&swap_lock);
}

int swap_out(void *paddr_) {
    uint8_t *paddr = (uint8_t *)paddr_;
    size_t swap_idx;
    lock_acquire(&swap_lock);
    if (bitmap_count(swap_free_map, 0, swap_free_map_size, false) < PAGE_SIZE/BLOCK_SECTOR_SIZE) {
        NOT_REACHED();
        goto done;
    }
    if ((swap_idx = bitmap_scan_and_flip(swap_free_map, 0, PAGE_SIZE/BLOCK_SECTOR_SIZE, false) == BITMAP_ERROR)) {
        PANIC("unreachable");
        goto done;
    }

    for (size_t i = 0; i < PAGE_SIZE/BLOCK_SECTOR_SIZE; i++) {
        block_write(swap_block, swap_idx + i, &paddr[i*BLOCK_SECTOR_SIZE]);
    }
done:
    lock_release(&swap_lock);
}

void swap_free(uint32_t swap_idx) {
    lock_acquire(&swap_lock);
    bitmap_set_multiple(swap_free_map, swap_idx, PAGE_SIZE/BLOCK_SECTOR_SIZE, false);
    lock_release(&swap_lock);
}