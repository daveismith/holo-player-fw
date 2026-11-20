# holo-player-fw

Firmware for the Waveshare ESP32-S3-Touch-LCD-1.28: ESP32-S3R2 with 2 MB of PSRAM,
16 MB flash, a GC9A01 240×240 round LCD, CST816S touch and a QMI8658 IMU. It
builds with ESP-IDF 6.1.

It plays Motion-JPEG clips and shows PNG, JPEG and GIF images — animated GIFs included —
on the round panel, drives a NeoPixel ring, and aims two holoprojector servos, all from a
console on the board's USB-C port.

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

The shared console commands, the GIF decoder and the Cyphal/CAN transport (libcanard) live in
git submodules, which the firmware needs to build. A fourth, Cyphal's public data types
(`external/public_regulated_data_types`), is needed only to regenerate the node's C headers
(`tools/gen_dsdl.py`) and to run its hardware tests (`tests/hardware/`). Clone with:

```sh
git clone --recursive <url>
# or, in an existing clone:
git submodule update --init
```

## Install

To install a release, no toolchain is needed: the Install page of the documentation flashes it
from Chrome, Edge or Opera, and each Release also carries the images for esptool. See
[Install](manual/install/flashing.md).

Once it is installed, the same browsers can put clips on the board and run every console command
from the documentation: [The board in the browser](manual/use/board.md).

## Build and flash

```sh
idf.py build
idf.py -p /dev/cu.wchusbserial5B910448341 flash monitor
```

The board's USB-C port goes through a CH343P USB-UART bridge to UART0 (115200). Its
DTR/RTS lines drive the auto-download circuit, so flashing needs no buttons. The
S3's native USB pins aren't connected on this board.

Type `help` at the console for every command. Once the bootloader is on, later updates can go
over Wi-Fi or the console instead of USB — see [Updating the firmware](manual/use/ota.md).

## Over Wi-Fi

On your network (`wifi_save <ssid> <pass>`), or on its own access point (`wifi ap on`; it also comes
on for 5 minutes when the board can't join a network at start, or when you press BOOT), the board
serves a web app for phones and computers at `http://holo-xxxx.local/`; `web` on the console prints
the addresses. It plays clips and shows images, sets the LEDs, points and animates the holo,
calibrates its servos, saves scenes and chooses the one the board starts with, manages the files on
the board, sets up Wi-Fi, and updates the firmware by upload or straight from the latest release. The
app is a front end for a JSON API that scripts can use too, described in OpenAPI at
`/api/v1/openapi.json`: see [The board over Wi-Fi](manual/use/web.md) and the
[HTTP API](manual/reference/http-api.md).

## Layout

| Path | What |
|---|---|
| `main/` | `app_main`, the console loop, and the holo's servos (`holo_servos.c`) |
| `components/board_ws128/` | Pins (`include/board.h`), shared I2C bus, LCD, touch, IMU, console colour parsing, and the `lcd`/`touch`/`imu` commands |
| `components/video/` | QuickTime parser, the player (clips and animated GIFs), the PNG/JPEG/GIF decoders, and the `video`/`screen`/`image` commands |
| `components/animatedgif/` | Packaging for the GIF decoder in `external/AnimatedGIF`, which is used unmodified |
| `components/webui/` | The web app's embedding (`web/`, gzipped into the image), the application's own API routes (`/screen`, `/media`, `/leds`, `/holo`, `/scenes`, `/settings`), and release channels from the documentation site |
| `components/scenes/` | Scenes (the screen, LEDs and holo saved together) and the settings the board starts with, kept in NVS; the `scene` and `settings` commands |
| `web/` | The web app itself: plain ES modules and CSS, no build step (`tools/web_dev.py` serves it from disk against a real board) |
| `components/leds/` | The NeoPixel strip on P2: Espressif's `led_strip`, the wipe and rainbow patterns, and the `leds` command |
| `components/canbus/` | The CAN (TWAI) controller through ESP-IDF's node driver (`canbus.h`: start, stop, send, a receive queue filled from the interrupt, error state, bus-off recovery, one owner at a time), and the `can` bench command on the host link's pins |
| `components/cyphal/` | The Cyphal/CAN node on `canbus` and libcanard: node-ID, heartbeat, `GetInfo`, the node-ID-in-use stop, its settings, and the `cyphal` command. `dsdl/` holds the Nunavut-generated C headers (`tools/gen_dsdl.py` regenerates them); `cyphal_util.c` is the part the host tests build |
| `components/libcanard/` | Packaging for `external/libcanard`, which is used unmodified |
| `external/esp-console-kit/` | Submodule: `cmd_system`, `cmd_wifi`, `cmd_network`, `cmd_nvs`, `cmd_i2c`, `cmd_fs` (+ `tools/fs_xfer.py`; `ota_core`, the update session every transport shares), `web_server` (HTTP server, `/api/v1/ota`, `/api/v1/web`, pulls, long operations), `web_api` (`/api/v1/fs`, `/servos`, `/network`), `servo`, `holo`, `i2c_bus_guard` (a link-time patch for ESP-IDF 6.1.0's I2C driver) |
| `external/AnimatedGIF/` | Submodule: [bitbank2/AnimatedGIF](https://github.com/bitbank2/AnimatedGIF), Apache-2.0, pinned to `c2478ec` |
| `external/libcanard/` | Submodule: [OpenCyphal/libcanard](https://github.com/OpenCyphal/libcanard), MIT, pinned to `v4.0.0` (v5 can't send the anonymous transfers plug-and-play needs) |
| `external/public_regulated_data_types/` | Submodule: the Cyphal DSDL, pinned to `a229bb7`, from which the C headers were generated and the hardware tests compile theirs |
| `tests/` | `host/`: C tests of the node's pure parts, built with the computer's compiler (`make unit-test`). `hardware/`: pytest against a board and a gs_usb CAN adapter (`make can-test`; `requirements-can.txt`) |
| `manual/` | The documentation site's pages (`mkdocs.yml`) |
| `tools/` | `check_command_docs.py` (every command is documented), `check_api_docs.py` (the OpenAPI description matches the firmware's routes), `check_api_examples.py` (its examples match its schemas), `api_smoke.py` (a board's replies match the description), `web_dev.py` (the web app from disk), `check_version.py` (release tags), `web_install_manifest.py` (release images and the installer's firmware), `vendor_js.py` (vendored JavaScript), `offline_serve.py` (`serve.py` in the offline docs), and `test_installer.mjs` and `test_board.mjs` with their `fixtures/` (the pages' JavaScript) |
| `tools/can_check.py` | The computer's side of `can`, through a gs_usb CAN adapter (python-can): `listen`, `send`, `echo` (numbered frames to a board running `can echo on`, checked as they come back) and `flood` |
| `tools/gen_dsdl.py` | Regenerates `components/cyphal/dsdl/` with Nunavut from the DSDL submodule |
| `partitions.csv` | nvs, otadata, two 2.25 MB OTA slots, coredump, and an 11 MB LittleFS `storage` volume at `/data` |

## Versioning

There is no version constant in the source: ESP-IDF takes `PROJECT_VER` from `git describe`, so
a release tag is the version baked into the image and reported by `version` and `ota`. A build
from an untagged tree reports the commit it came from. See [RELEASING.md](RELEASING.md).
