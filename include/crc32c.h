/* crc32c.c -- compute CRC-32C using the Intel crc32 instruction
 * Copyright (C) 2013 Mark Adler
 * Version 1.1  1 Aug 2013  Mark Adler
 */

#ifndef CRC32C_INCLUDE
#define CRC32C_INCLUDE

#include <stdint.h>
#include <string.h>
uint32_t crc32c_hw(uint32_t crc, const void *buf, size_t len);
uint32_t crc32c_sw(uint32_t crc, const void *buf, size_t len);
uint32_t crc32c(uint32_t crc, const void *buf, size_t len);

#endif
