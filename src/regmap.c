#include "regmap.h"
#include <string.h>
#include <stddef.h>
#include "i2c-slave.h"

#define REGMAP_SIZE                                             sizeof(struct regmap)

#define REGMAP_MEMBER(name, addr, rw, members)                  struct REGMAP_##name name;
#define REGMAP_REGION_SIZE(name, addr, rw, members)             (sizeof(struct REGMAP_##name)),
#define REGMAP_REGION_STRUCT_OFFSET(name, addr, rw, members)    (offsetof(struct regmap, name)),
#define REGMAP_REGION_RW(name, addr, rw, members)               rw,
#define REGMAP_REGION_ADDR(name, addr, rw, members)             addr,

struct regmap {
    REGMAP(REGMAP_MEMBER)
};

struct regions_info {
    uint8_t addr[REGMAP_REGION_COUNT];
    uint8_t size[REGMAP_REGION_COUNT];
    uint8_t struct_offset[REGMAP_REGION_COUNT];
    enum regmap_rw rw[REGMAP_REGION_COUNT];
};

struct regmap_ctx {
    uint8_t regmap[REGMAP_SIZE];
    uint8_t regmap_snapshot[REGMAP_SIZE];
    bool region_is_changed[REGMAP_REGION_COUNT];
    int busy_semaphore;
    bool write_complited;
};

static const struct regions_info regions_info = {
    .addr = { REGMAP(REGMAP_REGION_ADDR) },
    .size = { REGMAP(REGMAP_REGION_SIZE) },
    .struct_offset = { REGMAP(REGMAP_REGION_STRUCT_OFFSET) },
    .rw = { REGMAP(REGMAP_REGION_RW) },
};

static struct regmap_ctx regmap_ctx = {};

static inline uint8_t region_size(enum regmap_region r)
{
    return regions_info.size[r];
}

static inline uint8_t region_struct_offset(enum regmap_region r)
{
    return regions_info.struct_offset[r];
}

static inline uint8_t region_first_reg(enum regmap_region r)
{
    return regions_info.addr[r];
}

static inline uint8_t region_last_reg(enum regmap_region r)
{
    return region_first_reg(r) + region_size(r) - 1;
}

static inline int get_region_by_addr(uint8_t addr)
{
    enum regmap_region r = 0;

    // Find region
    while (addr > region_last_reg(r)) {
        r++;
    }

    // Check region fist reg
    if (addr < region_first_reg(r)) {
        return -EADDRNOTAVAIL;
    }

    return r;
}

static inline bool is_region_rw(enum regmap_region r)
{
    return (regions_info.rw[r] == REGMAP_RW);
}

static uint8_t reg_addr_to_struct_offset(enum regmap_region r, uint8_t reg_addr)
{
    uint8_t region_offset = region_struct_offset(r);
    uint8_t reg_offset = reg_addr - region_first_reg(r);
    uint8_t offset = region_offset + reg_offset;

    return offset;
}

bool regmap_set_region_data(enum regmap_region r, const void * data, size_t size)
{
    if (r >= REGMAP_REGION_COUNT) {
        return 0;
    }
    if (size != region_size(r)) {
        return 0;
    }

    uint8_t offset = region_struct_offset(r);
    memcpy(&regmap_ctx.regmap[offset], data, size);
    return 1;
}

void regmap_make_snapshot(void)
{
    memcpy(regmap_ctx.regmap_snapshot, regmap_ctx.regmap, sizeof(struct regmap));
}

uint8_t regmap_get_snapshot_reg(uint8_t addr)
{
    int r = get_region_by_addr(addr);
    if (r < 0) {
        return 0;
    }

    uint8_t offset = reg_addr_to_struct_offset(r, addr);

    return regmap_ctx.regmap_snapshot[offset];
}

int regmap_snapshot_set_reg(uint8_t addr, uint8_t value)
{
    int r = get_region_by_addr(addr);
    if (r < 0) {
        return -EADDRNOTAVAIL;
    }

    if (!is_region_rw(r)) {
        return -EROFS;
    }

    uint8_t offset = reg_addr_to_struct_offset(r, addr);

    if (regmap_ctx.regmap_snapshot[offset] != value) {
        regmap_ctx.regmap_snapshot[offset] = value;

        if (!regmap_ctx.region_is_changed[r]) {
            regmap_ctx.busy_semaphore++;
            regmap_ctx.region_is_changed[r] = 1;
        }
        return 1;
    }

    return 0;
}

bool regmap_snapshot_is_region_changed(enum regmap_region r)
{
    if (r >= REGMAP_REGION_COUNT) {
        return 0;
    }
    if (!regmap_ctx.write_complited) {
        return 0;
    }

    return regmap_ctx.region_is_changed[r];
}

void regmap_snapshot_clear_changed(enum regmap_region r)
{
    if (r >= REGMAP_REGION_COUNT) {
        return;
    }

    if (regmap_ctx.region_is_changed[r]) {
        regmap_ctx.region_is_changed[r] = 0;
        regmap_ctx.busy_semaphore--;
        if (regmap_ctx.busy_semaphore == 0) {
            i2c_slave_set_free();
            regmap_ctx.write_complited = 0;
        }
    }
}

void regmap_snapshot_set_write_complited(void)
{
    if (regmap_ctx.busy_semaphore > 0) {
        regmap_ctx.write_complited = 1;
    }
}

void regmap_get_snapshop_region_data(enum regmap_region r, void * data, size_t size)
{
    if (r >= REGMAP_REGION_COUNT) {
        return;
    }

    if (size != region_size(r)) {
        return;
    }

    uint8_t offset = region_struct_offset(r);
    memcpy(data, &regmap_ctx.regmap_snapshot[offset], size);
}
