/*
 * Here you can customize firmware.
 */

/***********************************
 * Verbose name of hardware platform, only used for console output
 * when booting up.
 */
#define PLATFORM_NAME               "bluepill stm32f103c8t6 (cortex-m3)"

/***********************************
 * Console settings.
 * Console subsystem has two buffers: for transmission of log messages
 * and for reception of commands.
 * Both transmission and reception are done in USART interrupt handler,
 * buffers are required. Transmission buffer should be big enough to
 * contain block of consecutive debug lines or command output,
 * and reception buffer can be much smaller (no bigger than longest
 * command).
 * When some log record requested and transmission buffer has less
 * than CONSOLE_MAX_MSG_LEN bytes free, then record is skipped.
 * Level of logging can be changed at runtime via console commands,
 * CONSOLE_MIN_LEVEL refers to default level at startup.
 */
#define CONSOLE_BAUDRATE            460800
#define CONSOLE_TX_BUFSIZE          512
#define CONSOLE_RX_BUFSIZE          64
#define CONSOLE_MAX_MSG_LEN         160
#define CONSOLE_ENABLE_ECHO         1
#define CONSOLE_MIN_LEVEL           CONSOLE_LVL_INFO

/***********************************
 * If LED_INVERT is set to 1 then `led_on()` pushs gpio pin low and
 * `led_off()` pulls it high.
 */
#define LED_INVERT                  1

/***********************************
 * Period of LED blinks when device in different modes,
 * in microseconds.
 */
#define BLINK_MODE_NONE             (2000 * 1000)
#define BLINK_MODE_HIRES            (1000 * 1000)
#define BLINK_MODE_MIDRES           ( 500 * 1000)
#define BLINK_MODE_LORES            ( 100 * 1000)

/***********************************
 * Period between reports with generic information about device state,
 * in miscoseconds.
 * Generic information is: current mode, total number of bytes
 * transmitted and received (both SPI and UART).
 * New report is generated when byte totals changed, but
 * respecting these limits.
 */ 
#define MIN_REPORT_PERIOD           (1 * 1000000)
#define MAX_REPORT_PERIOD           (1 * 10000000)

/***********************************
 * Device USB idVendor and idProduct.
 * ...
 */
#define ADC_VID                     0x1A87
#define ADC_PID                     0x5513

/***********************************
 * Miscellaneous settings of adc specific behaviour.
 * 
 * ADC_MAX_PACKET_SIZE and ADC_SAMPLE_SIZE
 *   are USB-specific sizes (maximum transfer size and endpoint data
 *   sizes).
 * ADC_SAMPLES_COUNT is amount of USB packets that can be buffered,
 *   it is important for high frequencies when ADC(s) is(are) faster
 *   than USB; in this case buffer will be eventually exhausted and
 *   acquisition will be downsampled to the speed of USB transfer.
 */
#define ADC_MAX_PACKET_SIZE         64
#define ADC_SAMPLE_SIZE             60
#define ADC_SAMPLES_COUNT           272

/***********************************
 * Default device configuration after startup.
 * 
 * Note that frequency means total samples per second for all channels.
 */
#define ADC_DEFAULT_COMMAND         ADC_CMD_CONTINUOUS
#define ADC_DEFAULT_BITS            ADC_BITS_HI
#define ADC_DEFAULT_FREQUENCY       ADC_FREQUENCY_200KHZ
#define ADC_DEFAULT_CHANNELS        ((1 << 2) - 1)
#define ADC_DEFAULT_SAMPLES         0
