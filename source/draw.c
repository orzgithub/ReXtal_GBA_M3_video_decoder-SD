#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <gba_base.h>
#include <gba_dma.h>
#include <string.h>

#include "font_chinese.h"
#include "font_ascii.h"
#include "draw.h"

#include "images.h"

//******************************************************************************
void fill_color(u16 x, u16 y, u16 width, u16 height, u16 color)
{
	u16 *vram = (u16 *)VRAM;

	u16 clamped_height = (y + height > 160) ? 160 : (y + height);
	u16 clamped_width  = (x + width  > 240) ? (240 - x) : width;

	u16 scanline[240];
	for (u32 i = 0; i < 240; i++)
		scanline[i] = color;

	for (u16 row = y; row < clamped_height; row++)
		dmaCopy(scanline, vram + row * 240 + x, clamped_width * 2);
}

//******************************************************************************
void fill_bg_pic(u16 *bg_image, u16 x, u16 y, u16 width, u16 height)
{
	u16 *vram = (u16 *)VRAM;

	u16 clamped_height = (y + height > 160) ? 160 : (y + height);
	u16 clamped_width  = (x + width  > 240) ? (240 - x) : width;

	for (u16 row = y; row < clamped_height; row++)
		dmaCopy(bg_image + row * 240 + x, vram + row * 240 + x, clamped_width * 2);
}

//******************************************************************************
void draw_pic(u16 *image, u16 x, u16 y, u16 width, u16 height, u8 use_transparency, u16 transparent_color)
{
	u16 *vram = (u16 *)VRAM;

	u16 clamped_height = (y + height > 160) ? 160 : (y + height);
	u16 clamped_width  = (x + width  > 240) ? (240 - x) : width;

	if (use_transparency)
	{
		for (u16 row = y; row < clamped_height; row++)
		{
			u16 src_row_offset = (row - y) * width;
			u16 dst_row_offset = row * 240;

			for (u16 col = 0; col < clamped_width; col++)
			{
				u16 pixel = image[src_row_offset + col];
				if (pixel != transparent_color)
					vram[dst_row_offset + x + col] = pixel;
			}
		}
	}
	else
	{
		for (u16 row = y; row < clamped_height; row++)
			dmaCopy(image + (row - y) * width, vram + row * 240 + x, clamped_width * 2);
	}
}

void draw_text(char *str, u16 max_chars, u16 x, u16 y, u16 color)
{
	if (!str) return;

	u16 length;
	if (max_chars == 0)
		length = strlen(str);
	else
		length = (max_chars < strlen(str)) ? max_chars : strlen(str);

	u16 char_index = 0;
	u16 cursor_x = x;
	u16 *vram = (u16 *)VRAM;

	while (char_index < length)
	{
		u8 first_byte = str[char_index++];

		if (first_byte < 0x80)
		{
			if (cursor_x + 6 > 240)
				break;

			u32 font_offset = first_byte * 12;
			u16 *dest = vram + y * 240 + cursor_x;
			const u8 *glyph = &ascii_1208[font_offset];

			for (int row = 0; row < 12; row++)
			{
				u8 bits = glyph[row];
				if (bits & 0x80) dest[0] = color;
				if (bits & 0x40) dest[1] = color;
				if (bits & 0x20) dest[2] = color;
				if (bits & 0x10) dest[3] = color;
				if (bits & 0x08) dest[4] = color;
				if (bits & 0x04) dest[5] = color;
				if (bits & 0x02) dest[6] = color;
				if (bits & 0x01) dest[7] = color;
				dest += 240;
			}
			cursor_x += 6;
		}
		else
		{
			if (char_index >= length)
				break;

			u8 second_byte = str[char_index++];

			if (cursor_x + 12 > 240)
				break;

			u32 font_offset;
			if (first_byte < 0xb0)
				font_offset = ((first_byte - 0xa1) * 94 + (second_byte - 0xa1)) * 24;
			else
				font_offset = (9 * 94 + (first_byte - 0xb0) * 94 + (second_byte - 0xa1)) * 24;

			u16 *dest = vram + y * 240 + cursor_x;
			const u8 *glyph = &chinese_1212[font_offset];

			for (int row = 0; row < 12; row++)
			{
				u8 left_byte  = glyph[row * 2];
				u8 right_byte = glyph[row * 2 + 1];

				if (left_byte & 0x80) dest[0] = color;
				if (left_byte & 0x40) dest[1] = color;
				if (left_byte & 0x20) dest[2] = color;
				if (left_byte & 0x10) dest[3] = color;
				if (left_byte & 0x08) dest[4] = color;
				if (left_byte & 0x04) dest[5] = color;
				if (left_byte & 0x02) dest[6] = color;
				if (left_byte & 0x01) dest[7] = color;

				if (right_byte & 0x80) dest[8]  = color;
				if (right_byte & 0x40) dest[9]  = color;
				if (right_byte & 0x20) dest[10] = color;
				if (right_byte & 0x10) dest[11] = color;
				if (right_byte & 0x08) dest[12] = color;
				if (right_byte & 0x04) dest[13] = color;
				if (right_byte & 0x02) dest[14] = color;
				if (right_byte & 0x01) dest[15] = color;

				dest += 240;
			}
			cursor_x += 12;
		}
	}
}