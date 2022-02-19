#ifndef __HW_CONFIG_H
#define __HW_CONFIG_H

#include "config.h"
#include "stm32f10x_conf.h"

/*
 * B10, B11     - USART3 (TX, RX)   - console       [5V FT]
 * A0:7, B0:1   - ADC12_IN0:7       - ADC channels
 * A11, A12     - USB (DM, DP)      - USB FS device
 * C13          - LED               - led
 */

#define USB_GPIO                GPIOA
#define USB_DM_PIN              GPIO_Pin_11
#define USB_DP_PIN              GPIO_Pin_12
#define USB_IRQ                 USB_LP_CAN1_RX0_IRQn
#define USB_IRQ_HANDLER         USB_LP_CAN1_RX0_IRQHandler

#define CONSOLE_GPIO            GPIOB
#define CONSOLE_TX              GPIO_Pin_10
#define CONSOLE_RX              GPIO_Pin_11
#define CONSOLE_IRQ             USART3_IRQn
#define CONSOLE_IRQ_HANDLER     USART3_IRQHandler
#define CONSOLE_USART           USART3

#define ADC_GPIO1               GPIOA
#define ADC_GPIO2               GPIOB
#define ADC_PINS1               (\
    GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3 | \
    GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7)
#define ADC_PINS2               (\
    GPIO_Pin_0 | GPIO_Pin_1)
#define ADCDMA_IRQ              DMA1_Channel1_IRQn
#define ADCDMA_IRQ_HANDLER      DMA1_Channel1_IRQHandler

#define LED_GPIO                GPIOC
#define LED_1                   GPIO_Pin_13

#define IRQ_PRIO_GROUP_CFG      NVIC_PriorityGroup_2
#define ADCDMA_IRQ_PRIO         0
#define USB_IRQ_PRIO            1
#define CONSOLE_IRQ_PRIO        2

/*
 * Another periphery in use:
 *   - TIM2 and TIM3 in chained counter mode, used by timer.c.
 */

void init_peripherals(void);

#endif  /*__HW_CONFIG_H*/
