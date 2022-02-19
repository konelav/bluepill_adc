#ifndef ADC_PROTO_H
#define ADC_PROTO_H

#include <stdint.h>

#define ADC_ID_VENDOR               0x1A87
#define ADC_ID_PRODUCT              0x5513

#define ADC_SAMPLES_EP              1

#define ADC_MAX_LEVEL               0xfff

#define ADC_SAMPLE_SIZE             60
#define ADC_PACKET_SIZE             (ADC_SAMPLE_SIZE + sizeof(ADCPacketHeader))

#define ADC_TOTAL_CHANNELS          10
#define ADC_SELECT_ALL_CHANNELS     ((1 << 10) - 1)

#define ADC_SIZ_DEVICE_DESC         18
#define ADC_SIZ_CONFIG_DESC         25

#define ADC_CMD_STOP                0
#define ADC_CMD_ONCE                1
#define ADC_CMD_CONTINUOUS          2

#define ADC_MODE_BITS               0x0F
#define ADC_MODE_FREQUENCY          0xF0

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

#define ADC_SAMPLES_COUNT           128

#pragma pack(1)
typedef struct {
    uint8_t     sequence;
    uint16_t    channels;
    uint8_t     mode;  /* bits per sample and sampling frequency */
} ADCPacketHeader;
#pragma pack()

#endif // ADC_PROTO_H
