// To make it work with a different flash cart, you need to implement all the must features.
#include <gba_base.h>
#include "../fatfs/ff.h"

void IWRAM_CODE pram_write(uint32_t phys_offset, const void* data, uint32_t size);
