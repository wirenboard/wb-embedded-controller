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
    printf("Testing regions for overlap...\n");
    for (int r = 0; r < last_region - 1; r++) {
        if ((region_first_reg(r) + region_reg_count(r)) > region_first_reg(r + 1)) {
            printf("ERROR: Region %d overlaps next region\n", r);
            return -EADDRINUSE;
        }
    }

    // Check last region
    printf("Testing last region bounds...\n");
    if ((region_first_reg(last_region) + region_reg_count(last_region)) >= REGMAP_TOTAL_REGS_COUNT) {
        printf("ERROR: Last region overlaps max address 0xFF\n");
        return -EADDRINUSE;
    }

    // Write data to regions
    printf("Writing test data to all regions...\n");
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
    }

    // Readback data from regs throught all regmap with autoinc
    printf("Reading back data with autoinc through entire regmap...\n");
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
    printf("Testing RO/RW region protection...\n");
    // Write all regs with data
    uint16_t autodec = UINT16_MAX;
    regmap_ext_prepare_operation(0);
    for (int reg = 0; reg < REGMAP_TOTAL_REGS_COUNT; reg++) {
        regmap_ext_write_reg_autoinc(autodec);
        autodec--;
    }
    regmap_ext_end_operation();

    // Check that only RW region written
    printf("Verifying that only RW regions were modified...\n");
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
    printf("Testing is_changed flags...\n");
    for (int r = 0; r < REGMAP_REGION_COUNT; r++) {
        if (is_region_rw(r)) {
            if (!regmap_get_data_if_region_changed(r, NULL, 0)) {
                printf("ERROR: No is_changed flag set for RW region");
                return -EBADMSG;
            } else {
                if (regmap_get_data_if_region_changed(r, NULL, 0)) {
                    printf("ERROR: is_changed flag not cleared");
                    return -EBADMSG;
                }

            }
        } else {
            if (regmap_get_data_if_region_changed(r, NULL, 0)) {
                printf("ERROR: is_changed flag is set for RO region");
                return -EBADMSG;
            }
        }
    }

    // TEST: Check regmap_set_region_data with invalid size
    printf("Testing regmap_set_region_data with invalid size...\n");
    for (int r = 0; r < REGMAP_REGION_COUNT; r++) {
        uint16_t test_data[10] = {0};
        size_t wrong_size = region_size(r) + 1;  // Wrong size
        if (regmap_set_region_data(r, test_data, wrong_size)) {
            printf("ERROR: regmap_set_region_data accepted wrong size for region %d\n", r);
            return -EINVAL;
        }
    }
    for (int r = 0; r < REGMAP_REGION_COUNT; r++) {
        uint16_t test_data[10] = {0};
        size_t wrong_size = region_size(r) - 1;  // Wrong size
        if (regmap_set_region_data(r, test_data, wrong_size)) {
            printf("ERROR: regmap_set_region_data accepted wrong size for region %d\n", r);
            return -EINVAL;
        }
    }
    for (int r = 0; r < REGMAP_REGION_COUNT; r++) {
        uint16_t test_data[10] = {0};
        size_t wrong_size = 0;  // Wrong size
        if (regmap_set_region_data(r, test_data, wrong_size)) {
            printf("ERROR: regmap_set_region_data accepted wrong size for region %d\n", r);
            return -EINVAL;
        }
    }

    // TEST: Check regmap_set_region_data with invalid region index
    printf("Testing regmap_set_region_data with invalid region index...\n");
    uint16_t test_data[10] = {0};
    if (regmap_set_region_data(REGMAP_REGION_COUNT, test_data, sizeof(test_data))) {
        printf("ERROR: regmap_set_region_data accepted invalid region index\n");
        return -EINVAL;
    }
    if (regmap_set_region_data(REGMAP_REGION_COUNT + 1, test_data, sizeof(test_data))) {
        printf("ERROR: regmap_set_region_data accepted invalid region index\n");
        return -EINVAL;
    }

    // TEST: Check regmap_get_data_if_region_changed with invalid size
    printf("Testing regmap_get_data_if_region_changed with invalid size...\n");
    for (int r = 0; r < REGMAP_REGION_COUNT; r++) {
        uint16_t test_buf[10] = {0};
        size_t wrong_size = region_size(r) + 1;  // Wrong size
        if (regmap_get_data_if_region_changed(r, test_buf, wrong_size)) {
            printf("ERROR: regmap_get_data_if_region_changed accepted wrong size for region %d\n", r);
            return -EINVAL;
        }
    }

    // TEST: Check regmap_get_data_if_region_changed with invalid region index
    printf("Testing regmap_get_data_if_region_changed with invalid region index...\n");
    if (regmap_get_data_if_region_changed(REGMAP_REGION_COUNT, test_data, sizeof(test_data))) {
        printf("ERROR: regmap_get_data_if_region_changed accepted invalid region index\n");
        return -EINVAL;
    }
    if (regmap_get_data_if_region_changed(REGMAP_REGION_COUNT + 1, test_data, sizeof(test_data))) {
        printf("ERROR: regmap_get_data_if_region_changed accepted invalid region index\n");
        return -EINVAL;
    }

    // TEST: Check that regmap_set_region_data fails when region is changed externally
    printf("Testing regmap_set_region_data when region changed externally...\n");
    for (int r = 0; r < REGMAP_REGION_COUNT; r++) {
        if (!is_region_rw(r)) {
            continue;
        }

        size_t r_size = region_size(r);
        uint16_t * data_to_write = malloc(r_size);
        memset(data_to_write, 0xAA, r_size);

        // First, write some data to the region externally
        regmap_ext_prepare_operation(region_first_reg(r));
        for (int i = 0; i < region_reg_count(r); i++) {
            regmap_ext_write_reg_autoinc(0x1234);
        }
        regmap_ext_end_operation();

        // Now try to set region data from inside - should fail because region was changed externally
        if (regmap_set_region_data(r, data_to_write, r_size)) {
            printf("ERROR: regmap_set_region_data succeeded when region was changed externally (region %d)\n", r);
            free(data_to_write);
            return -EBADMSG;
        }

        // Clear the changed flag
        regmap_get_data_if_region_changed(r, NULL, 0);

        // Now it should succeed
        if (!regmap_set_region_data(r, data_to_write, r_size)) {
            printf("ERROR: regmap_set_region_data failed when region was not changed (region %d)\n", r);
            free(data_to_write);
            return -EBADMSG;
        }

        free(data_to_write);
        break;  // Test only one RW region
    }

    // TEST: Check that regmap_set_region_data fails when regmap is busy
    printf("Testing regmap_set_region_data when regmap is busy...\n");
    for (int r = 0; r < REGMAP_REGION_COUNT; r++) {
        size_t r_size = region_size(r);
        uint16_t * data_to_write = malloc(r_size);
        memset(data_to_write, 0xBB, r_size);

        // Make regmap busy
        regmap_ext_prepare_operation(0);

        // Try to set region data - should fail
        if (regmap_set_region_data(r, data_to_write, r_size)) {
            printf("ERROR: regmap_set_region_data succeeded when regmap was busy (region %d)\n", r);
            regmap_ext_end_operation();
            free(data_to_write);
            return -EBUSY;
        }

        regmap_ext_end_operation();
        free(data_to_write);
        break;  // Test only one region
    }

    // TEST: Check that regmap_get_data_if_region_changed fails when regmap is busy
    printf("Testing regmap_get_data_if_region_changed when regmap is busy...\n");
    for (int r = 0; r < REGMAP_REGION_COUNT; r++) {
        if (!is_region_rw(r)) {
            continue;
        }

        size_t r_size = region_size(r);
        uint16_t * data_to_read = malloc(r_size);

        // Write data externally to set changed flag
        regmap_ext_prepare_operation(region_first_reg(r));
        for (int i = 0; i < region_reg_count(r); i++) {
            regmap_ext_write_reg_autoinc(0x5678);
        }
        // Keep regmap busy (don't call regmap_ext_end_operation yet)

        // Try to get data - should fail because regmap is busy
        if (regmap_get_data_if_region_changed(r, data_to_read, r_size)) {
            printf("ERROR: regmap_get_data_if_region_changed succeeded when regmap was busy (region %d)\n", r);
            regmap_ext_end_operation();
            free(data_to_read);
            return -EBUSY;
        }

        regmap_ext_end_operation();

        // Now it should succeed
        if (!regmap_get_data_if_region_changed(r, data_to_read, r_size)) {
            printf("ERROR: regmap_get_data_if_region_changed failed after regmap became free (region %d)\n", r);
            free(data_to_read);
            return -EBADMSG;
        }

        free(data_to_read);
        break;  // Test only one RW region
    }

    // TEST: Check address wraparound for read
    printf("Testing address wraparound for read...\n");
    regmap_ext_prepare_operation(REGMAP_TOTAL_REGS_COUNT - 2);
    regmap_ext_read_reg_autoinc();  // REGMAP_TOTAL_REGS_COUNT - 2
    regmap_ext_read_reg_autoinc();  // REGMAP_TOTAL_REGS_COUNT - 1
    uint16_t val_after_wrap = regmap_ext_read_reg_autoinc();  // Should wrap to 0
    regmap_ext_end_operation();
    // Check that we read from address 0 (which should be in first region)
    uint16_t expected_val = 0;
    for (int r = 0; r < REGMAP_REGION_COUNT; r++) {
        if ((0 >= region_first_reg(r)) && (0 <= region_last_reg(r))) {
            // Address 0 is in this region, get expected value
            size_t r_size = region_size(r);
            uint16_t * region_data = malloc(r_size);
            memset(region_data, 0, r_size);
            regmap_set_region_data(r, region_data, r_size);
            expected_val = region_data[0];
            free(region_data);
            break;
        }
    }
    if (val_after_wrap != expected_val) {
        printf("ERROR: Address wraparound for read didn't work correctly\n");
        return -EBADMSG;
    }

    // TEST: Check address wraparound for write
    printf("Testing address wraparound for write...\n");
    regmap_ext_prepare_operation(REGMAP_TOTAL_REGS_COUNT - 2);
    regmap_ext_write_reg_autoinc(0xAAAA);  // REGMAP_TOTAL_REGS_COUNT - 2
    regmap_ext_write_reg_autoinc(0xBBBB);  // REGMAP_TOTAL_REGS_COUNT - 1
    regmap_ext_write_reg_autoinc(0xCCCC);  // Should wrap to 0
    regmap_ext_end_operation();
    // Verify that address 0 was written (if it's in RW region)
    regmap_ext_prepare_operation(0);
    uint16_t val_at_zero = regmap_ext_read_reg_autoinc();
    regmap_ext_end_operation();
    // Check if address 0 is in RW region
    bool addr_zero_is_rw = false;
    for (int r = 0; r < REGMAP_REGION_COUNT; r++) {
        if ((0 >= region_first_reg(r)) && (0 <= region_last_reg(r))) {
            if (is_region_rw(r)) {
                addr_zero_is_rw = true;
            }
            break;
        }
    }
    if (addr_zero_is_rw && (val_at_zero != 0xCCCC)) {
        printf("ERROR: Address wraparound for write didn't work correctly: got 0x%04X instead of 0xCCCC\n", val_at_zero);
        return -EBADMSG;
    }

    // TEST: Full-duplex operation (simultaneous read and write with different addresses)
    printf("Testing full-duplex operation...\n");

    // Find first RW region to test on
    int test_region = -1;
    for (int r = 0; r < REGMAP_REGION_COUNT; r++) {
        if (is_region_rw(r)) {
            test_region = r;
            break;
        }
    }

    if (test_region < 0) {
        printf("WARNING: No RW regions found, skipping full-duplex test\n");
    } else {
        uint16_t start_addr = region_first_reg(test_region);
        uint16_t reg_count = region_reg_count(test_region);

        // First, set known data through external write
        regmap_ext_prepare_operation(start_addr);
        for (int i = 0; i < reg_count; i++) {
            regmap_ext_write_reg_autoinc(0x1000 + i);
        }
        regmap_ext_end_operation();

        // Clear changed flag
        regmap_get_data_if_region_changed(test_region, NULL, 0);

        // Now perform full-duplex: read from region, write to the same region
        // This simulates SPI full-duplex mode
        regmap_ext_prepare_operation(start_addr);
        uint16_t * read_vals = malloc(reg_count * sizeof(uint16_t));
        int test_count = (reg_count < 5) ? reg_count : 5;  // Test up to 5 registers
        for (int i = 0; i < test_count; i++) {
            read_vals[i] = regmap_ext_read_reg_autoinc();  // Read increases r_address
            regmap_ext_write_reg_autoinc(0xFD00 + i);      // Write increases w_address
        }
        regmap_ext_end_operation();

        // Verify that reads returned correct values (old data before write)
        for (int i = 0; i < test_count; i++) {
            uint16_t expected = 0x1000 + i;
            if (read_vals[i] != expected) {
                printf("ERROR: Full-duplex read returned wrong value at offset %d: 0x%04X instead of 0x%04X\n",
                       i, read_vals[i], expected);
                free(read_vals);
                return -EBADMSG;
            }
        }

        // Verify that writes were applied
        regmap_ext_prepare_operation(start_addr);
        for (int i = 0; i < test_count; i++) {
            uint16_t current_val = regmap_ext_read_reg_autoinc();
            uint16_t expected = 0xFD00 + i;
            if (current_val != expected) {
                printf("ERROR: Full-duplex write didn't apply at offset %d: 0x%04X instead of 0x%04X\n",
                       i, current_val, expected);
                free(read_vals);
                return -EBADMSG;
            }
        }
        regmap_ext_end_operation();
        free(read_vals);
    }

    printf("All tests passed!\n\n");
    return 0;
}
