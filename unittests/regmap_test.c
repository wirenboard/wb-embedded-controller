#include "regmap-int.h"
#include "regmap-ext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define REGMAP_ADDRESS_MASK                                     (REGMAP_TOTAL_REGS_COUNT - 1)

#define REGMAP_MEMBER(addr, name, rw, members)                  struct REGMAP_##name name;
#define REGMAP_REGION_SIZE(addr, name, rw, members)             (sizeof(struct REGMAP_##name)),
#define REGMAP_REGION_RW(addr, name, rw, members)               REGMAP_##rw,
#define REGMAP_REGION_ADDR(addr, name, rw, members)             addr,

enum regmap_rw {
    REGMAP_RO,
    REGMAP_RW
};

// Описания регионов регистров
struct regions_info {
    uint16_t addr[REGMAP_REGION_COUNT];             // Адрес первого регистра в регионе
    uint16_t size[REGMAP_REGION_COUNT];             // Размер региона в байтах
    enum regmap_rw rw[REGMAP_REGION_COUNT];         // Тип региона RO/RW
};

static const struct regions_info regions_info = {
    .addr = { REGMAP(REGMAP_REGION_ADDR) },
    .size = { REGMAP(REGMAP_REGION_SIZE) },
    .rw = { REGMAP(REGMAP_REGION_RW) },
};

// Возвращает размер региона в байтах
static inline uint16_t region_size(enum regmap_region r)
{
    return regions_info.size[r];
}

// Возвращает количество регистров в регионе
static inline uint16_t region_reg_count(enum regmap_region r)
{
    return (region_size(r) + 1) / sizeof(uint16_t);
}

// Возвращает адрес первого регистра в регионе
static inline uint16_t region_first_reg(enum regmap_region r)
{
    return regions_info.addr[r];
}

// Возвращает адрес последнего регистра в регионе
static inline uint16_t region_last_reg(enum regmap_region r)
{
    return region_first_reg(r) + region_reg_count(r) - 1;
}

// Возвращает true, если регион подходит для записи
static inline bool is_region_rw(enum regmap_region r)
{
    return (regions_info.rw[r] == REGMAP_RW);
}

int main(void)
{
    regmap_init();

    const enum regmap_region last_region = REGMAP_REGION_COUNT - 1;

    // Check regions overlap
    for (int r = 0; r < last_region - 1; r++) {
        if ((region_first_reg(r) + region_reg_count(r)) > region_first_reg(r + 1)) {
            printf("ERROR: Region %d overlaps next region\n", r);
            return -EADDRINUSE;
        }
    }

    // Check last region
    if ((region_first_reg(last_region) + region_reg_count(last_region)) >= REGMAP_TOTAL_REGS_COUNT) {
        printf("ERROR: Last region overlaps max address 0xFF\n");
        return -EADDRINUSE;
    }

    // Write data to regions
    uint16_t autoinc = 0;
    for (int r = 0; r < REGMAP_REGION_COUNT; r++)
    {
        size_t r_size = region_size(r);

        uint16_t * data = malloc(r_size);
        for (int i = 0; i < region_reg_count(r); i++) {
            data[i] = autoinc;
            autoinc++;
        }
        regmap_set_region_data(r, data, r_size);

        // Get data back and compare
        uint16_t * r_data = malloc(r_size);
        regmap_get_region_data(r, r_data, r_size);
        if (memcmp(data, r_data, r_size) != 0) {
            printf("ERROR: Data corrupted when set/get region\n");
            return -EBADMSG;
        }
    }

    // Readback data from regs throught all regmap with autoinc
    autoinc = 0;
    regmap_ext_prepare_operation(0);
    for (int reg = 0; reg < REGMAP_TOTAL_REGS_COUNT; reg++) {
        uint16_t val = regmap_ext_read_reg_autoinc();

        // Check that reg in someone region
        bool reg_in_region = false;
        for (int r = 0; r < REGMAP_REGION_COUNT; r++) {
            if ((reg >= region_first_reg(r)) && (reg <= region_last_reg(r))) {
                reg_in_region = true;
                break;
            }
        }

        if (reg_in_region) {
            if (val != autoinc) {
                printf("ERROR: Reading data from reg fails\n");
                return -EBADMSG;
            }
            autoinc++;
        } else {
            if (val != 0) {
                printf("ERROR: Non-region reg %d value is non-zero: %d\n", reg, val);
                return -EBADMSG;
            }
        }
    }
    regmap_ext_end_operation();

    // Check region RO/RW
    // Write all regs with data
    uint16_t autodec = UINT16_MAX;
    regmap_ext_prepare_operation(0);
    for (int reg = 0; reg < REGMAP_TOTAL_REGS_COUNT; reg++) {
        regmap_ext_write_reg_autoinc(autodec);
        autodec--;
    }
    regmap_ext_end_operation();

    // Check that only RW region written
    autodec = UINT16_MAX;
    autoinc = 0;
    regmap_ext_prepare_operation(0);
    for (int reg = 0; reg < REGMAP_TOTAL_REGS_COUNT; reg++) {
        uint16_t val = regmap_ext_read_reg_autoinc();
        // Find region
        int found_r = -1;
        for (int r = 0; r < REGMAP_REGION_COUNT; r++) {
            if ((reg >= region_first_reg(r)) && (reg <= region_last_reg(r))) {
                found_r = r;
                break;
            }
        }

        if (found_r < 0) {
            // No region reg, must be 0
            if (val != 0) {
                printf("ERROR: Non-region reg %d value is non-zero: %d\n", reg, val);
                return -EBADMSG;
            }
        } else {
            if (is_region_rw(found_r)) {
                if (val != autodec) {
                    printf("ERROR: RW region %d reg 0x%.4X not written: %d instead of %d\n", found_r, reg, val, autodec);
                    return -EBADMSG;
                }
            } else {
                if (val != autoinc) {
                    printf("ERROR: RO region reg %d corrupted after write\n", reg);
                    return -EBADMSG;
                }
            }
            autoinc++;
        }
        autodec--;
    }
    regmap_ext_end_operation();

    // Check is_changed flags
    for (int r = 0; r < REGMAP_REGION_COUNT; r++) {
        if (is_region_rw(r)) {
            if (!regmap_is_region_changed(r)) {
                printf("ERROR: No is_changed flag set for RW region");
                return -EBADMSG;
            } else {
                regmap_clear_changed(r);
                if (regmap_is_region_changed(r)) {
                    printf("ERROR: is_changed flag not cleared");
                    return -EBADMSG;
                }

            }
        } else {
            if (regmap_is_region_changed(r)) {
                printf("ERROR: is_changed flag is set for RO region");
                return -EBADMSG;
            }
        }
    }

    printf("All tests passed!\n\n");
    return 0;
}
