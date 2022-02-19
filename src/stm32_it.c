#include "stm32_it.h"
#include "console.h"
#include "usbd.h"
#include "adc.h"

void NMI_Handler(void) {
}

void HardFault_Handler(void) {
    while (1) {
    }
}

void MemManage_Handler(void) {
    while (1) {
    }
}

void BusFault_Handler(void) {
    while (1) {
    }
}

void UsageFault_Handler(void) {
    while (1) {
    }
}

void SVC_Handler(void) {
}

void DebugMon_Handler(void) {
}

void PendSV_Handler(void) {
}

void SysTick_Handler(void) {
}

void USB_IRQ_HANDLER(void) {
    usbd_istr();
}

void CONSOLE_IRQ_HANDLER(void) {
    console_irq();
}

void ADCDMA_IRQ_HANDLER(void) {
    adcdma_irq();
}
