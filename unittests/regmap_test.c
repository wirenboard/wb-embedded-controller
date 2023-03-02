#include "regmap.h"
#include <stdio.h>
#include <errno.h>

#define REGMAP_SIZE                                             sizeof(struct regmap)

#define REGMAP_MEMBER(name, addr, rw, members)                  struct REGMAP_##name name;
#define REGMAP_REGION_SIZE(name, addr, rw, members)             (sizeof(struct REGMAP_##name)),
#define REGMAP_REGION_STRUCT_OFFSET(name, addr, rw, members)    (offsetof(struct regmap, name)),
#define REGMAP_REGION_RW(name, addr, rw, members)               rw,
#define REGMAP_REGION_ADDR(name, addr, rw, members)             addr,

struct regmap {
    REGMAP(REGMAP_MEMBER)
};


static const struct regions_info {
    uint8_t addr[REGMAP_REGION_COUNT];
    uint8_t size[REGMAP_REGION_COUNT];
    uint8_t struct_offset[REGMAP_REGION_COUNT];
    bool rw[REGMAP_REGION_COUNT];
} regions_info = {
    .addr = { REGMAP(REGMAP_REGION_ADDR) },
    .size = { REGMAP(REGMAP_REGION_SIZE) },
    .struct_offset = { REGMAP(REGMAP_REGION_STRUCT_OFFSET) },
    .rw = { REGMAP(REGMAP_REGION_RW) },
};


void i2c_slave_set_busy(void)
{

}

void i2c_slave_set_free(void)
{

}


int main(void)
{
    // Check regions overlap
    for (int r = 0; r < REGMAP_REGION_COUNT - 1; r++) {
        if (regions_info.addr[r] + regions_info.size[r] > regions_info.addr[r + 1]) {
            printf("ERROR: Region %d overlaps next region\n", r);
            return -EADDRINUSE;
        }
    }

    // Check last region
    if (regions_info.addr[REGMAP_REGION_COUNT - 1] + regions_info.size[REGMAP_REGION_COUNT - 1] > (0xFF + 1)) {
        printf("ERROR: Last region overlaps max address 0xFF\n");
        return -EADDRINUSE;
    }

    return 0;
}
