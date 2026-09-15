# holo-player-fw

Firmware for the Waveshare ESP32-S3-Touch-LCD-1.28: ESP32-S3R2 with 2 MB of PSRAM,
16 MB flash, a GC9A01 240×240 round LCD, CST816S touch and a QMI8658 IMU. It
builds with ESP-IDF 6.1.

## Getting the source

The shared console commands live in a git submodule. Clone with:

```sh
git clone --recursive <url>
# or, in an existing clone:
git submodule update --init
```

## Build and flash

```sh
idf.py build
idf.py -p /dev/cu.wchusbserial5B910448341 flash monitor
```

The board's USB-C port goes through a CH343P USB-UART bridge to UART0 (115200). Its
DTR/RTS lines drive the auto-download circuit, so flashing needs no buttons. The
S3's native USB pins aren't connected on this board.

## Layout

| Path | What |
|---|---|
| `main/` | `app_main` and the console loop |
| `components/board_ws128/` | Pins (`include/board.h`), shared I2C bus, LCD, touch, IMU, console colour parsing, and the `lcd`/`touch`/`imu` commands |
| `components/video/` | QuickTime parser, MJPEG player, and the `video`/`screen` commands |
| `components/leds/` | The NeoPixel strip on P2: Espressif's `led_strip`, Flash_PNG's patterns, and the `leds` command |
| `external/esp-console-kit/` | Submodule: `cmd_system`, `cmd_wifi`, `cmd_network`, `cmd_nvs`, `cmd_i2c`, `cmd_fs` (+ `tools/fs_xfer.py`) |
| `partitions.csv` | nvs, otadata, two 2.25 MB OTA slots, coredump, and an 11 MB LittleFS `storage` volume at `/data` |

## Console

Type `help` for the full list. The board-specific commands are:

| Command | Does |
|---|---|
| `screen [colour <name\|#RRGGBB\|R,G,B\|0xRGB565> \| clear]` | Show a solid colour, or clear the screen; alone, what's showing. The screen is off (panel asleep, backlight off) whenever nothing is showing: at boot, after `clear`, and when a clip ends |
| `lcd [bl <0-100> \| bench [frames]]` | LCD hardware: power state, backlight level while on, fill-rate benchmark |
| `touch [on\|off\|status]` | CST816S touch reporting, off at boot; prints down/move/up with x,y |
| `imu [-r <hz>] [-n <count>] \| imu id` | Streams accelerometer (g), gyro (dps) and temperature until a key is pressed |
| `fs ls\|df\|stat\|mkdir\|rmdir\|rm\|mv\|cat\|hexdump\|sha256\|bench ...` | The LittleFS volume at `/data`. Paths are relative to it. |
| `fs put [-f] [-b baud] <path> [size]` / `fs get [-b baud] <path>` | XMODEM-1K upload and download over the console (use `fs_xfer.py`) |
| `video play <file> [loop] [frame] \| stop \| status \| info <file> \| verify <file> [step]` | Play a Motion-JPEG QuickTime clip centred on the panel, on the clip's own timing |
| `leds [colour <c> \| off \| wipe [<c>] [loop] \| rainbow [loop] \| bright <1-100>]` | The NeoPixel strip: a solid colour, off, brightness, or one of Flash_PNG's patterns (a colour wipe, the rainbow), played once and then off, or looped. Alone, what it's showing |

A colour for `screen colour` or `leds colour` is a name (`red`, `orange`, ...), `#RRGGBB`,
`R,G,B`, or `R G B`. `screen colour` also takes `0x` and a raw RGB565 value.

The shared components add the following:
- System: `version`, `free`, `heap`, `tasks`, `top`, `log_level`, `gpio`, and sleep.
- Wi-Fi: `wifi`, `wifi_save`, `wifi_forget`, `wifi_known`, `join`, `wifi_link`, `wifi_ps`, `wifi_txpower`.
- Network: `ip addr`, `ping`, `iperf`, `traceroute`, `dig`.
- NVS: `nvs_*`.
- I2C: `i2cdetect` and related commands.

`wifi_save <ssid> [pass]` remembers the network, and the board rejoins the last one at boot.

## Putting video files on the board

Close `idf.py monitor`, then:

```sh
python external/esp-console-kit/tools/fs_xfer.py -p /dev/cu.wchusbserial5B910448341 \
    put ~/Documents/Arduino/Flash_PNG/data/*.mov
```

Each file goes over XMODEM-1K at 460800 baud. The two Flash_PNG clips ran at 7–10 KB/s,
2.5–4.5 minutes each. The tool then checks that the SHA-256
matches the local file twice: for the bytes the board received, and for the file read
back from flash. Use `-f` to replace existing files, `get` to download, and `sha256`
to hash on the board.

Downloads run at 230400 baud. On macOS, WCH's CH34x driver loses data coming from
the board at higher rates; see the esp-console-kit README.

## Video

`components/video` has two parts:
- `quicktime.cpp`: the Flash_PNG sketch's QuickTime parser, ported.
- `video_player.cpp`: the player, which decodes with Espressif's `esp_new_jpeg`.

Each frame is read from `/data` and drawn centred on the panel, timed by the clip's own
time-to-sample table.

The panel is off whenever nothing is showing. To start a clip, the player wakes the panel
and fills it black while the display is still off. It then turns the display on, waits two
panel refreshes, and turns on the backlight. Only then does it draw frame 0 and start the
clip's clock, so the first frames are never drawn to a dark screen. When the clip ends or
`video stop` runs, the panel goes back to sleep with the backlight off. Starting another
clip or a `screen colour` over a playing clip keeps the panel on.

- **Streamed (the default).** `esp_new_jpeg`'s block mode decodes 16 lines at a time into
  one of two small DMA buffers. Each block is sent to the panel while the next one decodes,
  so decoding and the SPI transfer overlap. The whole frame is never held in memory: it
  would be 115 KB at 240×240, more than the largest free block of internal RAM.
- **Whole frame.** `video play <file> frame` decodes the whole frame first, then sends it.
  This mode is also used for clips whose width or height isn't a multiple of 8, which
  block mode can't handle.
- **Checking block mode.** `video verify <file>` decodes sample frames both ways and
  compares them byte for byte.
- **Status.** `video status` reports read, decode, and decode-plus-draw time per frame.
  It also reports the paint time, from the first pixels sent to the last: how long the
  panel spends mid-update, which is what tearing depends on.

Measured on the four test clips, at 30 fps (a 33.3 ms frame):

| Clip | Streamed: decode+draw | Streamed: paint | Whole frame: decode+draw | Whole frame: paint |
|---|---|---|---|---|
| 120×120 (Flash_PNG clips) | 4.6 ms | 4.1 ms | 4.9–5.2 ms | 3.2 ms |
| 240×240 | 14.2–14.6 ms, none late | 13.6 ms | 28.3–30.2 ms, some late | 17.8 ms (frame in PSRAM) |

Anything that writes to flash during playback, such as an NVS save, pauses both cores. That
can make an occasional frame late. The console's history file is written only when no clip is
playing. While one plays, the save waits until the screen turns off: when the clip ends by
itself, on `video stop`, or on `screen clear`. If a colour replaces the clip, the screen stays
on, so the save happens with the next command. The console task always does the write. When a
clip ends by itself, the player signals the console task, which saves while waiting for the
next keystroke. Anything already typed on the command line is left alone.

To make a clip, use the Flash_PNG recipe. Export at 120×120 (anything up to 240×240
fits the panel), then run:

```sh
ffmpeg -i <input> -c:v mjpeg -q:v 9 -an <output>.mjpeg.mov
```

Upload it with `fs_xfer.py put` and play it with `video play <output>.mjpeg.mov`.

I measured `esp_new_jpeg` against JPEGDEC 1.5.0 and 1.8.4 and TJpgDec before choosing
it; the results are in the commit message of `3a7182e`. On a 120×120 clip frame it
decoded in 1.79 ms, against 2.63 ms for JPEGDEC 1.8.4. JPEGDEC 1.5.0 also leaves the
last 8 rows of a 120-row frame unwritten.

## NeoPixel strip

Flash_PNG drove 16 NeoPixels from GPIO16: `NEO_GRB + NEO_KHZ400`, which is GRB order at 400 kHz.
`components/leds` does the same by default, using Espressif's `led_strip` on the RMT. Its
`LED_MODEL_WS2811` timing (0.5/1.2 µs high, 2.5 µs a bit) is Adafruit's 400 kHz timing. The
strip is cleared at boot. The sketch's two patterns are ported but play only when triggered:
- `leds wipe [<c>]` lights each LED in turn, 250 ms apart. The default colour is white.
- `leds rainbow` is Adafruit's rainbow: five times round the colour wheel in 12.8 s,
  gamma-corrected.

Each holds for a second, then turns the strip off. With `loop`, it repeats until `leds off`.

### Wiring

P2 is the 12-pin, 1.0 mm-pitch SH connector on the back of the board. The strip needs three
wires:

| Strip | P2 pin | Signal |
|---|---|---|
| DIN (data in, the arrow's tail) | 8 | GPIO16, through a 300–500 Ω resistor at the strip end |
| GND | 1 or 5 | GND. Always connect it, even when the strip has its own supply |
| +5 V | 2 | VSYS, or a separate 5 V supply (see below) |

The full header: 1 GND, 2 VSYS, 3 RUN (reset), 4 BOOT (GPIO0), 5 GND, 6 3V3, 7 GPIO15,
8 GPIO16, 9 GPIO17, 10 GPIO18, 11 GPIO21, 12 GPIO33.

- **Power.** With USB plugged in, VSYS is USB 5 V through a Schottky diode (D1), so about
  4.6–4.7 V. On battery, it's the cell (3.0–4.2 V) through a P-FET (Q2).
  - Each LED draws up to about 60 mA at full white, so the 16 LEDs can want 1 A. That's
    too much for one SH1.0 contact and its thin wire, and for a USB port that also powers
    the board.
  - From VSYS, keep `leds bright` at 30 or below (about 300 mA). For full brightness, feed
    the strip's +5 V from the supply directly and join the grounds.
  - Put a 470–1000 µF capacitor across the strip's +5 V and GND, at the strip.
  - A 12 V WS2811 strip needs its own 12 V supply. Share only GND and DIN with the board.
- **Data level.** GPIO16 drives 3.3 V. A WS281x wants 0.7 × its supply as a logic high:
  3.3 V from VSYS on USB, and 3.5 V from a true 5 V supply. That's marginal, though short
  wires usually work. If the first LED flickers or shows the wrong colour, add a 74AHCT1G125
  buffer powered from the strip's +5 V, between GPIO16 and DIN.
- **Speed.** 400 kHz matches Flash_PNG. WS2812B and SK6812 NeoPixels, such as Adafruit's
  rings, are 800 kHz parts. Most also accept the 400 kHz timing, but if yours misbehaves,
  switch to 800 kHz in menuconfig.

GPIO16 is the S3's XTAL_32K_N pin. This board has no 32 kHz crystal, so the pin is a plain
GPIO. The `gpio` command refuses to drive it.

## Configuration (`idf.py menuconfig`)

- **Board: Waveshare ESP32-S3-Touch-LCD-1.28**
  - `BOARD_TOUCH_ENABLE` compiles the touch driver in or out.
  - `BOARD_TOUCH_AUTOSTART`
- **LED strip (NeoPixel)**
  - `LEDS_GPIO` (16) and `LEDS_COUNT` (16)
  - Data rate: 400 kHz, as Flash_PNG, or 800 kHz
  - Colour order: GRB, as Flash_PNG, or RGB
  - `LEDS_BRIGHTNESS` at boot (100%)
- **Console WiFi commands (cmd_wifi)**
  - Saved-network NVS namespace
  - Reconnect retries and interval

`sdkconfig` isn't committed. Everything the build needs is in `sdkconfig.defaults`.
