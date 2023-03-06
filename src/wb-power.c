#include "wb-power.h"
#include "systick.h"
#include "gpio.h"
#include "config.h"

enum wb_power_state {
    WB_OFF,
    WB_ON,
    WB_OFF_WAIT_RESET,
    WB_OFF_WAIT_SLEEP
};

struct wb_power_ctx {
    enum wb_power_state state;
    systime_t timestamp;
    uint16_t delay;
};

static struct wb_power_ctx wb_power_ctx;

static inline void a40_5v_off(void)
{
    GPIO_SET(A40_POWER_OFF_PORT, A40_POWER_OFF_PIN);
}

static inline void a40_5v_on(void)
{
    GPIO_RESET(A40_POWER_OFF_PORT, A40_POWER_OFF_PIN);
}

static inline void wbec_goto_standby(void)
{

}

void wb_power_init(void)
{
    a40_5v_off();
    wb_power_ctx.state = WB_OFF;
    GPIO_SET_PUSHPULL(A40_POWER_OFF_PORT, A40_POWER_OFF_PIN);
    GPIO_SET_OUTPUT(A40_POWER_OFF_PORT, A40_POWER_OFF_PIN);
}

void wb_power_on(void)
{
    a40_5v_on();
    wb_power_ctx.state = WB_ON;
}

void wb_power_reset(void)
{
    a40_5v_off();
    wb_power_ctx.state = WB_ON;
    wb_power_ctx.timestamp = systick_get_system_time();
}

void wb_power_off_and_sleep(uint16_t off_delay)
{
    a40_5v_off();
    if (off_delay == 0) {
        wbec_goto_standby();
    }
    wb_power_ctx.state = WB_ON;
    wb_power_ctx.timestamp = systick_get_system_time();
    wb_power_ctx.delay = off_delay;
}

void wb_power_do_periodic_work(void)
{
    switch (wb_power_ctx.state) {
    case WB_OFF_WAIT_RESET:
        if (systick_get_time_since_timestamp(wb_power_ctx.timestamp) > WBEC_POWER_RESET_TIME_MS) {
            a40_5v_on();
            wb_power_ctx.state = WB_ON;
        }
        break;

    case WB_OFF_WAIT_SLEEP:
        if (systick_get_time_since_timestamp(wb_power_ctx.timestamp) > wb_power_ctx.delay) {
            wbec_goto_standby();
        }
        break;

    default:
        break;
    }
}
