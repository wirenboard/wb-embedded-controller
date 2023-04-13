#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <assert.h>
#include "regmap-int.h"
#include "atomic.h"

/**
 * regmap - определяет набор регистров устройства и предоставляет доступ к ним снаружи и изнутри
 *
 * Позволяет объявлять регионы регистров, для каждого региона можно задать:
 *  - поля структуры данных региона
 *  - начальный адрес
 *  - тип: RO/RW
 *
 * Набор регистров описан в файле regmap-structs.h с помощью макроса REGMAP.
 * После разворачивания макроса становятся доступны имена регионов через enum
 * и данные через структуры
 *
 * Для доступа из прошивки методы объявлены в файле regmap-int.h и позволяют:
 *  - записать данные в регион, передав указатель на структуру
 *  - проверить, что регион был изменен снаружи
 *  - получить данные из региона
 *
 * Для доступа снаружи (по i2c/spi) используется файл regmap-ext.h,
 * в котором объявлены функции установки начального адреса и чтения/записи регистров
 * с автоинкрементом адреса
 */

#define REGMAP_ADDRESS_MASK                                     (REGMAP_TOTAL_REGS_COUNT - 1)

#define REGMAP_MEMBER(addr, name, rw, members)                  struct REGMAP_##name name;
#define REGMAP_REGION_SIZE(addr, name, rw, members)             (sizeof(struct REGMAP_##name)),
#define REGMAP_REGION_RW(addr, name, rw, members)               REGMAP_##rw,
#define REGMAP_REGION_ADDR(addr, name, rw, members)             addr,

// Check that regs count is power of two
static_assert((REGMAP_TOTAL_REGS_COUNT & REGMAP_ADDRESS_MASK) == 0, "Registers count must be power of two");

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

// Состояние regmap
// Если не объединять в структуру, код работает немного быстрее
static uint16_t regs[REGMAP_TOTAL_REGS_COUNT] = {};                 // Массив для хранения данных
static uint32_t written_flags[REGMAP_TOTAL_REGS_COUNT / 32] = {};   // Битовые флаги записи каждого регистра
static uint32_t rw_flags[REGMAP_TOTAL_REGS_COUNT / 32] = {};        // Признак того, что в регистр можно записывать данные снаружи
static uint16_t op_address = 0;                                     // Адрес текущей операции
static bool is_busy = 0;                                            // Флаг занятости regmap


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

static inline uint32_t addr_to_bit_mask(uint16_t addr)
{
    return 1 << (addr & 0x01F);
}

static inline uint32_t addr_to_bit_addr(uint16_t addr)
{
    return addr >> 5;
}

static inline void set_bit_flag(uint16_t addr, uint32_t bit_array[])
{
    bit_array[addr_to_bit_addr(addr)] |= addr_to_bit_mask(addr);
}

static inline void clear_bit_flag(uint16_t addr, uint32_t bit_array[])
{
    bit_array[addr_to_bit_addr(addr)] &= ~addr_to_bit_mask(addr);
}

static inline bool get_bit_flag(uint16_t addr, const uint32_t bit_array[])
{
    return bit_array[addr_to_bit_addr(addr)] & addr_to_bit_mask(addr);
}

void regmap_init(void)
{
    // Fill RW bits for each register
    for (unsigned r = 0; r < REGMAP_REGION_COUNT; r++) {
        if (is_region_rw(r)) {
            for (unsigned i = region_first_reg(r); i <= region_last_reg(r); i++) {
                set_bit_flag(i, rw_flags);
            }
        }
    }
}

// Записывает данные в регион
// Проверяет размер данных на совпадаение с размером региона
// Проверяет, не изменился ли регион снаружи
// Проверяет занятость regmap и атомарно записывает данные
bool regmap_set_region_data(enum regmap_region r, const void * data, size_t size)
{
    if (r >= REGMAP_REGION_COUNT) {
        return 0;
    }
    if (size != region_size(r)) {
        return 0;
    }
    if (regmap_is_region_changed(r)) {
        return 0;
    }

    uint16_t offset = region_first_reg(r);
    ATOMIC {
        if (is_busy) {
            return 0;
        }
        memcpy(&regs[offset], data, size);
    }
    return 1;
}

// Получает данные из региона
// Проверяет размер данных на совпадаение с размером региона
// Проверяет занятость regmap и атомарно переписывает данные во внешнюю структуру
void regmap_get_region_data(enum regmap_region r, void * data, size_t size)
{
    if (r >= REGMAP_REGION_COUNT) {
        return;
    }
    if (size != region_size(r)) {
        return;
    }

    uint16_t offset = region_first_reg(r);
    ATOMIC {
        if (is_busy) {
            return;
        }
        memcpy(data, &regs[offset], size);
    }
}

// Проверяет, изменился ли регион снаружи
bool regmap_is_region_changed(enum regmap_region r)
{
    if (is_busy) {
        return 0;
    }
    if (r >= REGMAP_REGION_COUNT) {
        return 0;
    }

    uint16_t r_start = region_first_reg(r);
    uint16_t r_end = region_last_reg(r);

    for (uint16_t i = r_start; i <= r_end; i++) {
        if (get_bit_flag(i, written_flags)) {
            return 1;
        }
    }
    return 0;
}

// Атомарно сбрасывает флаги изменения региона
void regmap_clear_changed(enum regmap_region r)
{
    if (r >= REGMAP_REGION_COUNT) {
        return;
    }

    uint16_t r_start = region_first_reg(r);
    uint16_t r_end = region_last_reg(r);

    ATOMIC {
        if (is_busy) {
            return;
        }
        for (uint16_t i = r_start; i <= r_end; i++) {
            clear_bit_flag(i, written_flags);
        }
    }
}

// Подготовка внешней операции с regmap
// Устанавливает начальный адрес и флаг занятости
// Выполняется в контексте прерывания
void regmap_ext_prepare_operation(uint16_t start_addr)
{
    op_address = start_addr;
    is_busy = 1;
}

// Конец внешней операции, снимает флаг занятости
// Выполняется в контексте прерывания
void regmap_ext_end_operation(void)
{
    is_busy = 0;
}

// Возвращает значение регистра и увеличивает адрес
// Выполняется в контексте прерывания
uint16_t regmap_ext_read_reg_autoinc(void)
{
    uint16_t r = regs[op_address];
    op_address++;
    op_address &= REGMAP_ADDRESS_MASK;
    return r;
}

// Записывает значение в регистр, устанавливает адрес и флаг изменения регистра
// Выполняется в контексте прерывания
void regmap_ext_write_reg_autoinc(uint16_t val)
{
    // Для ускорения не используем функции, т.к. rw_bit_addr и rw_bit_mask нужны в двух местах
    uint16_t rw_bit_addr = addr_to_bit_addr(op_address);
    uint32_t rw_bit_mask = addr_to_bit_mask(op_address);

    if (rw_flags[rw_bit_addr] & rw_bit_mask) {
        regs[op_address] = val;
        written_flags[rw_bit_addr] |= rw_bit_mask;
    }
    op_address++;
    op_address &= REGMAP_ADDRESS_MASK;
}
