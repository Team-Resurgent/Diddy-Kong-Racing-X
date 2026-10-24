/* dkr_compat.h -- forced in front of every dkr_assets_tool compile on Windows
 * (setup.py). The tool's gzip code zeroes buffers with bzero, which the
 * MinGW C library does not have. */
#pragma once
#include <string.h>

#define bzero(p, n) memset((p), 0, (n))
