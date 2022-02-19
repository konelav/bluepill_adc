#include <string.h>
#include "adc.h"
#include "console.h"
#include "led.h"

/* USB Standard Device Descriptor */
const uint8_t ADC_DeviceDescriptor[] = {
    0x12,   /* bLength */
    USB_DEVICE_DESCRIPTOR_TYPE,     /* bDescriptorType */
    0x10,
    0x01,   /* bcdUSB = 1.10 */
    0xff,   /* bDeviceClass: Vendor Specific */
    0x00,   /* bDeviceSubClass */
    0x02,   /* bDeviceProtocol */
    ADC_MAX_PACKET_SIZE,   /* bMaxPacketSize0 */
    (ADC_VID &  0xFF),
    (ADC_VID >>    8),   /* idVendor = ADC_PID */
    (ADC_PID &  0xFF),
    (ADC_PID >>    8),   /* idProduct = ADC_VID */
    0x04,
    0x03,   /* bcdDevice = 3.04 */
    0,              /* Index of string descriptor describing manufacturer */
    0,              /* Index of string descriptor describing product */
    0,              /* Index of string descriptor describing the device's serial number */
    0x01    /* bNumConfigurations */
};
ONE_DESCRIPTOR Device_Descriptor = {
    (uint8_t*)ADC_DeviceDescriptor,
    ADC_SIZ_DEVICE_DESC
};

const uint8_t ADC_ConfigDescriptor[] = {
    /*Configuration Descriptor*/
    0x09,   /* bLength: Configuration Descriptor size */
    USB_CONFIGURATION_DESCRIPTOR_TYPE,      /* bDescriptorType: Configuration */
    ADC_SIZ_CONFIG_DESC,       /* wTotalLength:no of returned bytes */
    0x00,
    0x01,   /* bNumInterfaces: 1 interface */
    0x01,   /* bConfigurationValue: Configuration value */
    0x00,   /* iConfiguration: Index of string descriptor describing the configuration */
    0x80,   /* bmAttributes */
    0x30,   /* MaxPower x2 mA */
    /*Interface Descriptor*/
    0x09,   /* bLength: Interface Descriptor size */
    USB_INTERFACE_DESCRIPTOR_TYPE,  /* bDescriptorType: Interface */
    /* Interface descriptor type */
    0x00,   /* bInterfaceNumber: Number of Interface */
    0x00,   /* bAlternateSetting: Alternate setting */
    0x01,   /* bNumEndpoints: One endpoints used */
    0xff,   /* bInterfaceClass: Vendor Specific */
    0x01,   /* bInterfaceSubClass */
    0x02,   /* bInterfaceProtocol */
    0x00,   /* iInterface: */
    /*Endpoint 1 Descriptor*/
    0x07,   /* bLength: Endpoint Descriptor size */
    USB_ENDPOINT_DESCRIPTOR_TYPE,   /* bDescriptorType: Endpoint */
    0x81,   /* bEndpointAddress: EP 1 IN */
    0x02,   /* bmAttributes: Bulk */
    ADC_PACKET_SIZE,      /* wMaxPacketSize: */
    0x00,
    0x0
};
ONE_DESCRIPTOR Config_Descriptor = {
    (uint8_t*)ADC_ConfigDescriptor,
    ADC_SIZ_CONFIG_DESC
};

DEVICE Device_Table = {
    EP_NUM,
    1
};

static void adc_init(void);
static void adc_reset(void);
static void adc_status_in(void);
static void adc_status_out(void);
static RESULT adc_data_setup(uint8_t RequestNo);
static RESULT adc_nodata_setup(uint8_t RequestNo);
static RESULT adc_get_interface_setting(uint8_t Interface, uint8_t AlternateSetting);
static uint8_t *adc_get_device_descriptor(uint16_t Length);
static uint8_t *adc_get_config_descriptor(uint16_t Length);
static uint8_t *adc_get_string_descriptor(uint16_t Length);
DEVICE_PROP Device_Property = {
    adc_init,
    adc_reset,
    adc_status_in,
    adc_status_out,
    adc_data_setup,
    adc_nodata_setup,
    adc_get_interface_setting,
    adc_get_device_descriptor,
    adc_get_config_descriptor,
    adc_get_string_descriptor,
    0,
    ADC_MAX_PACKET_SIZE
};
USER_STANDARD_REQUESTS User_Standard_Requests = {
    adc_get_configuration,
    adc_set_configuration,
    adc_get_interface,
    adc_set_interface,
    adc_get_status,
    adc_clear_feature,
    adc_set_end_point_feature,
    adc_set_device_feature,
    adc_set_device_address
};


uint32_t adc_tx_total = 0;
uint32_t adc_rx_total = 0;
volatile int usb_tx_in_progress = 0;

typedef uint8_t USBPacket[ADC_PACKET_SIZE];

static USBPacket usb_packets[ADC_SAMPLES_COUNT];
static volatile int usb_first_packet = 0;
static volatile int usb_last_packet = 0;

static ADCRegs regs = {
    .cmd            = ADC_DEFAULT_COMMAND,
    .channels       = ADC_DEFAULT_CHANNELS,
    .bits           = ADC_DEFAULT_BITS,
    .frequency      = ADC_DEFAULT_FREQUENCY,
    .offset         = 0,
    .gain           = 0,
    .samples        = ADC_DEFAULT_SAMPLES,
    .trigger        = ADC_TRIGGER_NONE,
    .trig_channel   = 0,
    .trig_level     = 0x7ff,
    .trig_offset    = 0,
    .trig_t_min     = 0,
    .trig_t_max     = 0
};
static uint8_t reg_requested_value[2] = {0x00, 0x00};

static ADCPacketHeader header;
static int nchannels = 0;
static int samples_per_packet = 0;
static int samples_in_reversed_order = 0;
static int trigger_chan_index = -1;
static uint32_t samples_per_trigger = 0;

volatile int is_triggered = 0;

static int trig_wait = 1;
static int trig_event = 0;
static int trig_strobe_started = 0;
static uint32_t trig_holded = 0;
static uint32_t trig_rx_cnt0 = 0;
static uint32_t trig_tx_cnt0 = 0;

/* we need double buffer:
 *     - one half is filling with ADC values via DMA
 *     - other half is being processed for transfering via USB
 * also for 2-bit mode we need 4 samples per 1 byte
 */
static uint16_t adcdma_rx_buf[ADC_SAMPLE_SIZE * 2 * 4];

static void trigger_reset(int restart) {
    is_triggered = 0;
    trig_event = 0;
    trig_rx_cnt0 = 0;
    trig_tx_cnt0 = 0;
    trig_strobe_started = 0;
    trig_holded = 0;
    switch (regs.cmd) {
    case ADC_CMD_STOP:
        trig_wait = 0;
        break;
    case ADC_CMD_ONCE:
        trig_wait = restart;
        break;
    default:
    case ADC_CMD_CONTINUOUS:
        trig_wait = 1;
        break;
    }
}

static void set_first_packet(int id) {
    ADCPacketHeader * hdr;
    if (id < 0)
        id = 0;
    else
        id = id % ADC_SAMPLES_COUNT;
    hdr = (ADCPacketHeader*)usb_packets[id];
    hdr->sequence |= 0x80;
    usb_first_packet = id;
}

static int check_trigger(uint16_t *levels) {
    int i;
    uint16_t prev_level;
    
    if (!trig_event) {
        if (!trig_wait)
            return 0;
        
        prev_level = levels[trigger_chan_index];
        
        switch (regs.trigger) {
        default:
        case ADC_TRIGGER_NONE:
            trig_event = 1;
            break;
        case ADC_TRIGGER_RISING:
            for (i = trigger_chan_index + nchannels; i < samples_per_packet; i += nchannels) {
                if (prev_level < regs.trig_level && levels[i] > regs.trig_level) {
                    trig_event = 1;
                    break;
                }
                prev_level = levels[i];
            }
            break;
        case ADC_TRIGGER_FALLING:
            for (i = trigger_chan_index + nchannels; i < samples_per_packet; i += nchannels) {
                if (prev_level > regs.trig_level && levels[i] < regs.trig_level) {
                    trig_event = 1;
                    break;
                }
                prev_level = levels[i];
            }
            break;
        case ADC_TRIGGER_THRESHOLD:
            for (i = trigger_chan_index + nchannels; i < samples_per_packet; i += nchannels) {
                if ((prev_level < regs.trig_level && levels[i] > regs.trig_level) ||
                    (prev_level > regs.trig_level && levels[i] < regs.trig_level)) {
                    trig_event = 1;
                    break;
                }
                prev_level = levels[i];
            }
            break;
        case ADC_TRIGGER_STROBE_LO:
            for (i = trigger_chan_index; i < samples_per_packet; i += nchannels) {
                if (!trig_strobe_started && prev_level > regs.trig_level && levels[i] < regs.trig_level) {
                    trig_strobe_started = 1;
                    trig_holded = 0;
                }
                else if (trig_strobe_started && levels[i] < regs.trig_level) {
                    trig_holded++;
                }
                else if (trig_strobe_started && regs.trig_t_min <= trig_holded &&
                         (regs.trig_t_max == 0 || trig_holded <= regs.trig_t_max)) {
                    trig_event = 1;
                    break;
                }
                else {
                    trig_strobe_started = 0;
                }
                prev_level = levels[i];
            }
            break;
        case ADC_TRIGGER_STROBE_HI:
            for (i = trigger_chan_index; i < samples_per_packet; i += nchannels) {
                if (!trig_strobe_started && prev_level < regs.trig_level && levels[i] > regs.trig_level) {
                    trig_strobe_started = 1;
                    trig_holded = 0;
                }
                else if (trig_strobe_started && levels[i] > regs.trig_level) {
                    trig_holded++;
                }
                else if (trig_strobe_started && regs.trig_t_min <= trig_holded &&
                         (regs.trig_t_max == 0 || trig_holded <= regs.trig_t_max)) {
                    trig_event = 1;
                    break;
                }
                else {
                    trig_strobe_started = 0;
                }
                prev_level = levels[i];
            }
            break;
        }
        
        if (trig_event) {
            int32_t trigger_offset_signed = (int32_t)regs.trig_offset;
            int offset = (trigger_offset_signed < 0 ? -trigger_offset_signed : 0);
            int periods_per_packet = samples_per_packet / nchannels;
            int packets_offset = offset / periods_per_packet;
            set_first_packet(usb_last_packet - packets_offset + ADC_SAMPLES_COUNT);
            trig_rx_cnt0 = adc_rx_total;
            trig_tx_cnt0 = 0;
            if (!usb_tx_in_progress) {
                
            }
        }
    }
    
    if (trig_event) {
        int32_t trigger_offset_signed = (int32_t)regs.trig_offset;
        int offset = (trigger_offset_signed > 0 ? +trigger_offset_signed : 0);
        uint32_t samples_sent;
        uint32_t samples_received = adc_rx_total - trig_rx_cnt0;
        if (samples_received < offset * nchannels) {
            set_first_packet(usb_last_packet);
            return 0;
        }
        if (trig_tx_cnt0 == 0)
            trig_tx_cnt0 = adc_tx_total;
        samples_sent = adc_tx_total - trig_tx_cnt0;
        if (samples_sent >= samples_per_trigger * nchannels) {
            trigger_reset(0);
            return 0;
        }
        return 1;
    }
    
    return 0;
}

static int write_reg(uint8_t index, uint8_t value) {
    DBG_VAL("write_reg(index = 0x", index, 10, ")");
    DBG_VAL("  value = 0x", value, 16, "");
    
    if (index < sizeof(regs)) {
        uint8_t * pregs = (uint8_t*)&regs;
        pregs[index] = value;
        return 1;
    }
    
    return 0;
}

static uint8_t *read_reg(uint16_t length) {
    uint8_t * pregs = (uint8_t*)&regs;
    int wlength = 0;
    
    DBG_VAL("read_reg(length = ", length, 10, ")");
    
    if (pInformation->USBwIndexs.bw.bb0 < sizeof(regs))
        reg_requested_value[wlength++] = pregs[pInformation->USBwIndexs.bw.bb0];
    if (pInformation->USBwIndexs.bw.bb1 < sizeof(regs))
        reg_requested_value[wlength++] = pregs[pInformation->USBwIndexs.bw.bb1];
    
    if (length == 0) {
        pInformation->Ctrl_Info.Usb_wLength = wlength;
        return NULL;
    }
    return (uint8_t*)reg_requested_value;
}

static int bitmask_to_array(uint16_t bitmask, uint8_t indicies[ADC_TOTAL_CHANNELS], uint8_t *last_reset) {
    int ret = 0;
    int nbit;
    *last_reset = 255;
    for (nbit = 0; nbit < ADC_TOTAL_CHANNELS; nbit++)
        if (bitmask & (1 << nbit)) {
            indicies[ret++] = nbit;
        }
        else {
            *last_reset = nbit;
        }
    return ret;
}

static void update_mode(void) {
    uint8_t channels[ADC_TOTAL_CHANNELS], unselected, chan;
    uint32_t adc_sample_period_us = 1;
    uint32_t adc_sample_time = ADC_SampleTime_1Cycles5;
    int continuous_mode = 0;
    int interleave_mode = 0;
    int i;
    
    console_flush_from_it();
    DBG_STR("update_mode()");
    
    TIM_Cmd(TIM1, DISABLE);
    while (DMA_GetITStatus(DMA1_IT_TC1) != RESET)
        ;
    while (DMA_GetITStatus(DMA1_IT_HT1) != RESET)
        ;
    
    DMA_DeInit(DMA1_Channel1);
    ADC_DeInit(ADC1);
    ADC_DeInit(ADC2);
    TIM_DeInit(TIM1);
    
    SetEPTxCount(ENDP1, 0);
    SetEPTxStatus(ENDP1, EP_TX_NAK);
    
    usb_first_packet = usb_last_packet = 0;
    usb_tx_in_progress = 0;
    
    INF_VAL("current_command: ", regs.cmd, 10, "");
    INF_VAL("channels requested: 0b", regs.channels, 2, "");
    INF_VAL("frequency requested: ", regs.frequency, 10, "");
    INF_VAL("bits per sample requested: ", regs.bits, 10, "");
    console_flush_from_it();
    
    regs.use_channels = regs.channels;
    nchannels = bitmask_to_array(regs.use_channels, channels, &unselected);
    
    if (nchannels == 0 || regs.cmd == ADC_CMD_STOP || regs.frequency == ADC_FREQUENCY_OFF) {
        led_set_period(BLINK_MODE_NONE);
        return;
    }
    switch (regs.bits) {
    default:
    case ADC_BITS_DIGITAL:
    case ADC_BITS_LO:
        led_set_period(BLINK_MODE_LORES);
        break;
    case ADC_BITS_MID:
        led_set_period(BLINK_MODE_MIDRES);
        break;
    case ADC_BITS_HI:
        led_set_period(BLINK_MODE_HIRES);
        break;
    }
    
    if ((nchannels > 1) && (nchannels % 2 != 0)) { /* select additional channel so ADC1 and ADC2 will be synced */
        WRN_VAL("forcing selection of channel #", unselected, 10, "");
        channels[nchannels++] = unselected;
        regs.use_channels |= (1 << unselected);
    }
    
    if ((regs.bits == ADC_BITS_HI  && nchannels == 6) ||
        (regs.bits == ADC_BITS_MID && nchannels == 8) ) {
        WRN_VAL("wrong mode, bits=", regs.bits, 10, "");
        WRN_VAL("  nchans=", nchannels, 10, "");
        WRN_STR("  selecting another two channels");
        for (i = 0; i < 2; i++) {
            nchannels = bitmask_to_array(regs.use_channels, channels, &unselected);
            channels[nchannels++] = unselected;
            regs.use_channels |= (1 << unselected);
        }
    }
    INF_VAL("channels selected: 0b", regs.use_channels, 2, "");

    samples_per_trigger = (1 << (regs.samples + 10));
    samples_per_packet = (ADC_SAMPLE_SIZE * 8) / regs.bits;
    INF_VAL("samples per trigger: ", samples_per_trigger, 10, "");
    INF_VAL("samples per packet: ", samples_per_packet, 10, "");
    console_flush_from_it();
    
    header.sequence = 0;
    header.channels = regs.use_channels;
    header.mode = ((regs.bits & 0x0F) | 
                   ((regs.frequency & 0x0F) << 4));
    
    {
        DMA_InitTypeDef s;
        s.DMA_PeripheralBaseAddr = (uint32_t)(&ADC1->DR);
        s.DMA_MemoryBaseAddr = (uint32_t)(&adcdma_rx_buf[0]);
        s.DMA_DIR = DMA_DIR_PeripheralSRC;
        s.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
        s.DMA_MemoryInc = DMA_MemoryInc_Enable;
        if (nchannels > 1 || (nchannels == 1 && regs.frequency == ADC_FREQUENCY_MAX)) {
            /* there are two (ADC1&ADC2) values (samples) in each transfer,
             * but we need double buffer for half-transfer handling:
             *   first half:  (*uint32_t)[0:samples_per_packet/2]
             *   second half: (*uint32_t)[sampler_per_packet/2:samples_per_packet]
             */
            s.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
            s.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
            s.DMA_BufferSize = samples_per_packet;
        }
        else {
            /* there is one (ADC1) value (sample) in each transfer,
             * and we need double buffer for half-transfer handling:
             *   first half:  (*uint16_t)[0:samples_per_packet]
             *   second half: (*uint16_t)[sampler_per_packet:samples_per_packet*2]
             */
            s.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
            s.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
            s.DMA_BufferSize = samples_per_packet * 2;
        }
        s.DMA_Mode = DMA_Mode_Circular;
        s.DMA_Priority = DMA_Priority_High;
        s.DMA_M2M = DMA_M2M_Disable;
        DMA_Init(DMA1_Channel1, &s);
        DMA_Cmd(DMA1_Channel1, ENABLE);
    }
    
    {
        TIM_TimeBaseInitTypeDef s;
        samples_in_reversed_order = continuous_mode = interleave_mode = 0;
        switch (regs.frequency) {
        case ADC_FREQUENCY_MAX:
            continuous_mode = 1;
            if (nchannels == 1)  
                /* Fast interleaved mode: 
                     ADC2 value (sampled first) in upper halfword,
                     ADC1 value (sampled second) in lower halfword 
                */
                samples_in_reversed_order = interleave_mode = 1;
            adc_sample_time = ADC_SampleTime_1Cycles5;
            break;
        case ADC_FREQUENCY_500KHZ:
            adc_sample_time = ADC_SampleTime_7Cycles5;
            adc_sample_period_us = 2;
            break;
        case ADC_FREQUENCY_200KHZ:
            adc_sample_time = ADC_SampleTime_41Cycles5;
            adc_sample_period_us = 5;
            break;
        case ADC_FREQUENCY_100KHZ:
            adc_sample_time = ADC_SampleTime_71Cycles5;
            adc_sample_period_us = 10;
            break;
        case ADC_FREQUENCY_50KHZ:
            adc_sample_time = ADC_SampleTime_71Cycles5;
            adc_sample_period_us = 20;
            break;
        case ADC_FREQUENCY_20KHZ:
            adc_sample_time = ADC_SampleTime_239Cycles5;
            adc_sample_period_us = 50;
            break;
        case ADC_FREQUENCY_10KHZ:
            adc_sample_time = ADC_SampleTime_239Cycles5;
            adc_sample_period_us = 100;
            break;
        case ADC_FREQUENCY_5KHZ:
            adc_sample_time = ADC_SampleTime_239Cycles5;
            adc_sample_period_us = 200;
            break;
        case ADC_FREQUENCY_2KHZ:
            adc_sample_time = ADC_SampleTime_239Cycles5;
            adc_sample_period_us = 500;
            break;
        case ADC_FREQUENCY_1KHZ:
            adc_sample_time = ADC_SampleTime_239Cycles5;
            adc_sample_period_us = 1000;
            break;
        }
        if (nchannels > 1)
            adc_sample_period_us *= (nchannels / 2);
        s.TIM_Period = adc_sample_period_us - 1;
        s.TIM_Prescaler = (SystemCoreClock / 1000000) - 1;
        s.TIM_ClockDivision = 0;
        s.TIM_CounterMode = TIM_CounterMode_Up;
        TIM_TimeBaseInit(TIM1, &s);
    }
    {
        TIM_OCInitTypeDef s;
        s.TIM_OCMode = TIM_OCMode_PWM1; 
        s.TIM_OutputState = TIM_OutputState_Enable;                
        s.TIM_Pulse = 1; 
        s.TIM_OCPolarity = TIM_OCPolarity_Low;         
        TIM_OC1Init(TIM1, &s);
    }
    
    {
        ADC_InitTypeDef s;
        
        if (nchannels > 1) {
            s.ADC_Mode = ADC_Mode_RegSimult;
            s.ADC_NbrOfChannel = nchannels / 2;
        }
        else if (interleave_mode) {
            s.ADC_Mode = ADC_Mode_FastInterl;
            s.ADC_NbrOfChannel = nchannels;
        }
        else {
            s.ADC_Mode = ADC_Mode_Independent;
            s.ADC_NbrOfChannel = nchannels;
        }
        
        if (continuous_mode) {
            s.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
            s.ADC_ContinuousConvMode = ENABLE;
        }
        else {
            s.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T1_CC1;
            s.ADC_ContinuousConvMode = DISABLE;
        }
        
        s.ADC_ScanConvMode = ENABLE;
        s.ADC_DataAlign = ADC_DataAlign_Right;
        ADC_Init(ADC1, &s);
        
        if (nchannels > 1 || interleave_mode) {
            s.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
            ADC_Init(ADC2, &s);
        }
    }
    
    trigger_chan_index = -1;
    
    if (nchannels > 1) {
        for (chan = 0; chan < nchannels / 2; chan++) {
            int chan_adc1 = channels[2 * chan + 0];
            int chan_adc2 = channels[2 * chan + 1];
            ADC_RegularChannelConfig(ADC1, chan_adc1, chan + 1, adc_sample_time);
            ADC_RegularChannelConfig(ADC2, chan_adc2, chan + 1, adc_sample_time);
            INF_VAL("Channel ", chan_adc1, 10, " set for ADC1");
            INF_VAL("Channel ", chan_adc2, 10, " set for ADC2");
            if (regs.trig_channel == chan_adc1)
                trigger_chan_index = 2 * chan + 0;
            else if (regs.trig_channel == chan_adc2)
                trigger_chan_index = 2 * chan + 1;
        }
    }
    else {
        for (chan = 0; chan < nchannels; chan++) {
            int chan_adc1 = channels[chan];
            ADC_RegularChannelConfig(ADC1, chan_adc1, chan + 1, adc_sample_time);
            INF_VAL("Channel ", chan_adc1, 10, " set for ADC1");
            if (interleave_mode) {
                ADC_RegularChannelConfig(ADC2, chan_adc1, chan + 1, adc_sample_time);
                INF_VAL("Channel ", chan_adc1, 10, " set for ADC2 (interleave)");
            }
            if (regs.trig_channel == chan_adc1)
                trigger_chan_index = chan;
        }
    }
    console_flush_from_it();
    
    INF_VAL("Trigger: ", regs.trigger, 10, "");
    INF_VAL("Trigger channel number: ", regs.trig_channel, 10, "");
    INF_VAL("Trigger channel index: ", trigger_chan_index, 10, "");
    INF_VAL("Command: ", regs.cmd, 10, "");
    console_flush_from_it();
    
    if (trigger_chan_index < 0) {
        WRN_STR("Channel for trigger is not enabled, set to first one");
        trigger_chan_index = 0;
    }
    
    ADC_DMACmd(ADC1, ENABLE);
    
    ADC_Cmd(ADC1, ENABLE);
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1) == SET)
        ;
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1) == SET)
        ;
    
    if (nchannels > 1 || interleave_mode) {
        ADC_ExternalTrigConvCmd(ADC2, ENABLE);
    
        ADC_Cmd(ADC2, ENABLE);
        ADC_ResetCalibration(ADC2);
        while (ADC_GetResetCalibrationStatus(ADC2) == SET)
            ;
        ADC_StartCalibration(ADC2);
        while (ADC_GetCalibrationStatus(ADC2) == SET)
            ;
    }
    
    INF_STR("ADC calibration done");
    
    DMA_ITConfig(DMA1_Channel1, DMA_IT_TC | DMA_IT_HT, ENABLE);
    
    NVIC_PriorityGroupConfig(IRQ_PRIO_GROUP_CFG);
    {
        NVIC_InitTypeDef s;
        s.NVIC_IRQChannel = ADCDMA_IRQ;
        s.NVIC_IRQChannelPreemptionPriority = ADCDMA_IRQ_PRIO;
        s.NVIC_IRQChannelSubPriority = 0;
        s.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&s);
    }
    
    trigger_reset(1);
    
    if (continuous_mode) {
        ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    }
    else {
        ADC_ExternalTrigConvCmd(ADC1, ENABLE);
        TIM_Cmd(TIM1, ENABLE);
        TIM_CtrlPWMOutputs(TIM1, ENABLE);
    }
}

static void adc_init(void) {
    INF_STR("adc init");

    pInformation->Current_Configuration = 0;
    usbd_power_on();
    USB_SIL_Init();
}

static void adc_reset(void) {
    INF_STR("adc reset");
    
    pInformation->Current_Configuration = 0;
    pInformation->Current_Feature = ADC_ConfigDescriptor[7];
    pInformation->Current_Interface = 0;
    SetBTABLE(BTABLE_ADDRESS);

    /* Initialize Endpoint 0 */
    SetEPType(ENDP0, EP_CONTROL);
    SetEPTxStatus(ENDP0, EP_TX_STALL);
    SetEPRxAddr(ENDP0, ENDP0_RXADDR);
    SetEPTxAddr(ENDP0, ENDP0_TXADDR);
    Clear_Status_Out(ENDP0);
    SetEPRxCount(ENDP0, Device_Property.MaxPacketSize);
    SetEPRxValid(ENDP0);

    /* Initialize Endpoint 1 */
    SetEPType(ENDP1, EP_BULK);
    SetEPTxAddr(ENDP1, ENDP1_TXADDR);
    SetEPTxCount(ENDP1, 0);
    SetEPTxStatus(ENDP1, EP_TX_NAK);
    SetEPRxStatus(ENDP1, EP_RX_DIS);
    
    /* Set this device to response on default address */
    SetDeviceAddress(0);
    
    update_mode();
    adc_tx_total = adc_rx_total = 0;
}

static void adc_status_in(void) {
    DBG_STR("status_in()");
}

static void adc_status_out(void) {
    DBG_STR("status_out()");
}

static RESULT adc_data_setup(uint8_t RequestNo) {
    uint8_t    *(*CopyRoutine)(uint16_t);

    CopyRoutine = NULL;

    DBG_VAL("data_setup(RequestNo = 0x", RequestNo, 16, ")");
    DBG_VAL("  Type_Recipient = 0x", Type_Recipient, 16, ")");
    DBG_VAL("  USBwValues  = 0x", pInformation->USBwValues.w, 16, ")");
    DBG_VAL("  USBwIndexs  = 0x", pInformation->USBwIndexs.w, 16, ")");
    DBG_VAL("  USBwLengths = 0x", pInformation->USBwLengths.w, 16, ")");
    
    if (Type_Recipient == (VENDOR_REQUEST | DEVICE_RECIPIENT)) {
        switch (RequestNo) {
        case ADC_REQUEST_SETUP:
            CopyRoutine = read_reg;
            break;
        default:
            break;
        }
    }
    
    if (CopyRoutine == NULL)
        return USB_UNSUPPORT;

    pInformation->Ctrl_Info.CopyData = CopyRoutine;
    pInformation->Ctrl_Info.Usb_wOffset = 0;
    (*CopyRoutine)(0);
    return USB_SUCCESS;
}

static RESULT adc_nodata_setup(uint8_t RequestNo) {
    DBG_VAL("nodata_setup(RequestNo = 0x", RequestNo, 16, ")");
    DBG_VAL("  Type_Recipient = 0x", Type_Recipient, 16, ")");
    DBG_VAL("  USBwValues = 0x", pInformation->USBwValues.w, 16, ")");
    DBG_VAL("  USBwIndexs = 0x", pInformation->USBwIndexs.w, 16, ")");
    
    if (RequestNo == ADC_REQUEST_SETUP) {
        if (write_reg(pInformation->USBwIndexs.bw.bb0, pInformation->USBwValues.bw.bb0) ||
            write_reg(pInformation->USBwIndexs.bw.bb1, pInformation->USBwValues.bw.bb1)) {
            update_mode();
            return USB_SUCCESS;
        }
    }

    return USB_UNSUPPORT;
}

static RESULT adc_get_interface_setting(uint8_t Interface, uint8_t AlternateSetting) {
    DBG_VAL("get_interface_setting(Interface = 0x", Interface, 16, ")");
    if (AlternateSetting > 0)
        return USB_UNSUPPORT;
    else if (Interface > 1)
        return USB_UNSUPPORT;
    return USB_SUCCESS;
}

static uint8_t *adc_get_device_descriptor(uint16_t Length) {
    DBG_VAL("get_device_descriptor(Length = ", Length, 10, ")");
    return Standard_GetDescriptorData(Length, &Device_Descriptor);
}

static uint8_t *adc_get_config_descriptor(uint16_t Length) {
    DBG_VAL("get_config_descriptor(Length = ", Length, 10, ")");
    return Standard_GetDescriptorData(Length, &Config_Descriptor);
}

static uint8_t *adc_get_string_descriptor(uint16_t Length) {
    uint8_t wValue0 = pInformation->USBwValue0;
    DBG_VAL("get_string_descriptor(wValue0 = 0x", wValue0, 16, ")");
    return NULL;
}

static void schedule_transmission() {
    STM_ARR(" (adc) ", (const char*)usb_packets[usb_first_packet], sizeof(USBPacket), "");
    USB_SIL_Write(ENDP1, usb_packets[usb_first_packet], sizeof(USBPacket));
    SetEPTxValid(ENDP1);
    usb_tx_in_progress = 1;
    usb_first_packet = (usb_first_packet + 1) % ADC_SAMPLES_COUNT;
}

void adcdma_irq() {
    uint16_t *src;
    uint8_t *dst = (uint8_t*)usb_packets[usb_last_packet];
    ADCPacketHeader *pHeader = (ADCPacketHeader*)dst;
    uint8_t *pBody = dst + sizeof(ADCPacketHeader);
    uint32_t i;
    int next_usb_last_packet;
    
    if (DMA_GetITStatus(DMA1_IT_HT1) == SET) {
        src = &adcdma_rx_buf[0];
        DMA_ClearITPendingBit(DMA1_IT_HT1);
    }
    else if (DMA_GetITStatus(DMA1_IT_TC1) == SET) {
        src = &adcdma_rx_buf[samples_per_packet];
        DMA_ClearITPendingBit(DMA1_IT_TC1);
    }
    else /* should not happen */
        return;
    
    adc_rx_total += samples_per_packet;
    
    header.sequence = (header.sequence + 1) & 0x7f;
    *pHeader = header;
    
    if (samples_in_reversed_order) {
        static uint16_t swapped_src[ADC_SAMPLE_SIZE * 4];
        uint32_t *src_u32 = (uint32_t*)src;
        uint32_t *dst_u32 = (uint32_t*)swapped_src;
        for (i = 0; i < samples_per_packet; i += 2) {
            *(dst_u32++) = (src_u32[0] >> 16) | (src_u32[0] << 16);
            src_u32++;
        }
        src = &swapped_src[0];
    }
    is_triggered = check_trigger(src);
    
    switch (header.mode & 0x0F) {
    case ADC_BITS_DIGITAL:
        for (i = 0; i < samples_per_packet; i += 4) {
            uint32_t v1 = ((uint32_t)(*(src++)) - regs.offset) << regs.gain;
            uint32_t v2 = ((uint32_t)(*(src++)) - regs.offset) << regs.gain;
            uint32_t v3 = ((uint32_t)(*(src++)) - regs.offset) << regs.gain;
            uint32_t v4 = ((uint32_t)(*(src++)) - regs.offset) << regs.gain;
            *(pBody++) = (uint8_t)(((v1 >>  4) & 0xc0) | 
                                   ((v2 >>  6) & 0x30) |
                                   ((v3 >>  8) & 0x0c) |
                                   ((v4 >> 10) & 0x03) );
        }
        break;
    case ADC_BITS_LO:
        for (i = 0; i < samples_per_packet; i += 2) {
            uint32_t v1 = ((uint32_t)(*(src++)) - regs.offset) << regs.gain;
            uint32_t v2 = ((uint32_t)(*(src++)) - regs.offset) << regs.gain;
            *(pBody++) = (uint8_t)(((v1 >> 4) & 0xf0) | ((v2 >> 8) & 0x0f));
        }
        break;
    case ADC_BITS_MID:
        for (i = 0; i < samples_per_packet; i += 2) {
            uint32_t v1 = ((uint32_t)(*(src++)) - regs.offset) << regs.gain;
            uint32_t v2 = ((uint32_t)(*(src++)) - regs.offset) << regs.gain;
            *(pBody++) = (uint8_t)(v1 >> 4);
            *(pBody++) = (uint8_t)(v2 >> 4);
        }
        break;
    case ADC_BITS_HI:
        for (i = 0; i < samples_per_packet; i += 2) {
            uint32_t v1 = ((uint32_t)(*(src++)) - regs.offset) << regs.gain;
            uint32_t v2 = ((uint32_t)(*(src++)) - regs.offset) << regs.gain;
            *(pBody++) = (uint8_t)(v1 >> 4);
            *(pBody++) = (uint8_t)(((v1 << 4) & 0xf0) | (v2 & 0x0f));
            *(pBody++) = (uint8_t)(v2 >> 4);
        }
        break;
    }
    
    next_usb_last_packet = (usb_last_packet + 1) % ADC_SAMPLES_COUNT;
    if (!is_triggered || !usb_tx_in_progress ||
        next_usb_last_packet != usb_first_packet) { /* no overflow */
        usb_last_packet = next_usb_last_packet;
    }
    
    if (is_triggered && !usb_tx_in_progress)
        schedule_transmission();
}

void adc_on_packet_transmitted() {
    DBG_STR("packet_transmitted()");
    adc_tx_total += samples_per_packet;
    if (!is_triggered) {
        usb_tx_in_progress = 0;
        return;
    }
    if (usb_last_packet != usb_first_packet)
        schedule_transmission();
    else
        usb_tx_in_progress = 0;
    if (usb_last_packet != usb_first_packet && !usb_tx_in_progress)
        schedule_transmission();
}
