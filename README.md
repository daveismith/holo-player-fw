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
| `components/board_ws128/` | Pins (`include/board.h`), shared I2C bus, LCD, touch, IMU, and the `lcd`/`touch`/`imu` commands |
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
playing. While one plays, the save waits until it stops. `video stop` and `screen` save at
once. If a clip ends by itself, the save happens with the next command.

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

## Configuration (`idf.py menuconfig`)

- **Board: Waveshare ESP32-S3-Touch-LCD-1.28**
  - `BOARD_TOUCH_ENABLE` compiles the touch driver in or out.
  - `BOARD_TOUCH_AUTOSTART`
- **Console WiFi commands (cmd_wifi)**
  - Saved-network NVS namespace
  - Reconnect retries and interval

`sdkconfig` isn't committed. Everything the build needs is in `sdkconfig.defaults`.
