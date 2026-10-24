/* arpa/inet.h -- for building dkr_assets_tool on Windows (setup.py).
 *
 * The tool reads the ROM's big-endian fields through htons/ntohl and friends
 * (dkr_assets_tool_src/fileTypes/types.hpp); Windows has them only in its
 * socket library. Byte swaps are all they are on a little-endian host. */
#pragma once
#include <stdint.h>

static inline uint16_t htons(uint16_t v) { return __builtin_bswap16(v); }
static inline uint16_t ntohs(uint16_t v) { return __builtin_bswap16(v); }
static inline uint32_t htonl(uint32_t v) { return __builtin_bswap32(v); }
static inline uint32_t ntohl(uint32_t v) { return __builtin_bswap32(v); }
