#pragma once
#include <stddef.h>
#include <stdint.h>
// Stub for TurboPFor vp4 library — legacy container decompression only.
// New-format recordings never use P4NZENC, so these are never called at runtime.
static inline size_t p4nzdec128v16(unsigned char* in, size_t n, unsigned short* out) { (void)in; (void)n; (void)out; return 0; }
static inline size_t p4nzenc128v16(unsigned char* in, size_t n, unsigned short* out) { (void)in; (void)n; (void)out; return 0; }
