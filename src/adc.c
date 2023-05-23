#include "adc.h"
#include "wbmcu_system.h"
#include "gpio.h"
#include "config.h"
#include <stdbool.h>
#include "systick.h"

/**
 * Модуль занимается опросом каналов АЦП и фильтрацией данных
 * АЦП работает непрерывно и через DMA складывает данные в память
 *
 * Функция adc_do_periodic_work вызывается из основного цикла и выполняет
 * инициализацию lowpass фильтра и фильтрацию значений
 *
 * Функция adc_get_ch_mv позволяет получить значение в mV
 *
 * Конфигурация каналов задается макросом:
 *
 * #define ADC_CHANNELS_DESC(macro) \
 *          Channel name          ADC CH  PORT    PIN     RC      K                 \
 *    macro(ADC_IN1,              10,     GPIOB,  2,      50,     1               ) \
 *    macro(ADC_IN2,              11,     GPIOB,  10,     50,     1.0 / 11.0      ) \
 *
 * Channel name - имя канала, превращается в enum ADC_CHANNEL_<name>
 * ADC CH - номер канала АЦП МК
 * PORT, PIN - GPIO
 * RC - постоянная RC (можно поменять в рантайме)
 * K - коэффициент пересчёта (делитель, в примере выше 1к + 10к). Если делителя нет, указать 1
 *
 * Если используются внутренние каналы, то вместо PORT и PIN указать ADC_NO_GPIO_PIN
*/

#define ADC_FILTRATION_PERIOD_MS        5
#define ADC_NO_GPIO_PIN                 0
#define ADC_RESOLUTION_BIT              12

#define ADC_CH_FULL_SCALE_MV(k)         (ADC_VREF_EXT_MV * (k)) // max measure voltage

struct adc_ctx {
    bool initialized;
    systime_t timestamp;
    uint16_t raw_values[ADC_CHANNEL_COUNT];
    fix16_t  lowpass_values[ADC_CHANNEL_COUNT];
    fix16_t  lowpass_factors[ADC_CHANNEL_COUNT];
};

struct adc_ctx adc_ctx = {};

#define ADC_CHANNEL_DATA(alias, ch_num, port, pin, rc_factor, k, offset_mv) \
    { ADC_CHSELR_CHSEL##ch_num, port, pin, rc_factor, ADC_CH_FULL_SCALE_MV(k), offset_mv },

/* This buffer contain order number in dma read sequence adc channels for each record in struct adc_channel adc_ch. It is set in runtime in adc_init() */
static uint8_t chan_index_in_dma_buff[ADC_CHANNEL_COUNT] = {};
#define ADC_CHANNEL_INDEX(ch)                                   (chan_index_in_dma_buff[ch])

struct adc_config_record {
    uint32_t channel;
    GPIO_TypeDef *port;
    uint8_t pin;
    uint32_t rc_factor;
    uint32_t full_scale_mv;
    int16_t offset_mv;
};

struct adc_config_record adc_cfg[ADC_CHANNEL_COUNT] = {
    ADC_CHANNELS_DESC(ADC_CHANNEL_DATA)
};

static inline fix16_t calculate_rc_factor(uint32_t tau_ms)
{
    return fix16_div(
        fix16_one,
        fix16_add(
            fix16_one,
            fix16_div(
                fix16_from_int(tau_ms),
                fix16_from_int(ADC_FILTRATION_PERIOD_MS)
            )
        )
    );
}

void adc_init(void)
{
    #ifdef ADC_VREF_EXT_EN_GPIO
        const gpio_pin_t vref_en_gpio = { ADC_VREF_EXT_EN_GPIO };
        GPIO_S_SET_PUSHPULL(vref_en_gpio);
        GPIO_S_SET_OUTPUT(vref_en_gpio);
        GPIO_S_SET(vref_en_gpio);

        // Нужно подождать, пока VREF стабилизируется после включения
        // Текущая RC - 220R * 10u = 2.2 ms
        // Экспериментально установленное время выхода на рабочий режим - 5 мс
        systime_t t_vref = systick_get_system_time_ms();
        while (systick_get_time_since_timestamp(t_vref) <= 5) {};
    #endif

    // init DMA
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;

    DMAMUX1_Channel0->CCR = 5;                                  // ADC source

    DMA1_Channel1->CCR |= DMA_CCR_PL;                           // very high priority
    DMA1_Channel1->CCR |= DMA_CCR_MSIZE_0 | DMA_CCR_PSIZE_0;    // 16 bit -> 16 bit
    DMA1_Channel1->CCR |= DMA_CCR_MINC;                         // memory increment
    DMA1_Channel1->CCR |= DMA_CCR_CIRC;                         // circular mode

    DMA1_Channel1->CPAR = (uint32_t)&(ADC1->DR);                // peripheral address
    DMA1_Channel1->CMAR = (uint32_t)adc_ctx.raw_values;         // memory address

    DMA1_Channel1->CNDTR = ADC_CHANNEL_COUNT;

    DMA1_Channel1->CCR |= DMA_CCR_EN;

    // Init ADC
    RCC->APBENR2 |= RCC_APBENR2_ADCEN;

    // RM0454: 14.3.2
    // The ADC has a specific internal voltage regulator which must be enabled and stable before using the ADC
    // The software must wait for the ADC voltage regulator startup time
    // (tADCVREG_SETUP) before launching a calibration or enabling the ADC. This delay must be
    // managed by software (for details on tADCVREG_SETUP, refer to the device datasheet).
    // tADCVREG_SETUP = 20 us
    ADC1->CR |= ADC_CR_ADVREGEN;
    // Wait 1-2 ms - two lsb of system time
    systime_t t = systick_get_system_time_ms();
    while (systick_get_time_since_timestamp(t) < 2) {};

    ADC1->CR |= ADC_CR_ADCAL;
    while (ADC1->CR & ADC_CR_ADCAL) {};

    ADC1->CFGR1 |= ADC_CFGR1_DMACFG | ADC_CFGR1_DMAEN;          // DMA enable, DMA circular mode
    ADC1->CFGR1 |= ADC_CFGR1_CONT;                              // Continuous conversion mode

    ADC1->SMPR |= ADC_SMPR_SMP1;                                // 160.5 ADC clock cycles
    ADC1->CR |= ADC_CR_ADEN;

    for (uint8_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
        // Configure ADC GPIOs
        if (adc_cfg[i].port != ADC_NO_GPIO_PIN) {
            GPIO_SET_ANALOG(adc_cfg[i].port, adc_cfg[i].pin);
        }
        // Enable all ADC channels
        ADC1->CHSELR |= adc_cfg[i].channel;

        // Prepare mapping ADC channel to dma buffer for quick data access using just channel
        // chan_index_in_dma_buff имеет байт на каждый enum ADC_CHANNEL_XXX и по сути это индекс, по которому значение на данном канале лежит в буффере дма
        // для каждой записи в таблице adc_ch мы инкрементируем индексы всех каналов которые имеют номер больше чем номер текущего канала в данной записи
        for (uint8_t j = 0; j < ADC_CHANNEL_COUNT; j++) {
            if (adc_cfg[i].channel < adc_cfg[j].channel) {
                chan_index_in_dma_buff[j]++;
            }
        }
    }

    for (uint8_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
        // Init time ms parameters for software RC filter
        adc_set_lowpass_rc(i, adc_cfg[i].rc_factor);
    }

    while ((ADC1->ISR & ADC_ISR_ADRDY) == 0) {};
    ADC1->CR |= ADC_CR_ADSTART;
}

void adc_set_lowpass_rc(enum adc_channel channel, uint16_t rc_ms)
{
    uint8_t ch_index_in_dma_buff = ADC_CHANNEL_INDEX(channel);

    adc_ctx.lowpass_factors[ch_index_in_dma_buff] = calculate_rc_factor(rc_ms);
}

// Возвращает единицы АЦП после lowpass фильтра
// [0, 4095] в целой части
// и результат усреднения в дробной части
fix16_t adc_get_ch_adc_raw(enum adc_channel channel)
{
    uint8_t ch_index_in_dma_buff = ADC_CHANNEL_INDEX(channel);

    return adc_ctx.lowpass_values[ch_index_in_dma_buff];
}

// Возвращает милливольты с учётом делителя (K) и оффсета после lowpass фильтра
uint32_t adc_get_ch_mv(enum adc_channel channel)
{
    uint8_t ch_index_in_dma_buff = ADC_CHANNEL_INDEX(channel);
    uint32_t adc_raw = fix16_to_int(adc_ctx.lowpass_values[ch_index_in_dma_buff]);

    uint32_t mv = (adc_raw * adc_cfg[channel].full_scale_mv) >> ADC_RESOLUTION_BIT;
    return mv + adc_cfg[channel].offset_mv;
}

void adc_do_periodic_work(void)
{
    if (!adc_ctx.initialized) {
        for (uint8_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
            adc_ctx.lowpass_values[i] = fix16_from_int(adc_ctx.raw_values[i]);
        }
        adc_ctx.initialized = 1;
        adc_ctx.timestamp = systick_get_system_time_ms();
        return;
    }

    if (systick_get_time_since_timestamp(adc_ctx.timestamp) < ADC_FILTRATION_PERIOD_MS) {
        return;
    }
    adc_ctx.timestamp += ADC_FILTRATION_PERIOD_MS;

    for (uint8_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
        adc_ctx.lowpass_values[i] += fix16_mul(
            adc_ctx.lowpass_factors[i],
            fix16_sub(
                fix16_from_int(adc_ctx.raw_values[i]),
                adc_ctx.lowpass_values[i]
            )
        );
    }
}
