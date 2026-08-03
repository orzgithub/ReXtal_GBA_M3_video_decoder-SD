#ifndef CART_INTERFACE_H
#define CART_INTERFACE_H

#include <gba_base.h>
#include "../fatfs/ff.h"

#define FEATURE_RETURN_MENU 1

void IWRAM_CODE pram_write(uint32_t phys_offset, const void *data, uint32_t size);
void IWRAM_CODE return_menu(void);

#endif
