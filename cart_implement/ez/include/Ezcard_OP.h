#ifndef EZ_CARD_OP_H
#define EZ_CARD_OP_H

#include <gba_base.h>
#include "../fatfs/ff.h"

#define EZ_SD_BUFFER_ADDR 0x9e00000

#define EZ_BUS_OPEN 0xd200
#define EZ_BUS_CLOSE 0x1500
#define EZ_SD_CONFIG_LO 0x9fe0000
#define EZ_SD_CONFIG_HI 0x9fc0000
#define EZ_SD_CTRL 0x9400000
#define EZ_SD_LBA_LO 0x9600000
#define EZ_SD_LBA_HI 0x9620000
#define EZ_SD_BLOCK_CTRL 0x9640000
#define EZ_ROM_PAGE 0x9880000
#define EZ_PSRAM_PAGE 0x9860000
#define EZ_BUFFER_CTRL 0x9420000
#define EZ_CARD_BOOT_LO 0x8000000
#define EZ_CARD_BOOT_HI 0x8020000
#define EZ_CARD_BOOT_END 0x8040000
#define EZ_SECTOR_SIZE 512
#define EZ_MAX_BLOCKS 4
#define EZ_READY_MARK 0xEEE1
#define EZ_RESPONSE_TIMEOUT 0x100000
#define EZ_READ_RETRY_COUNT 2
#define EZ_FAT_BUFFER_SIZE 0x400
#define EZ_PSRAM_BASE 0x08000000
#define EZ_PSRAM_WINDOW 0x08800000
#define EZ_PSRAM_PAGE_SIZE 0x800000
#define EZ_PSRAM_PAGE_MASK 0xff800000
#define EZ_PSRAM_PAGE_STEP 0x1000
#define EZ_ROM_MODE_PAGE 0x200
#define EZ_OS_MODE_PAGE 0x8000

#define SD_Enable() (SetSDControl(1))
#define SD_Disable() (SetSDControl(0))
#define SD_Read_state() (SetSDControl(3))
#define SD_Response() (*(vu16 *)EZ_SD_BUFFER_ADDR)

void IWRAM_CODE SetSDControl(u16 control);
u32 IWRAM_CODE Read_SD_sectors(u32 address, u16 count, u8 *SDbuffer);
u32 IWRAM_CODE Write_SD_sectors(u32 address, u16 count, const u8 *SDbuffer);
void IWRAM_CODE SetRompage(u16 page);
void IWRAM_CODE SetPSRampage(u16 page);
void IWRAM_CODE SetbufferControl(u16 control);
void IWRAM_CODE Send_FATbuffer(u32 *buffer, u32 mode);
void IWRAM_CODE Switch_OS_Mode(void);
void IWRAM_CODE Switch_PSRAM_Mode(void);

#endif
