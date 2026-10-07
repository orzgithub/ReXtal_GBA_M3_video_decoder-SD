#ifndef GBM_DECODER_H
#define GBM_DECODER_H

#include <gba_types.h>

#define FRAME_WIDTH 240
#define FRAME_HEIGHT 160
#define GBM_HEADER_SIZE 0x200

// GBM format versions
#define GBM_VERSION_GEN1 0x06  // XOR key 0xD669
#define GBM_VERSION_GEN3 0x05  // XOR key 0xD6AC
#define GBM_VERSION_V130 0x04  // No XOR (key 0x0000)
#define GBM_VERSION_SC   0x01  // SuperCard/FilmPlay clone, no XOR (key 0x0000)

// Video decode hot path.
//
// The block-tree dispatcher and the bit readers are pure 32-bit integer code
// with no 8-bit data handling, so compiling them for ARM beats Thumb here:
// IWRAM is a 32-bit zero-wait-state bus, so an ARM instruction costs the same
// fetch cycle as a Thumb one while doing more work, ARM has conditional
// execution and real register-register addressing (which the block tree's
// switch + offset arithmetic uses heavily), and ARM can move all 16 registers
// in one ldmia/stmia.
//
// Only the dispatcher side is moved. gbm_decode_frame, decode_block_8x8 (the
// per-8x8-block entry point) and the two tiny special cases decode_block_1x2 /
// decode_block_2x1 are GBM_HOT_CODE. The twelve larger leaf decoders
// (decode_block_8x4 ... decode_block_4x1, defined below) are NOT: they stay
// Thumb and keep the IWRAM_CODE they always had. That split is deliberate --
// ARM code is roughly twice the size of Thumb, the twelve leaves are ~2.5 KiB
// of the decoder, and compiling them for ARM too overflows IWRAM in the ez
// build. Measured on mGBA with the leaves left as Thumb, the speed is identical
// to the all-ARM build (283/283 vs 283/283 frames) at a third of the size cost.
//
// Cost: .iwram 0x3d10 -> 0x4820 in the image build (+1632 B) and
// 0x41c0 -> 0x4cd0 in the ez build (+2832 B), which leaves the ez build
// ~1.7 KiB of IWRAM headroom.
#define GBM_HOT_CODE __attribute__((section(".iwram"), long_call, target("arm")))

// Escape hatch: GBM_HOT_IN_IWRAM=0 keeps the dispatcher ARM and inlined but
// leaves it in cartridge ROM (.text) instead of IWRAM. Use it if a future
// change overflows .iwram in some cart target -- it links anywhere, at the cost
// of ROM wait states on every hot-path fetch (measured: 92.6% vs the 100% of
// the IWRAM placement, i.e. barely better than not doing this at all).
#ifndef GBM_HOT_IN_IWRAM
#define GBM_HOT_IN_IWRAM 1
#endif

#if !GBM_HOT_IN_IWRAM
#undef GBM_HOT_CODE
#define GBM_HOT_CODE __attribute__((long_call, target("arm")))
#endif

// Bit/byte readers on the critical path.
//
// next_bit/next_2bits are entered once per node of the block tree (and
// next_2bits once per block *and* once per split decision) -- tens of
// thousands of IWRAM_CODE round trips per frame with a function call each way.
// They are tiny, so inlining them everywhere is a clear win. They are emitted
// into the caller's section and inherit the caller's instruction set, so the
// copies inside GBM_HOT_CODE functions are ARM and the copies inside the Thumb
// leaf decoders stay Thumb.
#define GBM_ALWAYS_INLINE static inline __attribute__((always_inline))

#define IWRAM_CODE __attribute__((section(".iwram"), long_call))

// Context for decoding a single frame
typedef struct {
    u32 state;
    const u8 *flag_ptr; // Current position in flag stream (must be 4-byte aligned reads)
    const u8 *flag_end;

    const u8 *palette_ptr; // Current position in palette stream
    const u8 *palette_end;

    const u8 *payload_ptr; // Current position in payload stream
    const u8 *payload_end;

    u16 *dst;       // Destination buffer (current frame)
    const u16 *ref; // Reference buffer (previous frame)

    int row_offset;   // Current macroblock row offset in bytes
    int block_offset; // Current block offset in bytes
} DecodeContext;

// Set XOR key based on GBM version (call once after loading GBM header)
// version: 0x06 for Gen1, 0x05 for Gen3
void gbm_set_version(u8 version);

// Initialize and decode a frame
// returns the offset of the next frame, or 0 on error
u32 gbm_decode_frame(const u8 *data, u32 offset, u16 *dst, const u16 *ref);

#endif // GBM_DECODER_H
