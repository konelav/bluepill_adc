#include "config.h"
#include "stm32f10x_conf.h"
#include "fwinfo.h"
#include "hw_config.h"
#include "usb_lib.h"

#include "timer.h"
#include "led.h"
#include "console.h"
#include "usbd.h"
#include "adc.h"

extern void Reset_Handler(void);

static int strcmp(const char *str1, const char *str2) {
    while (*str1 && *str1 == *str2) {
        str1++;
        str2++;
    }
    return (*str1 - *str2);
}

static void output_help(void) {
    console_flush();
    
    console_putstr("Commands available: \r\n");
    console_putstr("  reset - restart firmware execution\r\n");
    console_putstr("  help  - show this message\r\n");
    console_flush();
    console_putstr("  output:silent  - disable all messages\r\n");
    console_putstr("  output:normal  - enable most important messages\r\n");
    console_putstr("  output:streams - enable all except debugging messages\r\n");
    console_putstr("  output:verbose - enable all messages\r\n");
    console_flush();
}

static void reset(void) {
    NVIC_DisableIRQ(ADC1_2_IRQn);
    NVIC_DisableIRQ(USB_IRQ);
    
    console_flush();
    MSG_STR("Rebooting device...");
    console_flush();
    
    NVIC_DisableIRQ(CONSOLE_IRQ);
    Reset_Handler();
}

int main() {
    char command[CONSOLE_RX_BUFSIZE];
    uint32_t last_report_totals, totals;
    uint32_t last_report_t, since_last_report;
    uint32_t now;
    
    init_peripherals();

    timer_init();
    led_init();
    console_init(CONSOLE_ENABLE_ECHO, CONSOLE_MIN_LEVEL);

    console_putstr("\r\n\r\n");
    MSG_STR("Platform: " PLATFORM_NAME);
    MSG_STR("Firmware version: " FW_VERSION);
    MSG_STR("SVC abbrev commit " FW_GIT_COMMIT_STR);
    MSG_STR("Build " FW_BUILD_TIMESTAMP);
    MSG_STR("----------------------------");
    MSG_VAL("SystemCoreClock = ", SystemCoreClock, 10, " Hz");
    MSG_STR("Starting");
    console_flush();

    USB_Init();
    
    led_set_period(BLINK_MODE_NONE);

    last_report_totals = 0;
    last_report_t = 0;
    for (;;) {
        now = timer_usec();
        
        led_blink();
        
        totals = adc_tx_total + adc_rx_total;
        since_last_report = now - last_report_t;
        if ( since_last_report > MAX_REPORT_PERIOD || 
            (since_last_report > MIN_REPORT_PERIOD && totals != last_report_totals)) {
            INF() {
                console_putstr("ADC tx: ");
                console_putnum(adc_tx_total, 10, 0);
                console_putstr("; rx: ");
                console_putnum(adc_rx_total, 10, 0);
                console_putstr("\r\n");
            }
            INF() {
                console_putstr("triggered: ");
                console_putstr(is_triggered ? "YES" : "no");
                console_putstr("; usb_tx: ");
                console_putstr(usb_tx_in_progress ? "ON" : "off");
                console_putstr("\r\n");
            }
            last_report_t = now;
            last_report_totals = totals;
        }
        
        if (console_readline(command, sizeof(command))) {
            MSG() {
                console_putstr("\r\n");
                console_putstr("received command <");
                console_putstr(command);
                console_putstr(">\r\n");
            }
            if (!strcmp(command, "reset"))
                reset();
            else if (!strcmp(command, "help") || !strcmp(command, "?"))
                output_help();
            else if (!strcmp(command, "output:silent"))
                console_init(CONSOLE_ENABLE_ECHO, CONSOLE_LVL_ERR+1);
            else if (!strcmp(command, "output:normal"))
                console_init(CONSOLE_ENABLE_ECHO, CONSOLE_LVL_INFO);
            else if (!strcmp(command, "output:streams"))
                console_init(CONSOLE_ENABLE_ECHO, CONSOLE_LVL_STREAMS);
            else if (!strcmp(command, "output:verbose"))
                console_init(CONSOLE_ENABLE_ECHO, CONSOLE_LVL_DEBUG);
        }
    }
    return 0;
}
