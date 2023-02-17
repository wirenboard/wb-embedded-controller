#include "regmap.h"
#include <string.h>
#include <stddef.h>

#define REGMAP_MEMBER(type, name, rw)                   type name;
#define REGMAP_REGION_SIZE(type, name, rw)              (sizeof(type)),
#define REGMAP_REGION_START_OFFSET(type, name, rw)      (offsetof(struct regmap, name)),
#define REGMAP_REGION_END_OFFSET(type, name, rw)        (offsetof(struct regmap, name) + sizeof(type) - 1),
#define REGMAP_REGION_RW(type, name, rw)                rw,

struct regmap {
    REGMAP(REGMAP_MEMBER)
};

static const struct regions_info {
    size_t size[REGMAP_REGION_COUNT];
    uint8_t start_offset[REGMAP_REGION_COUNT];
    uint8_t end_offset[REGMAP_REGION_COUNT];
    bool rw[REGMAP_REGION_COUNT];
} regions_info = {
    .size = { REGMAP(REGMAP_REGION_SIZE) },
    .start_offset = { REGMAP(REGMAP_REGION_START_OFFSET) },
    .end_offset = { REGMAP(REGMAP_REGION_END_OFFSET) },
    .rw = { REGMAP(REGMAP_REGION_RW) },
};

union regmap_union {
    struct regmap regs;
    uint8_t data[sizeof(struct regmap)];
};

union regmap_union regmap;
union regmap_union regmap_snapshot;

static bool region_is_changed[REGMAP_REGION_COUNT];

bool is_write_completed = 0;

static inline size_t region_size(enum regmap_region r)
{
    return regions_info.size[r];
}

static inline size_t region_first_reg(enum regmap_region r)
{
    return regions_info.start_offset[r];
}

static inline size_t region_last_reg(enum regmap_region r)
{
    return regions_info.end_offset[r];
}

static inline enum regmap_region get_region_by_addr(uint8_t addr)
{
    enum regmap_region r = 0;

    while (addr > region_last_reg(r)) {
        r++;
    }

    return r;
}

static int is_region_rw(enum regmap_region r)
{
    return regions_info.end_offset[r];
}

bool regmap_set_region_data(enum regmap_region r, const void * data, size_t size)
{
    if (r >= REGMAP_REGION_COUNT) {
        return 0;
    }
    if (size != region_size(r)) {
        return 0;
    }

    uint8_t offset = region_first_reg(r);
    memcpy(&regmap.data[offset], data, size);
    return 1;
}

void regmap_make_snapshot(void)
{
    memcpy(regmap_snapshot.data, regmap.data, sizeof(struct regmap));
}

uint8_t regmap_get_snapshot_reg(uint8_t addr)
{
    if (addr > regmap_get_max_reg()) {
        return 0;
    }

    return regmap_snapshot.data[addr];
}

int regmap_set_snapshot_reg(uint8_t addr, uint8_t value)
{
    if (addr > regmap_get_max_reg()) {
        return -100;//EFAULT;
    }

    enum regmap_region r = get_region_by_addr(addr);

    if (!is_region_rw(r)) {
        return -10;//EROFS;
    }

    if (regmap_snapshot.data[addr] != value) {
        regmap_snapshot.data[addr] = value;

        region_is_changed[r] = 1;
        return 1;
    }

    return 0;
}

bool regmap_is_snapshot_region_changed(enum regmap_region r)
{
    if (r >= REGMAP_REGION_COUNT) {
        return 0;
    }

    bool ret = region_is_changed[r];
    region_is_changed[r] = 0;
    return ret;
}

void regmap_get_snapshop_region_data(enum regmap_region r, void * data, size_t size)
{
    if (r >= REGMAP_REGION_COUNT) {
        return;
    }

    if (size != region_size(r)) {
        return;
    }

    uint8_t offset = region_first_reg(r);
    memcpy(data, &regmap_snapshot.data[offset], size);
}

uint8_t regmap_get_max_reg(void)
{
    return sizeof(struct regmap) - 1;
}
