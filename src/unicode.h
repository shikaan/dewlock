#pragma once
#include <stddef.h>
#include <stdint.h>

// Technically UTF-8 supports up to 6 byte codepoints, but Unicode itself
// doesn't really bother with more than 4.
#define UTF8_MAX_SIZE 4

#define UTF8_INVALID 0x80

int utf8_last_size(const char *str);
uint32_t utf8_decode(const char **str);
size_t utf8_encode(char *str, uint32_t ch);
int utf8_size(const char *str);
size_t utf8_chsize(uint32_t ch);
