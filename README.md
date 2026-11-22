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

The shared console commands, the CAN controller, libcanard and the Cyphal node (all in the kit,
`external/esp-console-kit`) and the GIF decoder live in git submodules, which the firmware needs to
build. The holo's own Cyphal data types (`external/r2_cyphal_dsdl`, with its port table) are needed
only to regenerate its C headers (`tools/gen_dsdl.py`) and to run its hardware tests
(`tests/hardware/`), as are Cyphal's public data types, which are the kit's
(`external/esp-console-kit/third_party/`). Clone with:

```sh
git clone --recursive <url>
# or, in an existing clone:
git submodule update --init --recursive
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
| `main/` | `app_main`, the console loop, the holo's servos (`holo_servos.c`), and the holo's Cyphal ports (`holo_cyphal.c`: the setpoints, Status and Axes; `holo_cyphal_services.c`: Motion, Light, Ring, Scene and Screen; `holo_cyphal_events.c`: Event and the events listener; `holo_cyphal_lists.c`: ListScenes, MediaInfo and the standard file services; `holo_cyphal_registers.c`: the holo's calibration, drive policy and touch reporting as registers; their pure parts, which the host tests build, in `holo_cyphal_util.c`) |
| `components/board_ws128/` | Pins (`include/board.h`), shared I2C bus, LCD, touch, IMU, console colour parsing, and the `lcd`/`touch`/`imu` commands |
| `components/video/` | QuickTime parser, the player (clips and animated GIFs), the PNG/JPEG/GIF decoders, and the `video`/`screen`/`image` commands |
| `components/animatedgif/` | Packaging for the GIF decoder in `external/AnimatedGIF`, which is used unmodified |
| `components/webui/` | The web app's embedding (`web/`, gzipped into the image), the application's own API routes (`/screen`, `/media`, `/leds`, `/holo`, `/scenes`, `/settings`), and release channels from the documentation site |
| `components/scenes/` | Scenes (the screen, LEDs and holo saved together) and the settings the board starts with, kept in NVS; the `scene` and `settings` commands; the board's happenings read back from their JSON (`happening.c`, which the host tests build) |
| `web/` | The web app itself: plain ES modules and CSS, no build step (`tools/web_dev.py` serves it from disk against a real board) |
| `components/leds/` | The NeoPixel strip on P2: Espressif's `led_strip`, the wipe, rainbow, flicker, pulse and blink patterns, the holo light's level and timed modes, and the `leds` command |
| `components/hostlink/` | The host link on P2's spare pins: its settings and transports (`hostlink.c`'s per-transport table), UART and RS485 (`hl_uart.c`), JSON lines and native frames (their event records: `hl_record.c`, which the host tests build), events, the `link` command and the `/link` routes; `can` mode hands pins A and B to the Cyphal node |
| `external/esp-console-kit/` | Submodule: `cmd_system`, `cmd_wifi`, `cmd_network`, `cmd_nvs`, `cmd_i2c`, `cmd_fs` (+ `tools/fs_xfer.py`; `ota_core`, the update session every transport shares), `web_server` (HTTP server, `/api/v1/ota`, `/api/v1/web`, pulls, long operations), `web_api` (`/api/v1/fs`, `/servos`, `/network`), `servo`, `holo`, `i2c_bus_guard` (a link-time patch for ESP-IDF 6.1.0's I2C driver), and the CAN and Cyphal parts: `canbus` (the CAN (TWAI) controller through ESP-IDF's node driver, a receive queue filled from the interrupt, error state, bus-off recovery, one owner at a time, and the `can` bench command), `libcanard` (packaging for OpenCyphal/libcanard, MIT, v4.0.0, which is used unmodified: v5 can't send the anonymous transfers plug-and-play needs) and `cyphal` (the Cyphal/CAN node: node-ID, heartbeat, `GetInfo`, the node-ID-in-use stop, its settings as registers, `ExecuteCommand`, the application's ports through its handlers, and the `cyphal` command; the standard `uavcan` data types as committed Nunavut headers). The kit's `third_party/` holds the libcanard and `public_regulated_data_types` (pinned to `a229bb7`) submodules |
| `external/AnimatedGIF/` | Submodule: [bitbank2/AnimatedGIF](https://github.com/bitbank2/AnimatedGIF), Apache-2.0, pinned to `c2478ec` |
| `main/dsdl/` | The holo's own Cyphal data types (`r2/holo/`) as Nunavut-generated C headers, and the holo's port table `ports.h` (`tools/gen_dsdl.py` regenerates them); they include the kit's `uavcan` headers |
| `tests/` | `host/`: C tests of the node's pure parts (the kit's, built against the holo's port table), the holo's Cyphal conversions, and the board's happenings as Event and the host link's records, built with the computer's compiler (`make unit-test`; cJSON from `managed_components/`, there after a first build). `hardware/`: pytest against a board and a gs_usb CAN adapter (`make can-test`; `requirements-can.txt`) |
| `manual/` | The documentation site's pages (`mkdocs.yml`) |
| `tools/` | `check_command_docs.py` (every command is documented), `check_api_docs.py` (the OpenAPI description matches the firmware's routes), `check_api_examples.py` (its examples match its schemas), `api_smoke.py` (a board's replies match the description), `web_dev.py` (the web app from disk), `check_version.py` (release tags), `web_install_manifest.py` (release images and the installer's firmware), `vendor_js.py` (vendored JavaScript), `offline_serve.py` (`serve.py` in the offline docs), and `test_installer.mjs` and `test_board.mjs` with their `fixtures/` (the pages' JavaScript) |
| `tools/can_check.py` | The kit's tool (`external/esp-console-kit/tools/can_check.py`, which this file loads): the computer's side of `can`, through a gs_usb CAN adapter (python-can): `listen`, `send`, `echo` (numbered frames to a board running `can echo on`, checked as they come back), `flood`, and `flush`, which empties the adapter before another program (cyphal_service) opens it |
| `tools/yakut_can.py` | Yakut on the board through the same adapter, opened and closed only while the board is quiet (`make yakut`; `--flush` empties the adapter afterwards); it refuses what has wedged the adapter or would leave the board never quiet (commands, a node-ID of 65535, another bitrate than the board's), checked by `tools/test_yakut_can.py` in `make unit-test` |
| `tools/board_console.py` | The board's console for `yakut_can.py` and the hardware tests: commands with the log between them, and `cyphal restart` for the adapter's quiet moments |
| `tools/gen_dsdl.py` | Regenerates `main/dsdl/` (the r2 root only) with Nunavut from `r2_cyphal_dsdl`, with the kit's `public_regulated_data_types` to look types up in and a check that its revision is the kit's, and turns the holo's port table (`ports/holo.csv`) into `r2/holo/ports.h`, checked |
| `partitions.csv` | nvs, otadata, two 2.25 MB OTA slots, coredump, and an 11 MB LittleFS `storage` volume at `/data` |

## Versioning

There is no version constant in the source: ESP-IDF takes `PROJECT_VER` from `git describe`, so
a release tag is the version baked into the image and reported by `version` and `ota`. A build
from an untagged tree reports the commit it came from. See [RELEASING.md](RELEASING.md).
