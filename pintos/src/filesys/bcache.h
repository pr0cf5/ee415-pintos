#include "devices/block.h"

void bcache_init(void);
void bcache_write_at(block_sector_t sector, void *in, off_t offset, size_t length);
void bcache_write(block_sector_t sector, void *in);
void bcache_read(block_sector_t sector, void *out);
void bcache_sync(void);