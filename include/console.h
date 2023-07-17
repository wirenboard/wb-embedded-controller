#pragma once
#include <stddef.h>

void console_print_prefix(void);
void console_print_w_prefix(const char str[]);

void console_print(const char str[]);
void console_print_dec_pad(int val, unsigned int padding, char padding_char);
void console_print_dec(int val);
void console_print_fixed_point(int val, unsigned int fractional_digits);

void console_print_spinner(unsigned int counter);
void console_print_time_now(void);
