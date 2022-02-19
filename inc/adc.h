#ifndef __ADC_H
#define __ADC_H

#include "config.h"
#include "stm32f10x_conf.h"
#include "hw_config.h"
#include "usbd.h"

#define ADC_PACKET_SIZE             (ADC_SAMPLE_SIZE + sizeof(ADCPacketHeader))

#define ADC_TOTAL_CHANNELS          10
#define ADC_SELECT_ALL_CHANNELS     ((1 << 10) - 1)

#define ADC_SIZ_DEVICE_DESC         18
#define ADC_SIZ_CONFIG_DESC         25

#define ADC_CMD_STOP                0
#define ADC_CMD_ONCE                1
#define ADC_CMD_CONTINUOUS          2

#define ADC_BITS_DIGITAL            2
#define ADC_BITS_LO                 4
#define ADC_BITS_MID                8
#define ADC_BITS_HI                 12

#define ADC_FREQUENCY_OFF           0
#define ADC_FREQUENCY_MAX           1
#define ADC_FREQUENCY_500KHZ        2
#define ADC_FREQUENCY_200KHZ        3
#define ADC_FREQUENCY_100KHZ        4
#define ADC_FREQUENCY_50KHZ         5
#define ADC_FREQUENCY_20KHZ         6
#define ADC_FREQUENCY_10KHZ         7
#define ADC_FREQUENCY_5KHZ          8
#define ADC_FREQUENCY_2KHZ          9
#define ADC_FREQUENCY_1KHZ          10

#define ADC_TRIGGER_NONE            0
#define ADC_TRIGGER_RISING          1
#define ADC_TRIGGER_FALLING         2
#define ADC_TRIGGER_THRESHOLD       3
#define ADC_TRIGGER_STROBE_LO       4
#define ADC_TRIGGER_STROBE_HI       5

#define adc_get_configuration       NOP_Process
#define adc_set_configuration       NOP_Process
#define adc_get_interface           NOP_Process
#define adc_set_interface           NOP_Process
#define adc_get_status              NOP_Process
#define adc_clear_feature           NOP_Process
#define adc_set_end_point_feature   NOP_Process
#define adc_set_device_feature      NOP_Process
#define adc_set_device_address      NOP_Process

#define ADC_REQUEST_SETUP           1

#define ADC_INDEX_CMD               1
#define ADC_INDEX_CHANNELS          2
#define ADC_INDEX_BITS              4
#define ADC_INDEX_FREQUENCY         5
#define ADC_INDEX_OFFSET            6
#define ADC_INDEX_GAIN              8
#define ADC_INDEX_SAMPLES           9
#define ADC_INDEX_TRIGGER           10
#define ADC_INDEX_TRIG_CHANNEL      11
#define ADC_INDEX_TRIG_LEVEL        12
#define ADC_INDEX_TRIG_OFFSET       14
#define ADC_INDEX_TRIG_T_MIN        18
#define ADC_INDEX_TRIG_T_MAX        22
#define ADC_INDEX_USE_CHANNELS      26


#pragma pack(1)
typedef struct {
    uint8_t     reserved;
    uint8_t     cmd;
    uint16_t    channels;
    uint8_t     bits;
    uint8_t     frequency;
    uint16_t    offset;
    uint8_t     gain;
    uint8_t     samples;
    uint8_t     trigger;
    uint8_t     trig_channel;
    uint16_t    trig_level;
    uint32_t    trig_offset;
    uint32_t    trig_t_min;
    uint32_t    trig_t_max;
    uint16_t    use_channels;
} ADCRegs;

typedef struct {
    uint8_t     sequence;
    uint16_t    channels;
    uint8_t     mode;  /* bits per sample and sampling frequency */
} ADCPacketHeader;
#pragma pack()

extern uint32_t adc_rx_total;
extern uint32_t adc_tx_total;
extern volatile int is_triggered;
extern volatile int usb_tx_in_progress;

void adcdma_irq(void);

void adc_on_packet_transmitted(void);

#endif /* __ADC_H */
