#include "irq-subsystem.h"
#include <assert.h>
#include "regmap-int.h"
#include "config.h"
#include "gpio.h"

/**
 * IRQ subsystem представляет интерфейс для работы с флагами прерываний в regmap
 *
 * Есть 3 типа регистров:
 *  - флаги
 *  - маска
 *  - сброс
 *
 * Все они имеют одинаковый формат - в одном бите хранится один флаг прерывания
 * Активные биты - "1"
 * Если есть флаги прерываний и в соответствующих битах маски "1" - устанавливается активный уровень на INT GPIO
 * Чтобы сбросить флаг прерывания - нужно записать "1" в соответствующий бит регистра сброса
 *
 * В линуксе для этого есть удобный интерфейс regmap_irq_chip
 */

static_assert((sizeof(irq_flags_t) * 8) >= IRQ_COUNT, "IRQ flags not fitted to `irq_flags` type");

static const gpio_pin_t int_gpio = { EC_GPIO_INT };

static irq_flags_t flags = 0;
static irq_flags_t mask = 0;

static inline void set_int_gpio_active(void)
{
    #ifdef EC_GPIO_INT_ACTIVE_HIGH
        GPIO_S_SET(int_gpio);
    #else
        GPIO_S_RESET(int_gpio);
    #endif
}

static inline void set_int_gpio_inactive(void)
{
    #ifdef EC_GPIO_INT_ACTIVE_HIGH
        GPIO_S_RESET(int_gpio);
    #else
        GPIO_S_SET(int_gpio);
    #endif
}

void irq_init(void)
{
    set_int_gpio_inactive();
    GPIO_S_SET_PUSHPULL(int_gpio);
    GPIO_S_SET_OUTPUT(int_gpio);
}

irq_flags_t irq_get_flags(void)
{
    return flags;
}

void irq_set_flag(enum irq_flag f)
{
    flags |= (1 << f);
}

void irq_set_mask(irq_flags_t m)
{
    mask = m;
}

void irq_clear_flags(irq_flags_t f)
{
    flags &= ~f;
}

void irq_do_periodic_work(void)
{
    if (flags & mask) {
        set_int_gpio_active();
    } else {
        set_int_gpio_inactive();
    }

    // IRQ to regmap
    struct REGMAP_IRQ_FLAGS f;
    f.irqs = flags;
    regmap_set_region_data(REGMAP_REGION_IRQ_FLAGS, &f, sizeof(f));

    struct REGMAP_IRQ_MSK m;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_IRQ_MSK, &m, sizeof(m))) {
        irq_set_mask(m.irqs);
    }

    struct REGMAP_IRQ_CLEAR c;
    if (regmap_get_data_if_region_changed(REGMAP_REGION_IRQ_CLEAR, &c, sizeof(c))) {
        irq_clear_flags(c.irqs);
    }
}
