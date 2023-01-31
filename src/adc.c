#include "adc.h"
#include <stm32g0xx.h>
#include "gpio.h"
#include "config.h"

#define ADC_POLL_PERIOD_US              5000
#define ADC_NO_GPIO_PIN                 0

uint16_t adc_raw_values[ADC_CHANNEL_COUNT];
fix16_t  adc_lowpass_values[ADC_CHANNEL_COUNT] = {0};
fix16_t  adc_lowpass_factors[ADC_CHANNEL_COUNT];

#define ADC_CHANNEL_DATA(alias, ch_num, port, pin, rc_factor)   {ADC_CHSELR_CHSEL##ch_num, port, pin, rc_factor}
/* This buffer contain order number in dma read sequence adc channels for each record in struct adc_channel adc_ch. It is set in runtime in adc_init() */
static uint8_t chan_index_in_dma_buff[ADC_CHANNEL_COUNT] = {};
#define ADC_CHANNEL_INDEX(ch)                                   (chan_index_in_dma_buff[ch])

struct adc_config_record {
    uint32_t channel;
    GPIO_TypeDef *port;
    uint8_t pin;
    uint32_t rc_factor;
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
                fix16_from_int(ADC_POLL_PERIOD_US / 1000)
            )
        )
    );
}

void adc_init(void)
{
    // init DMA
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;

    DMA1_Channel1->CCR |= DMA_CCR_PL;                           // very high priority
    DMA1_Channel1->CCR |= DMA_CCR_MSIZE_0 | DMA_CCR_PSIZE_0;    // 16 bit -> 16 bit
    DMA1_Channel1->CCR |= DMA_CCR_MINC;                         // memory increment
    DMA1_Channel1->CCR |= DMA_CCR_CIRC;                         // circular mode
    DMA1_Channel1->CCR |= DMA_CCR_TCIE;                         // transfer complete IE

    DMA1_Channel1->CPAR = (uint32_t)&(ADC1->DR);                // peripheral address
    DMA1_Channel1->CMAR = (uint32_t)adc_raw_values;             // memory address

    DMA1_Channel1->CNDTR = ADC_CHANNEL_COUNT;

    DMA1_Channel1->CCR |= DMA_CCR_EN;

    DMA1->IFCR |= DMA_IFCR_CGIF1;                               // clear flags
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    NVIC_SetPriority(DMA1_Channel1_IRQn, 0);

    // Init ADC
    RCC->APBENR2 |= RCC_APBENR2_ADCEN;

    ADC1->CR |= ADC_CR_ADCAL;
    while (ADC1->CR & ADC_CR_ADCAL);

    ADC->CCR |= ADC_CCR_VREFEN;

    ADC1->CFGR1 |= ADC_CFGR1_DMACFG | ADC_CFGR1_DMAEN;          // DMA enable, DMA circular mode

    ADC1->SMPR |= ADC_SMPR_SMP1;                                // 160.5 ADC clock cycles

    // ADC1->CHSELR |= ADC_CHSELR_CHSEL0 | ADC_CHSELR_CHSEL1 | ADC_CHSELR_CHSEL2 | ADC_CHSELR_CHSEL3 | ADC_CHSELR_CHSEL4 |
    //                 ADC_CHSELR_CHSEL6 | ADC_CHSELR_CHSEL7 | ADC_CHSELR_CHSEL8 | ADC_CHSELR_CHSEL9;

    ADC1->CFGR1 |= ADC_CFGR1_EXTEN_0 | ADC_CFGR1_EXTSEL_0 | ADC_CFGR1_EXTSEL_1;      // TIM3_TRGO
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
        adc_lowpass_factors[ADC_CHANNEL_INDEX(i)] = calculate_rc_factor(adc_cfg[i].rc_factor);
    }

    RCC->APBENR1 |= RCC_APBENR1_TIM3EN;
    TIM3->PSC = F_CPU / 1000000 - 1;
    TIM3->ARR = ADC_POLL_PERIOD_US - 1;
    TIM3->CR2 |= TIM_CR2_MMS_1;             // The update event is selected as trigger output (TRGO).
    TIM3->CR1 |= TIM_CR1_CEN;

    ADC1->CR |= ADC_CR_ADSTART;
}

void adc_set_lowpass_rc(uint8_t channel, uint16_t rc_ms)
{
    adc_lowpass_factors[channel] = fix16_div(
        fix16_one,
        fix16_add(
            fix16_one,
            fix16_div(
                fix16_from_int(rc_ms),
                F16(ADC_POLL_PERIOD_US / 1000.0)
            )
        )
    );
}

fix16_t adc_get_channel_raw_value(uint8_t channel)
{
    return adc_lowpass_values[ADC_CHANNEL_INDEX(channel)];
}

void DMA1_Channel1_IRQHandler(void)
{
    static uint8_t run_counter = 0;

    if (DMA1->ISR & DMA_ISR_TCIF1) {
        DMA1->IFCR |= DMA_IFCR_CGIF1;
        for (uint8_t i = 0; i < ADC_CHANNEL_COUNT; i++) {
            if (run_counter >= 50) {
                adc_lowpass_values[i] += fix16_mul(adc_lowpass_factors[i],
                                                   fix16_sub(fix16_from_int(adc_raw_values[i]), adc_lowpass_values[i]));
            } else {
                adc_lowpass_values[i] = fix16_from_int(adc_raw_values[i]);
                run_counter++;
            }
        }
    }
}