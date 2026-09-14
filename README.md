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
| `main/` | `app_main`, the console loop, and the `storage` command |
| `components/board_ws128/` | Pins (`include/board.h`), shared I2C bus, LCD, touch, IMU, and the `lcd`/`touch`/`imu` commands |
| `external/esp-console-kit/` | Submodule: `cmd_system`, `cmd_wifi`, `cmd_network`, `cmd_nvs`, `cmd_i2c` |
| `partitions.csv` | nvs, otadata, two 2.25 MB OTA slots, coredump, and an 11 MB LittleFS `storage` volume at `/data` |

## Console

Type `help` for the full list. The board-specific commands are:

| Command | Does |
|---|---|
| `lcd [cycle on\|off \| fill r\|g\|b\|w\|k\|<rgb565> \| bl <0-100>]` | Red/green/blue/white test cycle (on at boot), solid fills, backlight |
| `touch [on\|off\|status]` | CST816S touch reporting, off at boot; prints down/move/up with x,y |
| `imu [-r <hz>] [-n <count>] \| imu id` | Streams accelerometer (g), gyro (dps) and temperature until a key is pressed |
| `storage [df \| ls [path] \| bench [kb]]` | LittleFS usage, a listing, and a sequential write/read benchmark |

The shared components add the following:
- System: `version`, `free`, `heap`, `tasks`, `top`, `log_level`, `gpio`, and sleep.
- Wi-Fi: `wifi`, `wifi_save`, `wifi_forget`, `wifi_known`, `join`, `wifi_link`, `wifi_ps`, `wifi_txpower`.
- Network: `ip addr`, `ping`, `iperf`, `traceroute`, `dig`.
- NVS: `nvs_*`.
- I2C: `i2cdetect` and related commands.

`wifi_save <ssid> [pass]` remembers the network, and the board rejoins the last one at boot.

## Configuration (`idf.py menuconfig`)

- **Board: Waveshare ESP32-S3-Touch-LCD-1.28**
  - `BOARD_LCD_CYCLE_AT_BOOT`
  - `BOARD_TOUCH_ENABLE` compiles the touch driver in or out.
  - `BOARD_TOUCH_AUTOSTART`
- **Console WiFi commands (cmd_wifi)**
  - Saved-network NVS namespace
  - Reconnect retries and interval

`sdkconfig` isn't committed. Everything the build needs is in `sdkconfig.defaults`.
