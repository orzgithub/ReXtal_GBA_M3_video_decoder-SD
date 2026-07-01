// To make it work with a different flash cart, you need to implement all the must features.
#include <gba_base.h>
#include "../fatfs/ff.h"

void IWRAM_CODE pram_write(uint32_t phys_offset, const void* data, uint32_t size);

// You can selectively implement some optional features.
#define FEATURE_RETURN_MENU 1

#if FEATURE_RETURN_MENU
void IWRAM_CODE return_menu(void);
#endif