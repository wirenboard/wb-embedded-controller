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

static irq_flags_t flags = 0;
static irq_flags_t mask = 0;

static inline void set_int_gpio_active(void)
{

}

static inline void set_int_gpio_inactive(void)
{

}

void irq_init(void)
{
    set_int_gpio_inactive();
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

    if (regmap_is_region_changed(REGMAP_REGION_IRQ_MSK)) {
        struct REGMAP_IRQ_MSK m;
        regmap_get_region_data(REGMAP_REGION_IRQ_MSK, &m, sizeof(m));
        irq_set_mask(m.irqs);

        regmap_clear_changed(REGMAP_REGION_IRQ_MSK);
    }

    if (regmap_is_region_changed(REGMAP_REGION_IRQ_CLEAR)) {
        struct REGMAP_IRQ_CLEAR c;
        regmap_get_region_data(REGMAP_REGION_IRQ_CLEAR, &c, sizeof(c));
        irq_clear_flags(c.irqs);

        regmap_clear_changed(REGMAP_REGION_IRQ_CLEAR);
    }
}
