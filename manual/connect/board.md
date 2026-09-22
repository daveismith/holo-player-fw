# The board

The **Waveshare ESP32-S3-Touch-LCD-1.28 (Rev3)** is an ESP32-S3R2 with 2 MB of quad PSRAM in the
package and 16 MB of flash, carrying a round display, a touch controller, an IMU and a battery
charger on a 1.28-inch disc.

Pin assignments below come from the Rev3 schematic netlist; Waveshare's own documentation, TFT_eSPI
Setup302 and the NuttX `esp32s3-ws-lcd128` board agree with them. They are defined in
`components/board_ws128/include/board.h`, which is the authority if the two ever disagree.

## What is on the board

| Part | Detail |
|---|---|
| Display | GC9A01A, 240×240 round IPS, 4-wire SPI on SPI2 |
| Touch | CST816S, I2C address `0x15` |
| IMU | QMI8658A, I2C address `0x6B` (or `0x6A`) |
| Battery | Sensed on ADC1 channel 0 through a 200 kΩ/100 kΩ divider, so V<sub>bat</sub> = 3 × V<sub>adc</sub> |
| USB | USB-C through a CH343P bridge to UART0 |

## Display

| Signal | GPIO |
|---|---|
| SCLK | 10 |
| MOSI | 11 |
| MISO | 12 (routed to the panel, unused) |
| CS | 9 |
| DC | 8 |
| RST | 14 |
| Backlight | 2 |

The backlight pin drives an N-FET and is active high with a pull-down, so the backlight is off at
reset and stays off until the firmware turns it on.

The visible area is circular, and its centre falls between pixels 119 and 120 on both axes.
`screen calibration` draws a crosshair on that centre for aligning the panel in a dome — see
[Mounting the screen](screen.md).

GPIO10 is not SPI2's IOMUX clock pin, so the signals go through the GPIO matrix. On the S3 that
limits reads rather than writes, and the panel is only ever written to, so it costs nothing here.

## I2C bus

One bus is shared by the touch controller and the IMU.

| Signal | GPIO |
|---|---|
| SDA | 6 |
| SCL | 7 |

It runs at 400 kHz. **There are no pull-up resistors on the board**, which matters if you hang
anything else off the bus. `i2cdetect` will show what is answering.

The touch controller is an awkward bus citizen: a CST816S only answers on I2C *while the screen is
being touched*. That is why reading its ID at startup is disabled
(`CONFIG_ESP_LCD_TOUCH_CST816S_DISABLE_READ_ID`), and why `i2cdetect` will usually not show
`0x15` unless you are touching the glass as it runs.

| Device | Address | INT | RST |
|---|---|---|---|
| CST816S touch | `0x15` | 5 | 13 |
| QMI8658A IMU | `0x6B`, or `0x6A` | 4 (INT1), 3 (INT2) | — |

The IMU's SA0 pin is tied low, but Waveshare's own headers disagree about which address that
produces, so the firmware probes both. `imu id` reports which one answered.

## P2 expansion header

P2 is the 12-pin, 1.0 mm-pitch SH connector on the back of the board, and the only place to
connect anything.

| Pin | Signal | Pin | Signal |
|---|---|---|---|
| 1 | GND | 7 | GPIO15 |
| 2 | VSYS | 8 | GPIO16 |
| 3 | RUN (reset) | 9 | GPIO17 |
| 4 | BOOT (GPIO0) | 10 | GPIO18 |
| 5 | GND | 11 | GPIO21 |
| 6 | 3V3 | 12 | GPIO33 |

Six GPIOs are free for your own use: **15, 16, 17, 18, 21 and 33**. This firmware already claims
three of them — GPIO16 for the [LED ring](leds.md), GPIO17 and GPIO18 for the
[servos](servos.md).

**VSYS is not a regulated 5 V rail.** With USB plugged in it is USB 5 V through a Schottky diode
(D1), so about 4.6–4.7 V. On battery it is the cell itself, 3.0–4.2 V, through a P-FET (Q2). Both
the LED and servo pages explain when that matters and when to bring your own supply.

## Pins you cannot drive

The `gpio` command refuses to drive the pins the board owns — the display, the I2C bus, the
touch and IMU interrupts, the UART, and the flash and PSRAM lines. Each has a reason recorded
alongside it in `main/main.c`. `gpio set` on one of them reports why instead of doing it.

## USB and the serial port

USB-C goes only to a **CH343P** UART bridge on UART0 (GPIO43/44). The ESP32-S3's native USB pins,
GPIO19 and GPIO20, are not connected.

Two consequences run through the rest of these pages:

- The bridge's DTR/RTS lines drive the auto-download circuit, so `idf.py flash` needs no buttons.
- Everything else — the console, uploading clips, updating the firmware — is a serial port, and
  **only one program can hold it at a time**. Close `idf.py monitor` before running any host-side
  tool.
