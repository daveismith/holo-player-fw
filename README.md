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
| `lcd [cycle on\|off \| fill r\|g\|b\|w\|k\|<rgb565> \| bl <0-100>]` | Red/green/blue/white test cycle (on at boot), solid fills, backlight |
| `touch [on\|off\|status]` | CST816S touch reporting, off at boot; prints down/move/up with x,y |
| `imu [-r <hz>] [-n <count>] \| imu id` | Streams accelerometer (g), gyro (dps) and temperature until a key is pressed |
| `fs ls\|df\|stat\|mkdir\|rmdir\|rm\|mv\|cat\|hexdump\|sha256\|bench ...` | The LittleFS volume at `/data`. Paths are relative to it. |
| `fs put [-f] [-b baud] <path> [size]` / `fs get [-b baud] <path>` | XMODEM-1K upload and download over the console (use `fs_xfer.py`) |

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

## Configuration (`idf.py menuconfig`)

- **Board: Waveshare ESP32-S3-Touch-LCD-1.28**
  - `BOARD_LCD_CYCLE_AT_BOOT`
  - `BOARD_TOUCH_ENABLE` compiles the touch driver in or out.
  - `BOARD_TOUCH_AUTOSTART`
- **Console WiFi commands (cmd_wifi)**
  - Saved-network NVS namespace
  - Reconnect retries and interval

`sdkconfig` isn't committed. Everything the build needs is in `sdkconfig.defaults`.
