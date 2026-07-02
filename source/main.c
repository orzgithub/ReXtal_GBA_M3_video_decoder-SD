/*
 * GBA Media Player
 *
 * Combined video (GBM) and audio (GBS) player.
 * Loads media files from SD card via FatFS into PRAM windows.
 *
 * Features:
 * - 10 FPS video with frame rate control
 * - A/V sync every 600 frames (1 minute) at I-frames
 * - A button to pause/resume
 * - B button to exit playback
 * - L/R buttons for seeking by minute
 * - START button to restart from beginning
 */

#include <gba.h>
#include <gba_base.h>
#include <gba_console.h>
#include <gba_interrupt.h>
#include <gba_input.h>
#include <gba_systemcalls.h>
#include <gba_video.h>
#include <stdio.h>
#include <string.h>

#include "media_source.h"
#include "gbs_audio.h"
#include "gbm_decoder.h"

#include "draw.h"
#include "images.h"

#pragma GCC push_options
#pragma GCC optimize("O3")

// ─── Fast frame copy using ARM ldmia/stmia ──────────────────────
// Unlike DMA, CPU memory access doesn't monopolize the bus and won't
// starve audio FIFO DMA. Audio DMA has higher priority and can interleave.
//
// Copies 128 bytes per iteration (4x unrolled, 32 bytes each).
// Total: 76800 bytes = 600 iterations.
__attribute__((target("arm"), noinline))
static void copy_frame_to_vram(const void* src, void* dst, u32 size) {
    asm volatile(
        "1:                         \n"
        "   ldmia %[src]!, {r2-r9}  \n"
        "   stmia %[dst]!, {r2-r9}  \n"
        "   ldmia %[src]!, {r2-r9}  \n"
        "   stmia %[dst]!, {r2-r9}  \n"
        "   ldmia %[src]!, {r2-r9}  \n"
        "   stmia %[dst]!, {r2-r9}  \n"
        "   ldmia %[src]!, {r2-r9}  \n"
        "   stmia %[dst]!, {r2-r9}  \n"
        "   subs  %[size], %[size], #128 \n"
        "   bgt   1b                \n"
        : [src] "+r" (src), [dst] "+r" (dst), [size] "+r" (size)
        :
        : "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "memory", "cc"
    );
}

// EWRAM buffer for video frame (240 * 160 = 38400 pixels = 76800 bytes)
u16 frame_buffer[38400] EWRAM_BSS;

// Frame rate control
// Video is 10 FPS, VBlank is 60 Hz, so 1 frame = 6 VBlanks
#define VBLANKS_PER_FRAME 6

// I-frame interval: 600 frames = 1 minute at 10 FPS
#define FRAMES_PER_MINUTE 600

// Maximum minutes for I-frame offset table (~4 hours)
#define MAX_MINUTES 256

// ─── Playback state ─────────────────────────────────────────────
static struct {
    MediaStream video_stream;
    MediaStream audio_stream;
    bool has_video;
    bool has_audio;
    u32 video_offset;
    u32 video_size;

    // target_frame: incremented by VBlank ISR, represents "should have displayed this many frames"
    // current_frame: maintained by main loop, represents "have decoded this many frames"
    u32 target_frame;
    u32 current_frame;

    // For tracking current minute (for sync and seeking)
    u32 current_minute;

    // Pause state
    bool is_paused;

    // Exit flag (set by B button)
    bool exit_requested;

    u32 total_minutes;
    // Stall protection
    u32 stall_counter;
} pb;

// ─── VBlank interrupt handler ───────────────────────────────────
// Called at 60 Hz, increment target_frame every 6 VBlanks (10 FPS)
// Don't increment when paused
__attribute__((used, noinline))
static IWRAM_CODE void vblank_handler(void) {
    if (pb.is_paused) return;

    static u8 vblank_counter = 0;
    vblank_counter++;
    if (vblank_counter >= VBLANKS_PER_FRAME) {
        vblank_counter = 0;
        pb.target_frame++;
    }
}

// ─── Error screen ───────────────────────────────────────────────
static void show_error(const char* msg) {
    consoleDemoInit();
    iprintf("\x1b[2J");
    iprintf("Ausar's M3 Media Player\n");
    iprintf("================\n\n");
    iprintf("Error: %s\n", msg);
    while (1) VBlankIntrWait();
}

// ─── Info screen ────────────────────────────────────────────────
static void show_playback_info(void) {
    char line1[64];
    char line2[64];
    char line3[64];
    char line4[64];
    
    if (pb.has_video) {
        snprintf(line1, 64, "Video: Yes (%lu KB)", (unsigned long)(pb.video_size / 1024));
        snprintf(line3, 64, "B:Exit A:Pause L/R:Seek");
    } else {
        snprintf(line1, 64, "Video: Not found");
        snprintf(line3, 64, "B:Exit A:Pause");
    }

    if (pb.has_audio) {
        const GbsAudioInfo* info = gbs_audio_get_info();
        uint32_t duration = info->total_samples / info->sample_rate;
        snprintf(line2, 64, "Audio: Mode %d, %lu sec", info->mode, (unsigned long)duration);
    } else {
        snprintf(line2, 64, "Audio: Not found");
    }
    snprintf(line4, 64, "START:Restart");

    // Clear text area with dark background
    fill_color(0, 0, 240, 160, gl_color_cheat_black);

    // Draw title
    draw_text("Ausar's M3 Media Player", 0, 10, 2, gl_color_text);

    // Draw info lines
    draw_text(line1, 0, 10, 20, gl_color_text);
    draw_text(line2, 0, 10, 34, gl_color_text);
    draw_text(line3, 0, 10, 48, gl_color_text);
    draw_text(line4, 0, 10, 62, gl_color_text);
}

// ─── Initialize video display ───────────────────────────────────
static void init_video_display(void) {
    // Mode 3: 240x160, 15-bit color
    SetMode(MODE_3 | BG2_ENABLE);

    // Clear frame buffer
    memset(frame_buffer, 0, sizeof(frame_buffer));

    // Clear VRAM
    u16* vram = (u16*)0x06000000;
    for (int i = 0; i < 38400; i++) {
        vram[i] = 0;
    }
}

static u32 frame_window_left = 0;
static bool refill_video_window(u32 start_offset) {
    if (!pb.has_video) return false;

    bool was_playing = false;
    if (!pb.is_paused && pb.has_audio) {
        pb.is_paused = true;
        gbs_audio_pause();
        was_playing = true;
    }
    
    u32 load_offset = start_offset;
    u32 load_size = pb.video_stream.window_size;
    
    if (load_offset + load_size > pb.video_stream.file_size) {
        if (pb.video_stream.file_size >= load_size) {
            load_offset = pb.video_stream.file_size - load_size;
        } else {
            load_offset = 0;
            load_size = pb.video_stream.file_size;
        }
    }
    
    const u8* ptr = media_stream_get_ptr(&pb.video_stream, load_offset, load_size);
    if (!ptr) {
        frame_window_left = 0;
        return false;
    }

    u32 frame_count = 0;
    u32 pos = 0;
    u32 window_end = load_size;
    
    while (pos + 2 <= window_end) {
        u16 frame_len = ptr[pos] | (ptr[pos + 1] << 8);
        if (frame_len == 0 || frame_len == 0xFFFF) break;
        if (pos + 2 + frame_len > window_end) break;
        pos += 2 + frame_len;
        frame_count++;
    }
    
    frame_window_left = frame_count;
    if (frame_window_left == 0) {
        frame_window_left = 1;
    }

    if (was_playing) {
        pb.is_paused = false;
        gbs_audio_resume();
    }
    return true;
}

// ─── Ensure video window covers the needed range ────────────────
// Returns true if window is ready, false on failure
static bool ensure_video_window(u32 offset, u32 length) {
    if (!pb.has_video) return false;
    
    if (pb.video_stream.window_valid &&
        offset >= pb.video_stream.window_offset &&
        offset + length <= pb.video_stream.window_offset + pb.video_stream.window_size) {
        return true;
    }
    
    bool was_playing = false;
    if (!pb.is_paused) {
        pb.is_paused = true;
        if (pb.has_audio) {
            gbs_audio_pause();
        }
        was_playing = true;
    }
    
    u32 new_offset = offset;
    if (new_offset + pb.video_stream.window_size > pb.video_stream.file_size) {
        if (pb.video_stream.file_size >= pb.video_stream.window_size) {
            new_offset = pb.video_stream.file_size - pb.video_stream.window_size;
        } else {
            new_offset = 0;
        }
    }
    
    u32 load_size = pb.video_stream.window_size;
    if (new_offset + load_size > pb.video_stream.file_size) {
        load_size = pb.video_stream.file_size - new_offset;
    }
    
    const u8* ptr = media_stream_get_ptr(&pb.video_stream, new_offset, load_size);
    
    bool success = false;
    if (ptr != NULL) {
        success = (pb.video_stream.window_valid &&
                   offset >= pb.video_stream.window_offset &&
                   offset + length <= pb.video_stream.window_offset + pb.video_stream.window_size);
    }
    
    if (was_playing) {
        pb.is_paused = false;
        if (pb.has_audio) {
            gbs_audio_resume();
        }
    }
    
    return success;
}

// ─── Ensure audio window covers the needed range ────────────────
// Returns true if window is ready, false on failure
static bool ensure_audio_window(u32 offset, u32 length) {
    if (!pb.has_audio) return false;
    
    if (pb.audio_stream.full_loaded) return true;
    
    if (pb.audio_stream.window_valid &&
        offset >= pb.audio_stream.window_offset &&
        offset + length <= pb.audio_stream.window_offset + pb.audio_stream.window_size) {
        return true;
    }
    
    bool was_playing = false;
    if (!pb.is_paused) {
        pb.is_paused = true;
        if (pb.has_audio) {
            gbs_audio_pause();
        }
        was_playing = true;
    }
    
    u32 new_offset = offset;
    if (new_offset + pb.audio_stream.window_size > pb.audio_stream.file_size) {
        if (pb.audio_stream.file_size >= pb.audio_stream.window_size) {
            new_offset = pb.audio_stream.file_size - pb.audio_stream.window_size;
        } else {
            new_offset = 0;
        }
    }
    
    u32 load_size = pb.audio_stream.window_size;
    if (new_offset + load_size > pb.audio_stream.file_size) {
        load_size = pb.audio_stream.file_size - new_offset;
    }
    
    const u8* ptr = media_stream_get_ptr(&pb.audio_stream, new_offset, load_size);
    
    bool success = false;
    if (ptr != NULL) {
        success = (pb.audio_stream.window_valid &&
                   offset >= pb.audio_stream.window_offset &&
                   offset + length <= pb.audio_stream.window_offset + pb.audio_stream.window_size);
    }
    
    if (was_playing) {
        pb.is_paused = false;
        if (pb.has_audio) {
            gbs_audio_resume();
        }
    }
    
    return success;
}

static const u8* get_audio_ptr(u32 offset, u32 length) {
    if (!pb.has_audio) return NULL;
    return media_stream_get_ptr(&pb.audio_stream, offset, length);
}

// ─── Scan video to find I-frame offsets (every 600 frames) ─────
static void scan_iframe_offsets(void) {
    if (!pb.has_video) return;

    u32 scan_pos = GBM_HEADER_SIZE;
    u32 total_samples = 0;
    u32 total_bytes = 0;
    u32 avg_frame_size = 7680;
    
    for (int i = 0; i < 20 && scan_pos < pb.video_size; i++) {
        if (!ensure_video_window(scan_pos, 2)) break;
        const u8* ptr = media_stream_get_ptr(&pb.video_stream, scan_pos, 2);
        if (!ptr) break;
        u16 frame_len = ptr[0] | (ptr[1] << 8);
        if (frame_len == 0 || frame_len == 0xFFFF) break;
        total_bytes += 2 + frame_len;
        total_samples++;
        scan_pos += 2 + frame_len;
    }
    
    if (total_samples > 0) {
        avg_frame_size = total_bytes / total_samples;
    }
    
    u32 data_size = pb.video_size - GBM_HEADER_SIZE;
    u32 total_frames = data_size / avg_frame_size;
    pb.total_minutes = total_frames / FRAMES_PER_MINUTE;
    
    if (pb.total_minutes == 0) pb.total_minutes = 1;
    if (pb.total_minutes > MAX_MINUTES) pb.total_minutes = MAX_MINUTES;
    
    frame_window_left = 0;
    refill_video_window(GBM_HEADER_SIZE);
}

// ─── Seek video to a specific minute (jumps to I-frame) ────────
static bool video_seek_minute(u32 minute) {
    if (!pb.has_video || pb.total_minutes == 0) return false;
    if (minute >= pb.total_minutes) minute = pb.total_minutes - 1;
    
    u32 target_frame = minute * FRAMES_PER_MINUTE;
    u32 current_frame = 0;
    u32 scan_offset = GBM_HEADER_SIZE;
    
    while (current_frame < target_frame && scan_offset < pb.video_size) {
        if (!ensure_video_window(scan_offset, 2)) break;
        const u8* ptr = media_stream_get_ptr(&pb.video_stream, scan_offset, 2);
        if (!ptr) break;
        u16 frame_len = ptr[0] | (ptr[1] << 8);
        if (frame_len == 0 || frame_len == 0xFFFF) break;
        scan_offset += 2 + frame_len;
        current_frame++;
    }
    
    pb.video_offset = scan_offset;
    pb.current_minute = minute;
    pb.current_frame = target_frame;
    pb.target_frame = pb.current_frame;
    pb.stall_counter = 0;
    
    refill_video_window(scan_offset);
    
    return true;
}

// ─── Seek both audio and video to a specific minute ─────────────
static void seek_to_minute(u32 minute) {
    if (pb.total_minutes == 0) return;
    if (minute >= pb.total_minutes) {
        minute = pb.total_minutes - 1;
    }

    // Pause during seek
    bool was_paused = pb.is_paused;
    pb.is_paused = true;
    if (pb.has_audio) {
        gbs_audio_pause();
    }

    bool success = true;
    if (pb.has_video) {
        success = video_seek_minute(minute);
    }

    if (pb.has_audio && success) {
        gbs_audio_seek_minute(minute);
        // Ensure audio window is ready after seek
        // (gbs_audio should handle its own window management)
    }

    if (success) {
        pb.current_minute = minute;
    }

    // Resume if was playing
    if (!was_paused) {
        pb.is_paused = false;
        if (pb.has_audio) {
            gbs_audio_resume();
        }
    }
}

// ─── Toggle pause state for both audio and video ────────────────
static void toggle_pause(void) {
    pb.is_paused = !pb.is_paused;
    
    if (pb.has_audio) {
        if (pb.is_paused) {
            gbs_audio_pause();
        } else {
            gbs_audio_resume();
        }
    }
}


static const u8* get_video_data(u32 offset, u32 length) {
    if (!pb.has_video) return NULL;
    return media_stream_get_ptr(&pb.video_stream, offset, length);
}

// ─── Decode next frame into frame_buffer (does not display) ────
// Returns true on success, false on error (state already reset)
static u8 frame_not_handle_input = 0; // Avoid input never being handled if video is stalled.
static bool decode_next_frame(void) {
    if (!pb.has_video) return false;

    do {
        if (frame_window_left == 0) {
            if (!refill_video_window(pb.video_offset)) break;
        }
        
        if (pb.video_offset >= pb.video_size) break;

        const u8* ptr = get_video_data(pb.video_offset, 2);
        if (!ptr) break;
        
        u16 frame_len = ptr[0] | (ptr[1] << 8);
        if (frame_len == 0 || frame_len == 0xFFFF) break;

        ptr = get_video_data(pb.video_offset, 2 + frame_len);
        if (!ptr) break;

        u32 next_offset = gbm_decode_frame(ptr, 0,
                                           frame_buffer,
                                           (const u16*)0x06000000);
        if (next_offset == 0) break;

        pb.video_offset += next_offset;
        pb.stall_counter = 0;
        
        frame_window_left--;
        frame_not_handle_input++;
        
        return true;
    } while (false);
    
    pb.video_offset = GBM_HEADER_SIZE;
    pb.current_frame = 0;
    pb.target_frame = 0;
    pb.current_minute = 0;
    pb.stall_counter = 0;
    frame_window_left = 0;
    return false;
}

// ─── Check if audio triggered a sync point ──────────────────────
static void check_audio_sync(void) {
    if (!pb.has_audio || !pb.has_video) return;

    int32_t sync_minute = gbs_audio_check_minute_sync();
    if (sync_minute >= 0 && (u32)sync_minute < pb.total_minutes) {
        video_seek_minute((u32)sync_minute);
    }
}

// ─── Handle input, returns true if state changed ────────────────
static void handle_input(void) {
    frame_not_handle_input = 0;
    scanKeys();
    u16 keys = keysDown();

    // B: exit playback
    if (keys & KEY_B) {
        pb.exit_requested = true;
        return;
    }

    // A: toggle pause/resume
    if (keys & KEY_A) {
        toggle_pause();
        return;
    }

    // START: restart from beginning
    if (keys & KEY_START) {
        if (pb.is_paused) {
            toggle_pause();
        }
        seek_to_minute(0);
        return;
    }

    // R: skip forward 1 minute
    if (keys & KEY_R) {
        u32 next_minute = pb.current_minute + 1;
        if (next_minute < pb.total_minutes) {
            seek_to_minute(next_minute);
        }
        return;
    }

    // L: skip backward 1 minute
    if (keys & KEY_L) {
        if (pb.current_minute > 0) {
            seek_to_minute(pb.current_minute - 1);
        } else {
            seek_to_minute(0);
        }
        return;
    }
}

// ─── Process video frames with frame rate control ───────────────
static void process_video(void) {
    bool decode_ok = decode_next_frame();
    if (!decode_ok) {
        VBlankIntrWait();
        return;
    }

    // Wait until it's time to display
    u32 wait_loops = 0;
    while (pb.current_frame >= pb.target_frame && !pb.exit_requested) {
        VBlankIntrWait();
        handle_input();
        wait_loops++;
        if (wait_loops > 300) {
            pb.target_frame = pb.current_frame + 1;
            break;
        }
    }
    if (frame_not_handle_input > 10) handle_input();
    if (pb.exit_requested) return;

    // Display the pre-decoded frame
    copy_frame_to_vram(frame_buffer, (void*)0x06000000, 240 * 160 * 2);
    pb.current_frame++;

    // Update current minute using subtraction loop
    {
        u32 frame = pb.current_frame;
        u32 minute = 0;
        while (frame >= FRAMES_PER_MINUTE) {
            frame -= FRAMES_PER_MINUTE;
            minute++;
        }
        pb.current_minute = minute;
    }
    
    pb.stall_counter = 0;
}

// ─── Cleanup after playback ─────────────────────────────────────
static void cleanup_playback(void) {
    if (pb.has_audio) {
        gbs_audio_stop();
        gbs_audio_shutdown();
    }

    for (volatile int i = 0; i < 10; i++) {
        VBlankIntrWait();
    }

    if (pb.has_audio) {
        media_stream_close(&pb.audio_stream);
    }
    if (pb.has_video) {
        media_stream_close(&pb.video_stream);
    }

    memset(&pb, 0, sizeof(pb));
}

// ─── Play video and audio ───────────────────────────────────────
void play_video(const char* video_path, const char* audio_path) {
    memset(&pb, 0, sizeof(pb));
    pb.video_offset = GBM_HEADER_SIZE;

    // ── Open video stream ──────────────────────────────────
    if (video_path && media_stream_open(&pb.video_stream, video_path,
                          VIDEO_WINDOW_BASE, VIDEO_WINDOW_SIZE)) {
        pb.has_video = true;
        pb.video_size = media_stream_size(&pb.video_stream);

        const u8* header = media_stream_get_ptr(&pb.video_stream, 0, GBM_HEADER_SIZE);
        if (header && header[0] == 'G' && header[1] == 'B' &&
            header[2] == 'A' && header[3] == 'M') {
            gbm_set_version(header[0x10]);
        } else {
            pb.has_video = false;
            media_stream_close(&pb.video_stream);
        }
    }

    // ── Open audio stream ──────────────────────────────────
    if (audio_path && media_stream_open(&pb.audio_stream, audio_path,
                          AUDIO_WINDOW_BASE, AUDIO_WINDOW_SIZE)) {
        if (pb.audio_stream.full_loaded) {
            const u8* gbs_data = media_stream_get_ptr(&pb.audio_stream, 0,
                                                      pb.audio_stream.file_size);
            if (gbs_data && gbs_audio_init(gbs_data, pb.audio_stream.file_size)) {
                pb.has_audio = true;
            }
        } else {
            // Audio file larger than cache - init with first window
            const u8* initial_data = media_stream_get_ptr(&pb.audio_stream, 0,
                                                           AUDIO_WINDOW_SIZE);
            if (initial_data && gbs_audio_init(initial_data, pb.audio_stream.file_size)) {
                pb.has_audio = true;
            }
        }
        
        if (!pb.has_audio) {
            media_stream_close(&pb.audio_stream);
        }
    }

    if (!pb.has_video && !pb.has_audio) {
        cleanup_playback();
        return;
    }

    show_playback_info();

    for (int i = 0; i < 180; i++) {
        VBlankIntrWait();
        scanKeys();
        if (keysDown() & KEY_A) break;
    }

    // ── Start playback ──────────────────────────────────────
    if (pb.has_video) {
        init_video_display();
        scan_iframe_offsets();
    }

    pb.target_frame = 0;
    pb.current_frame = 0;
    pb.current_minute = 0;
    pb.exit_requested = false;
    pb.is_paused = false;
    pb.stall_counter = 0;

    if (pb.has_video) {
        decode_next_frame();
        copy_frame_to_vram(frame_buffer, (void*)0x06000000, 240 * 160 * 2);
        pb.current_frame = 1;
    }

    if (pb.has_audio) {
        gbs_audio_set_window_callback(ensure_audio_window);
        gbs_audio_set_get_ptr_callback(get_audio_ptr);
        gbs_audio_start();
    }

    // ── Main loop ───────────────────────────────────────────
    while (!pb.exit_requested) {
        check_audio_sync();

        if (pb.has_video) {
            process_video();
        } else {
            VBlankIntrWait();
            handle_input();
            if (pb.exit_requested) break;
        }

        if (pb.has_audio && gbs_audio_is_finished()) {
            gbs_audio_restart();
            if (pb.has_video) {
                seek_to_minute(0);
            }
        }
        
        if (pb.has_video) {
            pb.stall_counter++;
            if (pb.stall_counter > 1800) {
                pb.stall_counter = 0;
                
                pb.video_offset = GBM_HEADER_SIZE;
                pb.current_frame = 0;
                pb.target_frame = 0;
                pb.current_minute = 0;
                if (pb.has_audio) {
                    gbs_audio_restart();
                }
            }
        }
    }

    cleanup_playback();
}

// ─── Play audio only ────────────────────────────────────────────
void play_audio(const char* audio_path) {
    play_video(NULL, audio_path);
}

// ─── Simple SD file browser ─────────────────────────────────────
#define MAX_FILES      512
#define MAX_FOLDERS    128
#define MAX_PATH_LEN   256

typedef struct {
    char name[64];
    u32  size;
    bool is_folder;
} DirEntry;

enum menu_page {
    SD,
    ABOUT,
    MENU_NUM
};

static DirEntry entries[MAX_FILES + MAX_FOLDERS] EWRAM_BSS;
static u32 entry_count;
static u32 folder_count;
static char current_path[MAX_PATH_LEN];

static void sort_entries(void) {
    for (u32 i = 0; i < entry_count; i++) {
        for (u32 j = i + 1; j < entry_count; j++) {
            const DirEntry* a = &entries[i];
            const DirEntry* b = &entries[j];
            
            bool swap = false;
            if (a->is_folder != b->is_folder) {
                swap = !a->is_folder;
            } else {
                swap = (strcasecmp(a->name, b->name) > 0);
            }
            
            if (swap) {
                DirEntry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }
}

static bool has_ext(const char* name, const char* ext) {
    const char* dot = strrchr(name, '.');
    if (!dot) return false;
    return strcasecmp(dot + 1, ext) == 0;
}

static bool is_gbs(const char* name) { return has_ext(name, "gbs"); }
static bool is_gbm(const char* name) { return has_ext(name, "gbm"); }

static void scan_directory(void) {
    entry_count = 0;
    folder_count = 0;

    DIR dir;
    FILINFO fno;
    FRESULT res = f_opendir(&dir, current_path);
    if (res != FR_OK) return;

    while (f_readdir(&dir, &fno) == FR_OK && fno.fname[0] != 0) {
        if (strcmp(fno.fname, ".") == 0) continue;

        if (entry_count >= MAX_FILES + MAX_FOLDERS) break;

        DirEntry* e = &entries[entry_count];

        if (fno.fattrib & AM_DIR) {
            strncpy(e->name, fno.fname, 63);
            e->name[63] = '\0';
            e->size = 0;
            e->is_folder = true;
            folder_count++;
            entry_count++;
        } else if (is_gbs(fno.fname) || is_gbm(fno.fname)) {
            strncpy(e->name, fno.fname, 63);
            e->name[63] = '\0';
            e->size = fno.fsize;
            e->is_folder = false;
            entry_count++;
        }
    }
    f_closedir(&dir);

    sort_entries();
}

static void draw_about(void) {
    draw_pic((u16*)gImage_BG, 0, 0, 240, 160, false, 0);
    draw_text("<L", 2, 2, 2, gl_color_MENU_btn);
    draw_text("About", 10, 100, 2, gl_color_text);
    draw_text("GBA M3 Player by Ausar", 30, 10, 20, gl_color_text);
    draw_text("SD Support by ZaindORp", 26, 10, 34, gl_color_text);
    draw_text("Original project:", 18, 10, 48, gl_color_text);
    draw_text("ArcheyChen/ReXtal_GBA_M3", 28, 10, 62, gl_color_text);
    draw_text("_video_decoder", 15, 10, 76, gl_color_text);
}

static void draw_list(u32 selected) {
    draw_pic((u16*)gImage_BG, 0, 0, 240, 160, false, 0);
    
    char title[64];
    snprintf(title, 64, "SD: %s", current_path);
    draw_text(title, 30, 2, 2, gl_color_text);
    draw_text("R>", 2, 226, 2, gl_color_MENU_btn);

    u32 start = (selected / 10) * 10;
    for (u32 i = 0; i < 10 && (start + i) < entry_count; i++) {
        u32 idx = start + i;
        DirEntry* e = &entries[idx];
        u32 y = 20 + i * 14;

        u16 color = (idx == selected) ? gl_color_selected : gl_color_text;

        char prefix[4];
        if (e->is_folder) {
            snprintf(prefix, 4, "[D]");
        } else if (is_gbm(e->name)) {
            snprintf(prefix, 4, "[V]");
        } else {
            snprintf(prefix, 4, "[A]");
        }
        draw_text(prefix, 3, 2, y, color);
        draw_text(e->name, 24, 20, y, color);

        if (!e->is_folder) {
            char size_str[16];
            if (e->size >= 1048576)
                snprintf(size_str, 16, "%luM", (unsigned long)(e->size / 1048576));
            else if (e->size >= 1024)
                snprintf(size_str, 16, "%luK", (unsigned long)(e->size / 1024));
            else
                snprintf(size_str, 16, "%luB", (unsigned long)e->size);
            draw_text(size_str, 0, 190, y, color);
        }
    }

    if (entry_count > 10) {
        u32 bar_h = (10 * 140) / entry_count;
        if (bar_h < 4) bar_h = 4;
        u32 bar_y = 20 + (selected * 140) / entry_count;
        fill_color(235, bar_y, 4, bar_h, gl_color_MENU_btn);
    }
}

static void get_pair_path(const char* base_name, const char* ext, char* out) {
    char temp[64];
    strncpy(temp, base_name, 63);
    temp[63] = '\0';
    char* dot = strrchr(temp, '.');
    if (dot) *dot = '\0';

    if (current_path[1] == '\0') {
        snprintf(out, MAX_PATH_LEN, "/%s.%s", temp, ext);
    } else {
        snprintf(out, MAX_PATH_LEN, "%s/%s.%s", current_path, temp, ext);
    }
}

void menu(void) {
    strcpy(current_path, "/");
    scan_directory();

    u32 selected = 0;
    enum menu_page cur_menu_page = SD;
    bool redraw = true;

    while (1) {
        if (redraw) {
            switch (cur_menu_page) {
            case SD:
                draw_list(selected);
                break;
            case ABOUT:
                draw_about();
                break;
            default:
                break;
            }
            redraw = false;
        }

        VBlankIntrWait();
        scanKeys();
        u16 keys = keysDown();
        u16 keys_rep = keysDownRepeat();

        if (keys & KEY_R) {
            if (cur_menu_page < MENU_NUM - 1) {
                cur_menu_page++;
                redraw = true;
            }
            continue;
        }
        else if (keys & KEY_L) {
            if (cur_menu_page > 0) {
                cur_menu_page--;
                redraw = true;
            }
            continue;
        }
        else switch (cur_menu_page) {
        case SD:
            if (keys_rep & KEY_DOWN) {
                if (selected + 1 < entry_count) {
                    selected++;
                    redraw = true;
                }
            } else if (keys_rep & KEY_UP) {
                if (selected > 0) {
                    selected--;
                    redraw = true;
                }
            } else if (keys_rep & KEY_RIGHT) {
                if (selected + 10 < entry_count) {
                    selected += 10;
                    redraw = true;
                }
            } else if (keys_rep & KEY_LEFT) {
                if (selected >= 10) {
                    selected -= 10;
                } else {
                    selected = 0;
                }
                redraw = true;
            } else if (keys & KEY_A) {
                if (selected >= entry_count) continue;
                
                DirEntry* e = &entries[selected];
                if (e->is_folder) {
                    if (current_path[1] != '\0') strcat(current_path, "/");
                    strcat(current_path, e->name);
                    scan_directory();
                    selected = 0;
                    redraw = true;
                } else if (is_gbm(e->name)) {
                    char video_path[MAX_PATH_LEN];
                    char audio_path[MAX_PATH_LEN];
                    if (current_path[1] == '\0') {
                        snprintf(video_path, MAX_PATH_LEN, "/%s", e->name);
                    } else {
                        snprintf(video_path, MAX_PATH_LEN, "%s/%s", current_path, e->name);
                    }
                    get_pair_path(e->name, "gbs", audio_path);
                    
                    frame_not_handle_input = 0;
                    play_video(video_path, audio_path);
                    redraw = true;
                } else if (is_gbs(e->name)) {
                    char audio_path[MAX_PATH_LEN];
                    if (current_path[1] == '\0') {
                        snprintf(audio_path, MAX_PATH_LEN, "/%s", e->name);
                    } else {
                        snprintf(audio_path, MAX_PATH_LEN, "%s/%s", current_path, e->name);
                    }
                    
                    play_audio(audio_path);
                    redraw = true;
                }
            } else if (keys & KEY_B) {
                if (current_path[1] != '\0') {
                    char* slash = strrchr(current_path, '/');
                    if (slash == current_path) {
                        current_path[1] = '\0';
                    } else if (slash) {
                        *slash = '\0';
                    }
                    scan_directory();
                    selected = 0;
                    redraw = true;
                }
            }
            #if FEATURE_RETURN_MENU
            else if (keys & KEY_START) {
                return_menu();
            }
            #endif
            break;
        
        default:
            break;
        }
    }
}

// ─── Main entry point ───────────────────────────────────────────
int main(void) {
    irqInit();
    irqEnable(IRQ_VBLANK);
    irqSet(IRQ_VBLANK, vblank_handler);
    REG_IME = 1;

    SetMode(MODE_3 | BG2_ENABLE);

    if (!media_source_init()) {
        show_error("No SD card found!\nInsert SD with media files.");
    }

    
    set_custom_theme();
    menu();

    while (1);
    return 0;
}

#pragma GCC pop_options