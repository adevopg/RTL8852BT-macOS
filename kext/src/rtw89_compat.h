/* SPDX-License-Identifier: BSD-3-Clause
 *
 * rtw89_compat.h - capa minima de compatibilidad Linux -> IOKit/XNU
 *
 * Objetivo: que las definiciones de registros y estructuras del driver Linux
 * (reference/rtw89-linux/reg.h, fw.h, ...) compilen dentro de un kext sin
 * tocarlas. Solo tipos y macros; nada de logica.
 *
 * FASE 1: lo justo para reg.h y la cabecera del firmware.
 * Fases siguientes: readl/writel, udelay, workqueue, skb, dma, mutex...
 */
#ifndef RTW89_COMPAT_H
#define RTW89_COMPAT_H

#include <IOKit/IOTypes.h>
#include <libkern/OSByteOrder.h>
#include <sys/types.h>

/* ---- tipos enteros de Linux ------------------------------------------ */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

/* tipos "endian-tagged" de Linux; en x86 le32 == u32 en memoria */
typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint64_t __le64;
typedef uint16_t __be16;
typedef uint32_t __be32;

#define __packed __attribute__((packed))
#define __aligned(x) __attribute__((aligned(x)))
#define __maybe_unused __attribute__((unused))
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* ---- bits.h / bitfield.h ------------------------------------------------ */
#ifndef BIT
#define BIT(n)        (1UL << (n))
#endif
#define BIT_ULL(n)    (1ULL << (n))
#define BITS_PER_LONG 64
#define GENMASK(h, l) \
	(((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define GENMASK_ULL(h, l) \
	(((~0ULL) - (1ULL << (l)) + 1) & (~0ULL >> (64 - 1 - (h))))

/* __ffs sobre la mascara para FIELD_GET/FIELD_PREP */
#define __bf_shf(x) (__builtin_ffsll(x) - 1)
#define FIELD_GET(_mask, _reg)  ((u32)(((_reg) & (_mask)) >> __bf_shf(_mask)))
#define FIELD_PREP(_mask, _val) (((u32)(_val) << __bf_shf(_mask)) & (_mask))

static inline u32 u32_get_bits(u32 v, u32 mask) { return FIELD_GET(mask, v); }
static inline u8  u8_get_bits(u8 v, u8 mask)    { return (u8)FIELD_GET(mask, v); }
static inline u32 le32_to_cpu(__le32 v)          { return OSSwapLittleToHostInt32(v); }
static inline u16 le16_to_cpu(__le16 v)          { return OSSwapLittleToHostInt16(v); }
static inline __le32 cpu_to_le32(u32 v)          { return OSSwapHostToLittleInt32(v); }
static inline __le16 cpu_to_le16(u16 v)          { return OSSwapHostToLittleInt16(v); }
static inline u32 le32_get_bits(__le32 v, u32 mask) { return FIELD_GET(mask, le32_to_cpu(v)); }
static inline u16 le16_get_bits(__le16 v, u16 mask) { return (u16)FIELD_GET(mask, le16_to_cpu(v)); }

/* ---- errno de Linux (valores negativos, como en el driver) -------------- */
#define EINVAL  22
#define ENOENT   2
#define EFAULT  14
#define ENOMEM  12
#define EBUSY   16
#define ETIMEDOUT 60
#define EIO      5

/* ---- misc --------------------------------------------------------------- */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))

/* Constantes propias del driver que necesitamos en fase 1 */
#define RTW89_MFW_SIG   0xFF
#define RTW89_R32_DEAD  0xdeadbeef
#define RTW89_R32_EA    0xeaeaeaea

#endif /* RTW89_COMPAT_H */
