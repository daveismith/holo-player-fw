# holo-player-fw

Firmware for the Waveshare ESP32-S3-Touch-LCD-1.28: ESP32-S3R2 with 2 MB of PSRAM,
16 MB flash, a GC9A01 240×240 round LCD, CST816S touch and a QMI8658 IMU. It
builds with ESP-IDF 6.1.

It plays Motion-JPEG clips and shows PNG and JPEG stills on the round panel, drives a
NeoPixel ring, and aims two holoprojector servos — all from a console on the board's
USB-C port.

## Documentation

The full documentation — wiring, the console reference, making and playing clips, showing
stills, servo calibration and configuration — is a site built from [`manual/`](manual/). Read it locally with:

```sh
make docs-setup     # once
make docs-serve
```

Once published it lives at <https://daveismith.github.io/holo-player-fw/>; see
[RELEASING.md](RELEASING.md) for enabling that.

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

Type `help` at the console for every command. Once the bootloader is on, later updates can go
over the console instead of USB — see [Updating over serial](manual/use/ota.md).

## Layout

| Path | What |
|---|---|
| `main/` | `app_main`, the console loop, and the holo's servos (`holo_servos.c`) |
| `components/board_ws128/` | Pins (`include/board.h`), shared I2C bus, LCD, touch, IMU, console colour parsing, and the `lcd`/`touch`/`imu` commands |
| `components/video/` | QuickTime parser, MJPEG player, and the `video`/`screen` commands |
| `components/leds/` | The NeoPixel strip on P2: Espressif's `led_strip`, the wipe and rainbow patterns, and the `leds` command |
| `external/esp-console-kit/` | Submodule: `cmd_system`, `cmd_wifi`, `cmd_network`, `cmd_nvs`, `cmd_i2c`, `cmd_fs` (+ `tools/fs_xfer.py`), `servo`, `holo` |
| `manual/` | The documentation site's pages (`mkdocs.yml`) |
| `tools/` | `check_command_docs.py` (every command is documented) and `check_version.py` (release tags) |
| `partitions.csv` | nvs, otadata, two 2.25 MB OTA slots, coredump, and an 11 MB LittleFS `storage` volume at `/data` |

## Versioning

There is no version constant in the source: ESP-IDF takes `PROJECT_VER` from `git describe`, so
a release tag is the version baked into the image and reported by `version` and `ota`. A build
from an untagged tree reports the commit it came from. See [RELEASING.md](RELEASING.md).
