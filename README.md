10-channel ADC with USB interface based on Bluepill board
=========================================================

What is this
------------

Firmware that provides ADC capabilities of stm32f1xx MCUs via USB
interface. It supposed to be run on so-called [bluepill] widely
available board that has [stm32f10x] on it. This project based on
[bluepill_ch341a] as a template.

USB protocol is made as simple as possible with just two endpoints,
one for configuring (EP0) and one for analog data acquisition (EP1,
bulk type).
Each usb packet contains minimal header that helps to parse its
contents without knowing full device configuration, i.e. one
could just plug the device and start grab data from EP1.


Features
--------

  - up to 10 channels;
  - selectable resolution (12/8/4/2 bit per sample);
  - selectable sample rate (up to ~1.7 MHz);
  - singleshot/continuous mode;
  - triggers (rising edge, falling edge, strobe duration);
  - UART console for diagnostics;
  - hardware simultaneity for even/odd channel pairs
    (1 and 2, 3 and 4 and so on).


Performance
-----------

To achieve best possible performance, both ADCs are used for
acquisition is possible. Low (2/4/8) bit resolutions are implemented for
reduction of throughtput requirements and prevention of packets loss.

In terms of stm32f10x reference manual, modes of ADCs are selected as
follows:

  - for single channel and maximum frequency: ADC1+ADC2 in 
    *Fast interleaved mode*;
  - for single channel and less than maximum frequency: ADC1 only;
  - for more than one channel: ADC1+ADC2 in *Dual mode*.

Unfortunately, it is not possible to achieve 1 megasample per second
per ADC acquisition rate (theretical maximum for stm32f1xx family with
1us minimum conversion time), because it requires 14 MHz ADC clock,
which is not a divisor of 48/72 MHz strictly needed for working USB and
hence is not achievable.
The only workaround I can imagine is changing clocking setup (divisors)
at runtime so in any given moment either ADC(s) work(s) and store(s)
data, or USB works and transfers data, then they swap and so on. But this
way is much harder to implement and it leads to that the device will
be permanently connected/disconnected to/from USB host.
In this project 72 MHz SYSCLK used that gives 12 MHz ADC maximum,
hence 1.17 us minimum sample+conversion time and 857143 samples per
second per each of two ADCs.

USB-FS has theoretical throughput 12 Mbit/sec half-duplex, with
approximately 20-40% overhead for protocol-specific data (headers,
trailers, acknowledgements and so on). Practically the maximum possible
average bandwitdth for transferring data from slave to master is 
approximately 5-6 MBit/sec. This can be checked with simple firmware
that just writes constant data to EP1 whenever it was read by master,
and simple PC script that reads all data from EP1.
In this project speed of USB transfers is near that maximum, usually
~5.5 MBit/sec (when ADCs work on high frequencies).

So there are two basic limits for samples per second rate: ADC speed
and USB speed. Due to usage of internal buffer for ADC data (which
consumes most of MCU's SRAM: ~18 kB out of 24 kB) there is some period
of time after start/trigger when buffer is not full and no data loss
occurs. If the requested amount of samples (see `SAMPLES` parameter
below) is less than size of buffer (plus size of data sent via USB
while buffer is filling by ADC), then ADC speed is the only limit.
Otherwise when buffer is full, ADC 'slows down' to the speed of USB.
Speed of USB (in samples per second) is near speed of ADC only for
lowest possible 2-bits resolution.

Examples of achievable sampling rates (all data for parameter 
`FREQUENCY = 1`, see below):

Number of channels | Bits per sample | Max rate - ADC  | Min rate - USB
-------------------|-----------------|-----------------|---------------
1                  |       12        |   1714 kS/s     |    406 kS/s
1                  |        8        |   1714 kS/s     |    700 kS/s
1                  |        4        |   1714 kS/s     |   1300 kS/s
1                  |        2        |   1714 kS/s     |   1714 kS/s
2                  |       12        |    857 kS/s     |    211 kS/s
2                  |        8        |    857 kS/s     |    326 kS/s
2                  |        4        |    857 kS/s     |    644 kS/s
2                  |        2        |    857 kS/s     |    857 kS/s
4                  |       12        |    428 kS/s     |    106 kS/s
4                  |        8        |    428 kS/s     |    165 kS/s
4                  |        4        |    428 kS/s     |    330 kS/s
4                  |        2        |    428 kS/s     |    428 kS/s
10                 |       12        |    171 kS/s     |     44 kS/s
10                 |        8        |    171 kS/s     |     68 kS/s
10                 |        4        |    171 kS/s     |    140 kS/s
10                 |        2        |    171 kS/s     |    171 kS/s


Configuring, building, flashing
-------------------------------

Same as described in source template project [bluepill_ch341a].


Pinout
------

PIN | Subsystem  | Function
----|------------|---------
C13 | LED        | Signals about current mode by blink speed
A11 | USB        | Data Minus
A12 | USB        | Data Plus
A0  | ADC.CH1    | Analog-to-digital converter, Channel 1
A1  | ADC.CH2    | Analog-to-digital converter, Channel 2
A2  | ADC.CH3    | Analog-to-digital converter, Channel 3
A3  | ADC.CH4    | Analog-to-digital converter, Channel 4
A4  | ADC.CH5    | Analog-to-digital converter, Channel 5
A5  | ADC.CH6    | Analog-to-digital converter, Channel 6
A6  | ADC.CH7    | Analog-to-digital converter, Channel 7
A7  | ADC.CH8    | Analog-to-digital converter, Channel 8
B0  | ADC.CH9    | Analog-to-digital converter, Channel 9
B1  | ADC.CH10   | Analog-to-digital converter, Channel 10
B10 | CONSOLE    | Transmitter of misc messages (TX, this is *output* of MCU)
B11 | CONSOLE    | Receiver of commads (RX, this is *input* of MCU)


Protocol: configuring
---------------------

There is a bunch of 8-bit *registers* available for writing by nodata
setup packets:
```
bmRequestType = 0x40
bRequest = 1
wIndex = <index_of_register_to_be_changed>
wValue = <byte_value_to_be_set>
```
and reading by data setup packets:
```
bmRequestType = 0x80|0x40
bRequest = 1
wIndex = <index_of_register_to_be_read>
wLength = 1
```
(corresponds to libusb's `libusb_control_transfer()` and pyusb's
`libusb.Device.ctrl_transfer()`)

There are 1-, 2- and 4-byte parameters. 2- and 4-bytes parameters
occupy 2 and 4 registers with consecutive indicies, low byte goes with
lower index.

At the moment parameters are as follows:

Parameter   | Number of bytes | Index of low byte
------------|-----------------|------------------
CMD         | 1               | 1
CHANNELS    | 2               | 2
BITS        | 1               | 4
FREQUENCY   | 1               | 5
OFFSET      | 2               | 6
GAIN        | 1               | 8
SAMPLES     | 1               | 9
TRIGGER     | 1               | 10
TRIG_CHANNEL| 1               | 11
TRIG_LEVEL  | 2               | 12
TRIG_OFFSET | 4               | 14
TRIG_T_MIN  | 4               | 18
TRIG_T_MAX  | 4               | 22
USE_CHANNELS| 2               | 26


Parameter `CMD` describes current acquisition behaviour:

`CMD` value | Mnemonic  | Meaning
------------|-----------|---------------
0           | STOP      | No acquisition 
1           | ONCE      | Single acquisition of `SAMPLES` samples after start/trigger
2           | CONTINUOUS| Automatically restart or wait trigger after `SAMPLES` samples

Parameter `CHANNELS` is simple bitmask of 10 possible
channels to be grabbed by ADC(s). Low bit is for channel 1,
and so on. High 4 bits are ignored.
Note that the only correct situation when odd channels (bits) are
selected is when there is exactly one channel (bit) selected,
because ADC1&ADC2 must work simultaineously. If you try to set
3/5/7/9 bits, device will select one another unselected channel so
there will be even number of channels.
The real set of channels that will be sent via USB can be read
from `USE_CHANNELS` parameter or from header in each data packet
received.

Parameter `BITS` is a number of bits per sample per channel, acceptable
values are 2, 4, 8 and 12. This resolution is only applied for samples
transferred via USB. Both ADCs still has fixed 12-bit resolution.
For parsing different resolution formats in USB packets see next
section.

Parameter `FREQUENCY` describes acquisition speed (total samples per
second by each ADC, *not* the sample rate for each separate channel):

`FREQUENCY` value | Frequency of each ADC | Rate if 1 chan selected | Rate if `N > 1`chans selected
------------------|---------------|----------------|-----------------
0                 | 0             | 0              | 0
1                 | 857143        | 1714286        | `857143*2/N`
2                 | 500000        | 500000         | `500000*2/N`
3                 | 200000        | 200000         | `200000*2/N`
4                 | 100000        | 100000         | `100000*2/N`
5                 | 50000         | 50000          | `50000*2/N`
6                 | 20000         | 20000          | `20000*2/N`
7                 | 10000         | 10000          | `10000*2/N`
8                 | 5000          | 5000           | `5000*2/N`
9                 | 2000          | 2000           | `2000*2/N`
10                | 1000          | 1000           | `1000*2/N`

Parameters `OFFSET` and `GAIN` describe processing of raw values from
ADC(s). The formula is:
    `<output> = (<ADC> - <OFFSET>) * 2^<GAIN>`
It is only useful when `BITS` value is less than 12 because in
these cases packing scheme uses only highest bits (see next section).
`OFFSET` should fit in 12 bits, and `GAIN` should be less than 12.

Parameter `SAMPLES` describes how many samples will be grabbed
after start/trigger. Number of samples is counted as:
    `<number_of_samples> = (1 << (SAMPLES + 10)) = 1024 * 2^SAMPLES`

Parameter `TRIGGER` describes when data acquisition and transfer starts.

`TRIGGER` value | Mnemonic  | When acquisition starts
----------------|-----------|------------------------
0               | NONE      | As soon as possible
1               | RISING    | On rising edge (level goes from low to high)
2               | FALLING   | On falling edge (level goes from high to low)
3               | THRESHOLD | On rising or falling edge
4               | STROBE_LO | On rising edge if there was a falling edge before it within time limits
5               | STROBE_HI | On falling edge if there was a rising edge before it within time limits

Parameter `TRIG_CHANNEL` describes number of channel to be
used in `TRIGGER` logic. This is 0-based index, i.e. zero value
means channel #1 selected for triggering.

Parameter `TRIG_LEVEL` describes level for edge detections.
It is 12-bit value and is not dependant on `BITS` parameter,
since trigger logic is working with raw ADC values, not samples
that will be sent via USB.

Parameter `TRIG_OFFSET` is one's complement signed 32-bit integer.

  - if it is less than zero, then it describes number of samples per
    channel before `TRIGGER` event that should be sent via USB. Note
    that this value is limited by the size of internal buffer,
    especially on high frequencies;
  - if it is greater than zero, then it describes number of samples per
    channel to be skipped (dropped) after `TRIGGER` event and before
    acquisition starts. This is not limited by internal buffer.


Parameters `TRIG_T_MIN` and `TRIG_T_MAX` describe limits on strobe
duration for `TRIGGER` types `STROBE_LO` and `STROBE_HI`. They are in
samples per channel between two edges of strobe. Zero value for
`TRIG_T_MAX` has special meaning that there is no upper limit, only
lower one.


Protocol: data stream
---------------------

Each 64-bytes packet read from EP1 consists of 4-bytes header and
60-bytes body.
Header describes the contents of body.

Header format:

  - byte 0, bit 7: trigger flag, it is set in the first packet after
    trigger event occures;
  - byte 0, bits 6..0: sequence number, it is increased by 1 on
    each data acquisition from ADC(s); it can be used to count lost
    packets either because host receiver is slow or because USB
    interface is slower than ADC(s);
  - bytes 1 and 2 (LE 16-bit): bitmask of channels that was written to the
    packet, see `CHANNEL` parameter in previous section;
  - byte 3, bits 7..4: acquisition frequency code (see `FREQUENCY`
    parameter);
  - byte 3, bits 3..0: sample resolution (see `BITS` parameter).

60 bytes of body contains samples (digitized voltage levels on channels).
Samples are going in round-robin order, starting from the
lowest-numbered channel. Examples:

  - `CHANNEL = 0b11`, selected channels are `[1, 2]`, order of
    samples in packet: `CH1:CH2:CH1:CH2:CH1:CH2:CH1:CH2:...`;
  - `CHANNEL = 0b1100110`, selected channels are `[2, 3, 6, 7]`, order of
    samples in packet: `CH2:CH3:CH6:CH7:CH2:CH3:CH6:CH7:CH2:...`;
  - `CHANNEL = 0b001000`, selectd channels are `[4]`, order of samples
    in packet: `CH4:CH4:CH4:CH4:...`.

2-bit format packs 4 samples in 1 byte, the first sample occupies 7 and
6 bits, second sample 5 and 4 bits, third sample 3 and 2 bits,
last fourth sample 1 and 0 bits.

4-bit format packs 2 samples in 1 byte, the first sample is in high
4 bits (7, 6, 5 and 4), the second sample is in low 4 bits
(3, 2, 1 and 0).

8-bit format packs 1 sample in 1 byte, using high 8 bits.

12-bit format packs 2 samples in 3 bytes:

  - byte 0: high 8 bits of first sample;
  - byte 1: low 4 bits of first sample in high 4 bits of byte,
    low 4 bits of second sample in low 4 bits of byte;
  - byte 2: high 8 bits of second sample.


PC software
-----------

The very simple python (2/3) script is included in sources, check
available options by running:

```
$ python3 python/plot_adc.py --help
```

Also there is lightweight crossplatform Qt/libusb-based GUI in
`gui` subdirectory. It can be built with something like
```
$ mkdir gui-build
$ cd gui-build
$ qmake ../gui/bluepill_adc_gui.pro
$ make
```
Should be compilable with Qt 4.8 or 5.x, libusb version 1.0.

Statically-linked binaries for windows and linux are
provided in Releases.

    - note for win: not tested yet, probably WinUSB/libusbK
      driver is required;
    - note for linux: appropriate permissions for USB device
      are required; simple script for udev is in
      srcipts/make_udev_rules.sh (or just `make udev_rules`).

[GUI screenshot example](gui/bluepill_adc_gui.png)

Issues and notes
----------------

For unambiguity and parsing simplicity, the number of samples per
channel in each USB packet must be the same. Default packet length
(64 byte) provides 60 bytes for samples, hence `60*8 = 480` bits must
be a multiple of `<bits_per_sample>*<number_of_channels>`.
For available combinations of resolutions and channels there are two
*problematic* cases:

  - `<bits_per_sample> = 12` and `<number_of_channels> = 6`;
    480 / (6*12) = 6.6666... (samples per channel per packet);
  - `<bits_per_sample> = 8` and `<number_of_channels> = 8`;
    480 / (8*8) = 7.5 (samples per channel per packet).

Since odd number of channels can't be set (except in case with single
channel, see above), in these cases another 2 channels will be selected,
sacrificing performance:

  - 480 / ((6+2)*12) = 5 (samples per channel per packet);
  - 480 / ((8+2)*8) = 6 (samples per channel per packet).



[bluepill]: https://stm32-base.org/boards/STM32F103C8T6-Blue-Pill.html
[stm32f10x]: https://www.st.com/resource/en/reference_manual/cd00171190-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf
[bluepill_ch341a]: https://www.github.com/konelav/bluepill_ch341a
