#include <bitmap.h>


void swap_init();
void swap_in(size_t swap_idx, void *paddr);
int swap_out(void *paddr);
void swap_free(uint32_t swap_idx);