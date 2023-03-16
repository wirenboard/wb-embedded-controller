#include "regmap.h"
#include <string.h>
#include <stddef.h>

#define REGMAP_TOTAL_REGS_COUNT                                 (REGMAP_ADDRESS_MASK + 1)

#define REGMAP_MEMBER(addr, name, rw, members)                  struct REGMAP_##name name;
#define REGMAP_REGION_SIZE(addr, name, rw, members)             (sizeof(struct REGMAP_##name)),
#define REGMAP_REGION_RW(addr, name, rw, members)               REGMAP_##rw,
#define REGMAP_REGION_ADDR(addr, name, rw, members)             addr,

enum regmap_rw {
    REGMAP_RO,
    REGMAP_RW
};

struct regions_info {
    uint16_t addr[REGMAP_REGION_COUNT];
    uint16_t size[REGMAP_REGION_COUNT];
    uint16_t reg_count[REGMAP_REGION_COUNT];
    enum regmap_rw rw[REGMAP_REGION_COUNT];
};

struct regmap_ctx {
    uint16_t regs[REGMAP_TOTAL_REGS_COUNT];
    bool changed_flags[REGMAP_TOTAL_REGS_COUNT];
    uint16_t op_address;
    bool is_busy;
};

static const struct regions_info regions_info = {
    .addr = { REGMAP(REGMAP_REGION_ADDR) },
    .size = { REGMAP(REGMAP_REGION_SIZE) },
    .rw = { REGMAP(REGMAP_REGION_RW) },
};

static struct regmap_ctx regmap_ctx = {};

static inline uint16_t region_size(enum regmap_region r)
{
    return regions_info.size[r];
}

static inline uint16_t region_reg_count(enum regmap_region r)
{
    return (region_size(r) + 1) / sizeof(uint16_t);
}

static inline uint16_t region_first_reg(enum regmap_region r)
{
    return regions_info.addr[r];
}

static inline uint16_t region_last_reg(enum regmap_region r)
{
    return region_first_reg(r) + region_reg_count(r) - 1;
}

static inline bool is_region_rw(enum regmap_region r)
{
    return (regions_info.rw[r] == REGMAP_RW);
}

bool regmap_set_region_data(enum regmap_region r, const void * data, size_t size)
{
    if (regmap_ctx.is_busy) {
        return 0;
    }
    if (r >= REGMAP_REGION_COUNT) {
        return 0;
    }
    if (size != region_size(r)) {
        return 0;
    }
    if (regmap_is_region_changed(r)) {
        return 0;
    }

    uint16_t offset = region_first_reg(r);
    memcpy(&regmap_ctx.regs[offset], data, size);
    return 1;
}

void regmap_get_region_data(enum regmap_region r, void * data, size_t size)
{
    if (regmap_ctx.is_busy) {
        return;
    }
    if (r >= REGMAP_REGION_COUNT) {
        return;
    }
    if (size != region_size(r)) {
        return;
    }

    uint16_t offset = region_first_reg(r);
    memcpy(data, &regmap_ctx.regs[offset], size);
}

bool regmap_is_region_changed(enum regmap_region r)
{
    if (regmap_ctx.is_busy) {
        return 0;
    }
    if (r >= REGMAP_REGION_COUNT) {
        return 0;
    }

    uint16_t r_start = region_first_reg(r);
    uint16_t r_end = region_last_reg(r);

    for (uint16_t i = r_start; i <= r_end; i++) {
        if (regmap_ctx.changed_flags[i]) {
            return 1;
        }
    }
    return 0;
}


void regmap_clear_changed(enum regmap_region r)
{
    if (regmap_ctx.is_busy) {
        return;
    }
    if (r >= REGMAP_REGION_COUNT) {
        return;
    }

    uint16_t r_start = region_first_reg(r);
    uint16_t r_end = region_last_reg(r);

    for (uint16_t i = r_start; i <= r_end; i++) {
        regmap_ctx.changed_flags[i] = 0;
    }
}


void regmap_ext_prepare_operation(uint16_t start_addr)
{
    regmap_ctx.op_address = start_addr;
    regmap_ctx.is_busy = 1;
}

void regmap_ext_end_operation(void)
{
    regmap_ctx.is_busy = 0;
}

uint16_t regmap_ext_read_reg_autoinc(void)
{
    uint16_t r = regmap_ctx.regs[regmap_ctx.op_address];
    regmap_ctx.op_address++;
    regmap_ctx.op_address &= REGMAP_ADDRESS_MASK;
    return r;
}

void regmap_ext_write_reg_autoinc(uint16_t val)
{
    regmap_ctx.regs[regmap_ctx.op_address] = val;
    regmap_ctx.changed_flags[regmap_ctx.op_address] = 1;
    regmap_ctx.op_address++;
    regmap_ctx.op_address &= REGMAP_ADDRESS_MASK;
}
