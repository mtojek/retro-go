#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RG_TIMER_INIT() int64_t _rgts_ = rg_system_timer(), _rgtl_ = rg_system_timer();
#define RG_TIMER_LAP(name)                                                                \
    RG_LOGX("Lap %s: %.2f   Total: %.2f\n", #name, (rg_system_timer() - _rgtl_) / 1000.f, \
            (rg_system_timer() - _rgts_) / 1000.f);                                       \
    _rgtl_ = rg_system_timer();

#define RG_MIN(a, b)            \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a < _b ? _a : _b;      \
    })
#define RG_MAX(a, b)            \
    ({                          \
        __typeof__(a) _a = (a); \
        __typeof__(b) _b = (b); \
        _a > _b ? _a : _b;      \
    })
#define RG_COUNT(array) (sizeof(array) / sizeof((array)[0]))

char *rg_strtolower(char *str);
char *rg_strtoupper(char *str);
size_t rg_strlcpy(char *dst, const char *src, size_t size);
const char *rg_dirname(const char *path);
const char *rg_basename(const char *path);
const char *rg_extension(const char *path);
const char *rg_relpath(const char *path);
const char *const_string(const char *str);
uint32_t rg_crc32(uint32_t crc, const uint8_t *buf, uint32_t len);
void *rg_alloc(size_t size, uint32_t caps);

#define MEM_ANY   (0)
#define MEM_SLOW  (1)
#define MEM_FAST  (2)
#define MEM_DMA   (4)
#define MEM_8BIT  (8)
#define MEM_32BIT (16)
#define MEM_EXEC  (32)

#define PTR_IN_SPIRAM(ptr) ((void *)(ptr) >= (void *)0x3F800000 && (void *)(ptr) < (void *)0x3FC00000)
