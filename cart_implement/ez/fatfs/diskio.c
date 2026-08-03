#include "ff.h"
#include "diskio.h"
#include "Ezcard_OP.h"
#include "RTC.h"

DSTATUS disk_status(BYTE pdrv)
{
    (void)pdrv;
    return RES_OK;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    (void)pdrv;
    return RES_OK;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    return (DRESULT)Read_SD_sectors((u32)sector, (u16)count, buff);
}

#if FF_FS_READONLY == 0

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;
    return (DRESULT)Write_SD_sectors((u32)sector, (u16)count, buff);
}

#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;
    (void)cmd;
    (void)buff;
    return RES_OK;
}

DWORD get_fattime(void)
{
    u8 datetime[7];

    rtc_enable();
    rtc_get(datetime);
    rtc_disenable();

    return ((DWORD)(UNBCD(datetime[0]) + 20) << 25 |
            (DWORD)UNBCD(datetime[1]) << 21 |
            (DWORD)UNBCD(datetime[2] & 0x3F) << 16 |
            (DWORD)UNBCD(datetime[4] & 0x3F) << 11 |
            (DWORD)UNBCD(datetime[5]) << 5 |
            (DWORD)UNBCD(datetime[6]) >> 1);
}

