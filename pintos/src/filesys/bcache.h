#include "devices/block.h"

void bcache_init(void);
void bcache_write(block_sector_t sector, uint8_t *in);
void bcache_read(block_sector_t sector, uint8_t *out);
void bcache_sync(void);