/*
 * Media Source Implementation
 *
 * Provides media data (GBS audio, GBM video) from SD card via FatFS
 * using a large PRAM sliding window.
 */

#include <string.h>
#include <gba_base.h>
#include <stdio.h>
#include "media_source.h"

#pragma GCC push_options
#pragma GCC optimize("O3")

// FatFS object (global)
static FATFS fatfs_obj;

// Temporary buffer for file transfers (located in EWRAM)
static u8 transfer_buf[TRANSFER_CHUNK] EWRAM_BSS;

// Debug counter
static u32 window_load_count = 0;


// ── Internal: load a region of a file into a PSRAM window ────────
static bool load_window_region(FIL* file, u32 file_offset,
                               u8* dest, u32 length,
                               u8* window_base, u32 window_size) {
    u32 file_size = f_size(file);
    if (file_offset >= file_size) {
        return false;
    }
    if (file_offset + length > file_size) {
        length = file_size - file_offset;
    }
    if (length == 0) {
        return false;
    }

    if (dest < window_base || dest + length > window_base + window_size) {
        return false;
    }

    FRESULT res = f_lseek(file, file_offset);
    if (res != FR_OK) {
        return false;
    }

    #if EWRAM_AS_PRAM
        UINT br;
        res = f_read(file, window_base, length, &br);
        if (res != FR_OK || br != length) {
            return false;
        }
    #else
        u32 remaining = length;
        u32 dest_off = (u32)(dest - window_base);
        while (remaining > 0) {
            u32 chunk = remaining > TRANSFER_CHUNK ? TRANSFER_CHUNK : remaining;
            UINT br;
            res = f_read(file, transfer_buf, chunk, &br);
            if (res != FR_OK || br != chunk) {
                return false;
            }

            u32 phys_off = (u32)(window_base - (u8*)PRAM_BASE) + dest_off;
            pram_write(phys_off, transfer_buf, chunk);

            dest_off += chunk;
            remaining -= chunk;
        }
    #endif
    window_load_count++;
    return true;
}

// ── Public API ────────────────────────────────────────────────────
bool media_source_init(void) {
    window_load_count = 0;
    FRESULT res = f_mount(&fatfs_obj, "", 1);
    return (res == FR_OK);
}

u32 media_source_get_window_loads(void) {
    return window_load_count;
}

bool media_stream_open(MediaStream* stream, const char* filename,
                       uint8_t* window_base, uint32_t window_size) {
    if (!stream || !filename || !window_base || window_size == 0) 
        return false;
    
    memset(stream, 0, sizeof(*stream));

    FRESULT res = f_open(&stream->file, filename, FA_READ);
    if (res != FR_OK) return false;

    stream->file_size   = f_size(&stream->file);
    stream->window_base = window_base;
    stream->window_size = window_size;

    if (stream->file_size == 0) {
        f_close(&stream->file);
        return false;
    }

    if (stream->file_size <= window_size) {
        if (!load_window_region(&stream->file, 0, window_base, stream->file_size,
                                window_base, window_size)) {
            f_close(&stream->file);
            return false;
        }
        stream->full_loaded   = true;
        stream->full_cache    = window_base;
        stream->window_offset = 0;
        stream->window_valid  = true;
    } else {
        if (!load_window_region(&stream->file, 0, window_base, window_size,
                                window_base, window_size)) {
            f_close(&stream->file);
            return false;
        }
        stream->full_loaded   = false;
        stream->window_offset = 0;
        stream->window_valid  = true;
    }

    stream->pos = 0;
    return true;
}

u32 media_stream_read(MediaStream* stream, void* buf, u32 len) {
    if (!stream || !buf || len == 0) return 0;

    u32 available = stream->file_size - stream->pos;
    if (len > available) len = available;
    if (len == 0) return 0;

    const u8* ptr = media_stream_get_ptr(stream, stream->pos, len);
    if (!ptr) return 0;

    memcpy(buf, ptr, len);
    stream->pos += len;
    return len;
}

const uint8_t* media_stream_get_ptr(MediaStream* stream,
                                    uint32_t offset, uint32_t len) {
    if (!stream) return NULL;
    if (len == 0) return NULL;
    if (offset + len > stream->file_size) return NULL;

    if (stream->full_loaded) {
        return stream->full_cache + offset;
    }

    if (stream->window_valid &&
        offset >= stream->window_offset &&
        offset + len <= stream->window_offset + stream->window_size) {
        return stream->window_base + (offset - stream->window_offset);
    }

    uint32_t new_offset = offset;
    if (new_offset + stream->window_size > stream->file_size) {
        if (stream->file_size >= stream->window_size) {
            new_offset = stream->file_size - stream->window_size;
        } else {
            new_offset = 0;
        }
    }

    uint32_t load_size = stream->window_size;
    if (new_offset + load_size > stream->file_size) {
        load_size = stream->file_size - new_offset;
    }

    if (!load_window_region(&stream->file, new_offset,
                            stream->window_base, load_size,
                            stream->window_base, stream->window_size)) {
        stream->window_valid = false;
        return NULL;
    }

    stream->window_offset = new_offset;
    stream->window_valid = true;

    if (offset < stream->window_offset ||
        offset + len > stream->window_offset + load_size) {
        return NULL;
    }

    return stream->window_base + (offset - stream->window_offset);
}

bool media_stream_seek(MediaStream* stream, u32 pos) {
    if (!stream || pos > stream->file_size) return false;
    stream->pos = pos;
    return true;
}

u32 media_stream_tell(const MediaStream* stream) {
    if (!stream) return 0;
    return stream->pos;
}

u32 media_stream_size(const MediaStream* stream) {
    if (!stream) return 0;
    return stream->file_size;
}

void media_stream_close(MediaStream* stream) {
    if (stream) {
        f_close(&stream->file);
        memset(stream, 0, sizeof(*stream));
    }
}

#pragma GCC pop_options