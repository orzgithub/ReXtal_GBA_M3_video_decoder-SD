#include <gba_base.h>
#include <gba_dma.h>

#include "Ezcard_OP.h"

extern void SoftReset_now(void);

__attribute__((always_inline))
static inline void open_bus(void)
{
    *(vu16 *)EZ_SD_CONFIG_LO = EZ_BUS_OPEN;
    *(vu16 *)EZ_CARD_BOOT_LO = EZ_BUS_CLOSE;
    *(vu16 *)EZ_CARD_BOOT_HI = EZ_BUS_OPEN;
    *(vu16 *)EZ_CARD_BOOT_END = EZ_BUS_CLOSE;
}

__attribute__((always_inline))
static inline void close_bus(void)
{
    *(vu16 *)EZ_SD_CONFIG_LO = EZ_BUS_OPEN;
    *(vu16 *)EZ_CARD_BOOT_LO = EZ_BUS_CLOSE;
    *(vu16 *)EZ_CARD_BOOT_HI = EZ_BUS_OPEN;
    *(vu16 *)EZ_CARD_BOOT_END = EZ_BUS_CLOSE;
    *(vu16 *)EZ_SD_CONFIG_HI = EZ_BUS_CLOSE;
}

static void IWRAM_CODE write_page_reg(vu16 *reg, u16 page)
{
    open_bus();
    *reg = page;
    close_bus();
}

static void IWRAM_CODE write_sd_command(u32 sector, u16 blocks)
{
    open_bus();
    *(vu16 *)EZ_SD_LBA_LO = sector & 0xffff;
    *(vu16 *)EZ_SD_LBA_HI = (sector >> 16) & 0xffff;
    *(vu16 *)EZ_SD_BLOCK_CTRL = blocks;
    close_bus();
}

static u32 IWRAM_CODE wait_sd_ready(void)
{
    u32 spins = 0;

    while (SD_Response() == EZ_READY_MARK) {
        if (++spins > EZ_RESPONSE_TIMEOUT) {
            return 1;
        }
    }

    return 0;
}

__attribute__((always_inline))
static inline u16 next_block_count(u16 remaining)
{
    return remaining > EZ_MAX_BLOCKS ? EZ_MAX_BLOCKS : remaining;
}

static void IWRAM_CODE delay(u32 loops)
{
    volatile u32 i;

    for (i = loops; i != 0; --i) {
        /* Busy wait used by the EZ cart timing paths. */
    }
}

void IWRAM_CODE SetSDControl(u16 control)
{
    open_bus();
    *(vu16 *)EZ_SD_CTRL = control;
    close_bus();
}

u32 IWRAM_CODE Read_SD_sectors(u32 address, u16 count, u8 *SDbuffer)
{
    u32 retries = EZ_READ_RETRY_COUNT;
    u16 i;

    Switch_OS_Mode();
    SD_Enable();

    for (i = 0; i < count; i += EZ_MAX_BLOCKS) {
        u16 blocks = next_block_count(count - i);
        u32 result;

    retry_read:
        write_sd_command(address + i, blocks);
        SD_Read_state();
        result = wait_sd_ready();
        SD_Enable();

        if (result == 1) {
            if (retries != 0) {
                retries--;
                delay(5000);
                goto retry_read;
            }
        }

        dmaCopy((void *)EZ_SD_BUFFER_ADDR, SDbuffer + i * EZ_SECTOR_SIZE, blocks * EZ_SECTOR_SIZE);
    }

    SD_Disable();
    Switch_PSRAM_Mode();

    return 0;
}

u32 IWRAM_CODE Write_SD_sectors(u32 address, u16 count, const u8 *SDbuffer)
{
    u16 i;

    Switch_OS_Mode();
    SD_Read_state();

    for (i = 0; i < count; i += EZ_MAX_BLOCKS) {
        u16 blocks = next_block_count(count - i);

        dmaCopy(SDbuffer + i * EZ_SECTOR_SIZE, (void *)EZ_SD_BUFFER_ADDR, blocks * EZ_SECTOR_SIZE);
        write_sd_command(address + i, (u16)(0x8000 + blocks));

        if (wait_sd_ready() == 1) {
            return 1;
        }
    }

    delay(3000);
    SD_Disable();
    Switch_PSRAM_Mode();

    return 0;
}

__attribute__((always_inline))
void inline SetRompage(u16 page)
{
    write_page_reg((vu16 *)EZ_ROM_PAGE, page);
}

__attribute__((always_inline))
void inline SetPSRampage(u16 page)
{
    write_page_reg((vu16 *)EZ_PSRAM_PAGE, page);
}

__attribute__((always_inline))
void inline SetbufferControl(u16 control)
{
    write_page_reg((vu16 *)EZ_BUFFER_CTRL, control);
}

void IWRAM_CODE Send_FATbuffer(u32 *buffer, u32 mode)
{
    SetbufferControl(1);
    dmaCopy(buffer, (void *)EZ_SD_BUFFER_ADDR, EZ_FAT_BUFFER_SIZE);

    if (mode == 2) {
        SetbufferControl(0);
        return;
    }

    SetbufferControl(3);

    if (mode == 1) {
        SetbufferControl(0);
        return;
    }

    while (SD_Response() == 0x0000) {
    }

    while (SD_Response() == 0x0001) {
    }

    SetbufferControl(0);
}

__attribute__((always_inline))
void inline Switch_PSRAM_Mode(void)
{
    SetRompage(EZ_ROM_MODE_PAGE);
}

__attribute__((always_inline))
void inline Switch_OS_Mode(void)
{
    SetRompage(EZ_OS_MODE_PAGE);
}

void IWRAM_CODE pram_write(u32 addr, const void *data, u32 size)
{
    u32 phys_offset = addr - EZ_PSRAM_BASE;
    const u8 *src = data;

    Switch_OS_Mode();

    while (size > 0) {
        u32 page_start = phys_offset & EZ_PSRAM_PAGE_MASK;
        u32 page_off = phys_offset - page_start;
        u32 space = EZ_PSRAM_PAGE_SIZE - page_off;
        u32 chunk = size < space ? size : space;
        u16 page = (u16)((phys_offset >> 23) * EZ_PSRAM_PAGE_STEP);

        SetPSRampage(page);
        dmaCopy((void *)src, (void *)(EZ_PSRAM_WINDOW + page_off), chunk);

        phys_offset += chunk;
        src += chunk;
        size -= chunk;
    }

    SetPSRampage(0);
    Switch_PSRAM_Mode();
}

__attribute__((noinline, noreturn))
void IWRAM_CODE return_menu(void)
{
    Switch_OS_Mode();
    SoftReset_now();
    while (1) {
    }
}
