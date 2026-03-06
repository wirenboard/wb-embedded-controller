#include "utest_regmap.h"
#include <string.h>

#define MAX_REGION_SIZE 256

// Internal state for mock regmap
static struct {
    uint8_t data[REGMAP_REGION_COUNT][MAX_REGION_SIZE];
    size_t size[REGMAP_REGION_COUNT];
    bool changed[REGMAP_REGION_COUNT];
} regmap_state;

static bool regmap_init_called = false;

void utest_regmap_reset(void)
{
    memset(&regmap_state, 0, sizeof(regmap_state));
    regmap_init_called = false;
}

void utest_regmap_mark_region_changed(enum regmap_region r)
{
    if (r < REGMAP_REGION_COUNT) {
        regmap_state.changed[r] = true;
    }
}

bool utest_regmap_get_region_data(enum regmap_region r, void * data, size_t size)
{
    if (r >= REGMAP_REGION_COUNT || data == NULL || size == 0) {
        return false;
    }

    if (size > regmap_state.size[r]) {
        return false;
    }

    memcpy(data, regmap_state.data[r], size);
    return true;
}

// Mock implementation of regmap API
void regmap_init(void)
{
    utest_regmap_reset();
    regmap_init_called = true;
}

bool utest_regmap_was_init_called(void)
{
    return regmap_init_called;
}

bool regmap_set_region_data(enum regmap_region r, const void * data, size_t size)
{
    if (r >= REGMAP_REGION_COUNT || data == NULL || size == 0 || size > MAX_REGION_SIZE) {
        return false;
    }

    memcpy(regmap_state.data[r], data, size);
    regmap_state.size[r] = size;
    return true;
}

bool regmap_get_data_if_region_changed(enum regmap_region r, void * data, size_t size)
{
    if (r >= REGMAP_REGION_COUNT || size > MAX_REGION_SIZE) {
        return false;
    }

    if (!regmap_state.changed[r]) {
        return false;
    }

    if (data != NULL && size > 0) {
        if (size > regmap_state.size[r]) {
            return false;
        }
        memcpy(data, regmap_state.data[r], size);
    }

    regmap_state.changed[r] = false;
    return true;
}
