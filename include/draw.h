#include <gba_base.h>

void fill_color(u16 x, u16 y, u16 width, u16 height, u16 color);
void fill_bg_pic(u16 *bg_image, u16 x, u16 y, u16 width, u16 height);
void draw_pic(u16 *image, u16 x, u16 y, u16 width, u16 height, u8 use_transparency, u16 transparent_color);
void draw_text(char *str, u16 max_chars, u16 x, u16 y, u16 color);