#pragma once
/* Compiler-only shim for the standalone upstream C code. No regex semantics. */
#if defined(_MSC_VER) && !defined(__clang__)
#include <stdint.h>
#define __attribute__(x)
#define __builtin_expect(x, expected) (x)
static __inline int server1_clz32(uint32_t x){int n=0;if(!x)return 32;while(!(x&0x80000000u)){++n;x<<=1;}return n;}
static __inline int server1_ctz32(uint32_t x){int n=0;if(!x)return 32;while(!(x&1u)){++n;x>>=1;}return n;}
static __inline int server1_clz64(uint64_t x){return (x>>32)?server1_clz32((uint32_t)(x>>32)):32+server1_clz32((uint32_t)x);}
static __inline int server1_ctz64(uint64_t x){return (uint32_t)x?server1_ctz32((uint32_t)x):32+server1_ctz32((uint32_t)(x>>32));}
#define __builtin_clz(x) server1_clz32(x)
#define __builtin_ctz(x) server1_ctz32(x)
#define __builtin_clzll(x) server1_clz64(x)
#define __builtin_ctzll(x) server1_ctz64(x)
#endif
