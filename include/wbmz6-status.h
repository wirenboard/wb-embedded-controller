#pragma once
#include <stdint.h>
#include <stdbool.h>

struct wbmz6_status {
    bool is_charging;
    bool is_dead;
    bool is_inserted;
    uint16_t voltage_now_mv;
    uint16_t charging_current_ma;
    uint16_t discharging_current_ma;
    uint16_t capacity_percent;
    uint16_t temperature;
};

struct wbmz6_params {
    uint16_t full_design_capacity_mah;
    uint16_t voltage_min_mv;
    uint16_t voltage_max_mv;
    uint16_t charge_current_ma;
};
