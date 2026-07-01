/*
 * Media Source Abstraction Layer
 *
 * Provides a unified interface for loading media data from SD card via FatFS,
 * using a large sliding window in PSRAM.
 *
 * Supports both GBS (audio) and GBM (video) files.
 */

#ifndef MEDIA_SOURCE_H
#define MEDIA_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

#include "cart_interface.h"

#if EWRAM_AS_PRAM
    #include "ewram_pram.h"
#endif

#define PRAM_BASE         0x08000000
#define PRAM_SIZE         0x02000000

// Reserved area: 4 MB for the player itself
#define PRAM_RESERVED     (4 * 1024 * 1024)     

#ifndef VIDEO_WINDOW_SIZE
    #if EWRAM_AS_PRAM
        #define VIDEO_WINDOW_SIZE (32 * 1024)
    #else
        // Video window: 16 MB (located right after the reserved area)
        #define VIDEO_WINDOW_SIZE (16 * 1024 * 1024)
    #endif
#endif
#if EWRAM_AS_PRAM
    #define VIDEO_WINDOW_BASE  video_window
#else
    #define VIDEO_WINDOW_BASE  ((uint8_t*)PRAM_BASE + PRAM_RESERVED)
#endif

#ifndef AUDIO_WINDOW_SIZE
    #if EWRAM_AS_PRAM
        #define AUDIO_WINDOW_SIZE  (32 * 1024)
    #else
        // Audio window: 12 MB (immediately after video window)
        #define AUDIO_WINDOW_SIZE  (12 * 1024 * 1024)
    #endif
#endif
#if EWRAM_AS_PRAM
    #define AUDIO_WINDOW_BASE   audio_window
#else
    #define AUDIO_WINDOW_BASE   ((uint8_t*)PRAM_BASE + (PRAM_RESERVED + VIDEO_WINDOW_SIZE))
#endif

// Transfer chunk size (fits comfortably in EWRAM)
#define TRANSFER_CHUNK    (32 * 1024)

// Media source types (kept for compatibility)
typedef enum {
    MEDIA_SOURCE_NONE = 0,
    MEDIA_SOURCE_EMBEDDED,
    MEDIA_SOURCE_GBFS,
    MEDIA_SOURCE_SDCARD
} MediaSourceType;

// Media file types (kept for compatibility)
typedef enum {
    MEDIA_TYPE_UNKNOWN = 0,
    MEDIA_TYPE_GBS,
    MEDIA_TYPE_GBM
} MediaFileType;

// Media source info (kept for compatibility)
typedef struct {
    MediaSourceType source;
    MediaFileType type;
    const uint8_t* data;
    uint32_t size;
    char filename[32];
} MediaSourceInfo;

/*
 * Streaming file handle backed by a sliding window in PSRAM.
 */
typedef struct {
    FIL      file;               // FatFS file handle
    uint32_t file_size;
    uint32_t pos;                // current logical read position

    uint8_t* window_base;        // PSRAM address of the window
    uint32_t window_size;        // size of the window
    uint32_t window_offset;      // file offset corresponding to window_base
    bool     window_valid;       // true if the window contains valid data

    bool     full_loaded;        // true if the entire file fits in the window
    uint8_t* full_cache;         // if full_loaded, points to the base of the data
} MediaStream;

/*
 * Initialize the media source subsystem.
 * Mounts the SD card and prepares the PSRAM transfer buffer.
 * Returns true on success.
 */
bool media_source_init(void);

/*
 * Open a media file and associate it with a PSRAM sliding window.
 * If the file fits inside the window, it is loaded completely.
 * Otherwise the window will be loaded on demand.
 */
bool media_stream_open(MediaStream* stream, const char* filename,
                       uint8_t* window_base, uint32_t window_size);

/*
 * Read data from the stream into a buffer.
 * Returns the number of bytes actually read.
 */
uint32_t media_stream_read(MediaStream* stream, void* buf, uint32_t len);

/*
 * Ensure that [offset, offset+len) is present in the window and return
 * a pointer to the data inside PSRAM.
 * Returns NULL on failure.
 */
const uint8_t* media_stream_get_ptr(MediaStream* stream,
                                    uint32_t offset, uint32_t len);

/*
 * Set the logical read position without loading data.
 */
bool media_stream_seek(MediaStream* stream, uint32_t pos);

/*
 * Close the stream and release resources.
 */
void media_stream_close(MediaStream* stream);

/*
 * Return the total size of the file.
 */
u32 media_stream_size(const MediaStream* stream);

#endif // MEDIA_SOURCE_H
