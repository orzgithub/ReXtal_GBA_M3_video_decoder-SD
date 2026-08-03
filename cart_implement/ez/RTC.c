#include <gba_base.h>

#include "RTC.h"

static void send_clock_byte(u16 bit)
{
    *RTC_DATA = bit | 4;
    *RTC_DATA = bit | 4;
    *RTC_DATA = bit | 4;
    *RTC_DATA = bit | 5;
}

void rtc_cmd(int v)
{
    int l;

    v <<= 1;
    for (l = 7; l >= 0; --l) {
        send_clock_byte((u16)((v >> l) & 0x2));
    }
}

void rtc_data(int v)
{
    int l;

    v <<= 1;
    for (l = 0; l < 8; ++l) {
        send_clock_byte((u16)((v >> l) & 0x2));
    }
}

int rtc_read(void)
{
    int value = 0;
    int l;

    for (l = 0; l < 8; ++l) {
        int j;

        for (j = 0; j < 5; ++j) {
            *RTC_DATA = 4;
        }
        *RTC_DATA = 5;

        value |= (*RTC_DATA & 2) << l;
    }

    return value >> 1;
}

int rtc_get(u8 *data)
{
    int i;

    *RTC_DATA = 1;
    *RTC_RW = 7;
    *RTC_DATA = 1;
    *RTC_DATA = 5;
    rtc_cmd(RTC_CMD_READ(2));

    *RTC_RW = 5;
    for (i = 0; i < 4; ++i) {
        data[i] = (u8)rtc_read();
    }

    *RTC_RW = 5;
    for (i = 4; i < 7; ++i) {
        data[i] = (u8)rtc_read();
    }

    return 0;
}

int rtc_gettime(u8 *data)
{
    int i;

    *RTC_DATA = 1;
    *RTC_RW = 7;
    *RTC_DATA = 1;
    *RTC_DATA = 5;
    rtc_cmd(RTC_CMD_READ(3));

    *RTC_RW = 5;
    for (i = 0; i < 3; ++i) {
        data[i] = (u8)rtc_read();
    }

    return 0;
}

void rtc_set(u8 *data)
{
    u8 newdata[7];
    int i;

    for (i = 0; i < 7; ++i) {
        newdata[i] = _BCD(data[i]);
    }

    *RTC_ENABLE = 1;
    *RTC_DATA = 1;
    *RTC_DATA = 5;
    *RTC_RW = 7;
    rtc_cmd(RTC_CMD_WRITE(2));

    for (i = 0; i < 4; ++i) {
        rtc_data(newdata[i]);
    }
    for (i = 4; i < 7; ++i) {
        rtc_data(newdata[i]);
    }
}
