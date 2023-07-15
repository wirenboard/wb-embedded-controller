#include "wbmcu_system.h"
#include "config.h"
#include "usart_tx.h"
#include "rtc.h"

void console_print(const char str[])
{
    usart_tx_str_blocking(str);
}

void console_print_dec_pad(unsigned int val, unsigned int padding, char padding_char)
{
    char buf[10];
    char *p = buf + sizeof(buf) - 1;
    *p = '\0';
    do {
        *--p = '0' + val % 10;
        val /= 10;
    } while (val != 0);
    while (p > buf + sizeof(buf) - 1 - padding) {
        *--p = padding_char;
    }
    console_print(p);
}

void console_print_time_now(void)
{
    struct rtc_time rtc_time;
    rtc_get_datetime(&rtc_time);
    console_print("20");
    console_print_dec_pad(BCD_TO_BIN(rtc_time.years), 2, '0');
    console_print("-");
    console_print_dec_pad(BCD_TO_BIN(rtc_time.months), 2, '0');
    console_print("-");
    console_print_dec_pad(BCD_TO_BIN(rtc_time.days), 2, '0');
    console_print(" ");
    console_print_dec_pad(BCD_TO_BIN(rtc_time.hours), 2, '0');
    console_print(":");
    console_print_dec_pad(BCD_TO_BIN(rtc_time.minutes), 2, '0');
    console_print(":");
    console_print_dec_pad(BCD_TO_BIN(rtc_time.seconds), 2, '0');
}

void console_print_prefix(void) {
    console_print(WBEC_DEBUG_MSG_PREFIX);
}


void console_print_w_prefix(const char str[]) {
    console_print_prefix();
    console_print(str);
}

void console_print_spinner(unsigned int counter)
{
    switch (counter % 4) {
        case 0:
            console_print("|");
            break;
        case 1:
            console_print("/");
            break;
        case 2:
            console_print("-");
            break;
        case 3:
            console_print("\\");
            break;
    }
}
