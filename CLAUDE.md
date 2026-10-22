# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this project is

Firmware for the **Waveshare ESP32-S3-Touch-LCD-1.28** (ESP32-S3R2, 2 MB quad PSRAM, 16 MB flash,
GC9A01 240x240 round LCD, CST816S touch, QMI8658 IMU), the "smart" front holoprojector of an
R2-D2 dome: clips and stills on the round panel, a NeoPixel ring, two holo servos on MCPWM, a web
app and HTTP API, OTA, and a host link over UART or RS485. ESP-IDF 6.1.

This file is the reasoning and the traps. What the firmware does is in `manual/` (the
documentation site, `mkdocs.yml`), how a release is cut and versioned is `RELEASING.md`, and
`README.md`'s Layout table says where each part lives: read those rather than restating them
here. `pytest_console_advanced.py` is an unmodified upstream artefact of the `console_advanced`
example this started from; there is no on-target test suite.

## Toolchain

ESP-IDF 6.1 is installed by the **ESP-IDF Installation Manager (EIM)**. `$IDF_PATH/export.sh`
fails, and activation defines `idf.py` only as a shell alias, which a non-interactive command
does not get. So call the interpreter and the script by path:

```sh
. /Users/davids/.espressif/tools/activate_idf_v6.1.sh >/dev/null 2>&1 && \
  /Users/davids/.espressif/tools/python/v6.1/venv/bin/python \
  /Users/davids/.espressif/v6.1/esp-idf/tools/idf.py build
```

The activation script kills a bash subshell; run it from zsh. `.mcp.json` wires up ESP-IDF's own
MCP server with the same activation. Paths are this machine's EIM install.

**`sdkconfig` is gitignored; `sdkconfig.defaults` is the source of truth.** A change made in
menuconfig vanishes on a clean checkout, and a change to the defaults does not reach an existing
`sdkconfig`: delete it and build. The build uses `-Werror`.

## Flashing

The USB-C port is a **CH343P USB-UART bridge to UART0** (115200), not the S3's native USB, which
is not connected. Its DTR/RTS drive the auto-download circuit, so flashing needs no buttons:

```sh
idf.py -p /dev/cu.wchusbserial5B910448341 flash
```

- **What `idf.py flash` writes** (`build/flash_args`): the bootloader at 0x0, the partition table
  at 0x8000, `ota_data_initial.bin` at 0xe000 and the app at 0x10000, which is `ota_0`. It never
  touches `nvs` (0x9000), `coredump` or `storage` (LittleFS at 0x500000, the clips), so settings
  and files survive a flash.
- **The otadata reset is the trap.** It makes `ota_0` the boot image, whatever ran before: a
  board that an OTA update had moved to `ota_1` comes back up from `ota_0`, and the rollback
  state is gone. `ota` on the console shows both slots.
- Before flashing a board whose layout you are not sure of, read its partition table back
  (`esptool read-flash 0x8000 0xc00 pt.bin`) and compare it with
  `build/partition_table/partition-table.bin`.
- **Opening the port with pyserial's defaults resets the board**, and so does any esptool
  command, `read-flash` included. The kit's `tools/fs_xfer.py` `Console` opens with DTR and RTS
  both asserted (which drives neither EN nor GPIO0) and then drops RTS before DTR, which only
  ever touches GPIO0: the other order is a reset. Use it for scripting, or `fs_xfer.py run`.
  The port does not change across a reset, so `idf.py flash monitor` works here.
- Scripting the console means answering linenoise's `ESC[6n`/`ESC[5n` queries, which `fs_xfer`
  does. A clip's asynchronous output can land after the prompt and make a scripted sync time out
  once (seen 2026-10-22); the board is fine, retry.

## esp-console-kit

`external/esp-console-kit` (git submodule, `github.com/daveismith/esp-console-kit`) is shared with
the dome panel firmware (`r2_domeplayer`). **A fix to any of it is made in the submodule and
committed there, then the pin is bumped here** (and in the other firmware). Never patch a kit file
in place in this repo. Kit work for the dome is often done in r2_domeplayer's checkout of the
kit; to bring an unpushed kit branch here, `git fetch <that checkout> <branch>` inside
`external/esp-console-kit`, and push the kit before pushing a pin that names it.

A pin bump is not neutral: read the kit's log for the range first. The ones on 2026-10-22 moved
the servo objects into PSRAM, added three console commands, a component and a change to Wi-Fi at
boot, and changed what `cmd_i2c` and `servo` require.

## The docs gate

**`make docs-check` must pass** (stdlib only, no venv needed):

- `tools/check_command_docs.py`: every console command registered **anywhere in `main/`,
  `components/` or the kit's sources** appears in backticks in `manual/`, and
  `manual/javascripts/board/commands.js`'s `GROUPS` names exactly the registered set. It scans the
  kit's source, not this build, so a command the kit gained for the dome fails the gate here
  until it has a row in `manual/reference/console.md` (or a reasoned `KNOWN_UNDOCUMENTED` entry).
- `tools/check_api_docs.py`: `manual/reference/openapi.json` describes exactly the routes the
  firmware and the kit register. The firmware embeds that file and serves it.
- A bare fenced block in `manual/` holds only board commands, one a line: the pages put a Run
  button on each. Output goes in a fence marked `text`, host commands in `sh`.
- The board pages' JavaScript has tests (`node --test`, run by `docs-check`). A command that
  cannot be run from a page (`fs put`, `ota put`, `nvs_import -`) is refused in `refusal()`; one
  that changes the board gets a confirmation in `danger()`.

`make api-smoke HOST=<ip>` checks a live board's replies against the description (`WRITE=1` also
changes things, under `/test-api`); `make docs-build` builds the site strictly. `CHANGELOG.md`
keeps an `Unreleased` section; nothing is tagged outside a release.

## Configuration backup

`nvs_inventory`, `nvs_export` and `nvs_import` are the kit's `cmd_nvs` backup, registered in
`app_main` (`main/main.c`, which has the fetch hook and its reasons). The user's side is
`manual/use/backup.md`; the engine's rules are `external/esp-console-kit/cmd_nvs/nvs_backup.h`.

- **No credential is ever in an export.** The kit's default exclusions cover every one this board
  has. **Whoever adds an NVS key that holds a passphrase, password, key or token excludes it in
  the same change** (`nvs_backup_exclude()` before `register_nvs_backup()`, reason
  `credential`), and the same for a per-board identity or a counter. Checked 2026-10-22 by
  reading the raw NVS partition with IDF's `nvs_tool.py` and searching an export, plain and hex,
  for every SSID and passphrase in it: none.
- **The hostname and the host link's address stay in** (decided 2026-10-19 and 2026-10-22): a
  replacement board restored from a backup takes the old one's place.
- **`nvs_import -` works over this UART console** (2026-10-22). No `stdin_refusal` is installed
  because stdin is always the console's; the host link has a UART of its own. If a second
  console ever shares stdin, install one.

## The I2C bus guard

ESP-IDF 6.1.0's `i2c_master_probe()` writes RX FIFO bytes through a stale pointer when the read
before it ended in a timeout or lost arbitration; the kit's `README.md` and the header of
`i2c_bus_guard/i2c_bus_guard.c` have the mechanism. On the dome it was months of random panics.
This bus carries touch and the IMU, and the IMU's probe at start and `i2cdetect` probe it (as
would the kit servo's PCA9685 attach, were a board ever attached here).

- `main` lists `i2c_bus_guard` although nothing here calls its API (the kit's `cmd_i2c` and
  `servo` pull it in on 6.1.0 anyway). That is for the `#error`: the guard refuses to build
  against any IDF but 6.1.0, so an EIM update stops there until someone re-reads
  `i2c_master_probe()` and the ISR. Without `main`'s entry the kit would leave the guard out on
  another IDF, with at most a configure warning, and the build would go on unguarded. The
  release workflow builds in the `v6.1` image (`.github/workflows/release.yml`): if that tag
  moves on to a 6.1.1, a release build stops at the same `#error`.
- Its `--wrap`s are the component's INTERFACE link options. To see that they took effect, the
  map's Cross Reference Table (`build/holo-player-fw.map`) lists every caller under
  `__wrap_i2c_master_*` and only the guard under the real symbols.
- Nothing on this board prints `i2c_bus_guard_stale_cleared()`. It has never been seen to catch
  anything here: there is no fault rig for this bus, as the dome's Pico is.

## Memory

Internal RAM is not this board's constraint; PSRAM bandwidth is the video player's. Measured
2026-10-22 with Wi-Fi and the web server up: 104 KB internal free at idle, 70 KB while a clip
plays, 40 KB at the lowest. The kit's `SERVO_TASK_STACK_IN_PSRAM` would add 4 KB and is
deliberately off (`sdkconfig.defaults`). The kit's task-walk hook is not wired: the LCD is SPI
with DMA, and the ring is RMT with DMA when it gets a DMA channel, so `tasks`/`top` holding the
kernel lock for ~2 ms tears nothing. `components/leds/leds.c` falls back to interrupt refills
when no channel is free, with a boot WARN (`RMT with DMA: ...; trying without`); wire the hook
if that ever becomes the normal case.

## Wi-Fi at start

`main/wifi_fallback.c` turns the access point on when the board cannot join a network within
`HOLO_WIFI_JOIN_WAIT_S` of starting. Since the kit's `wifi auto off` (2026-10-22) the boot rejoin
can be switched off, and the fallback then skips its check: without that, the access point came
on 20 s into every boot of a board told to stay off its network. Its test is
`wifi_known_autojoin()`, which repeats the kit's own condition for the rejoin; if the kit gains
another reason to skip it, change both. "No network" in the fallback means no network to
rejoin (`wifi_known_list`'s `last`), not none saved.

## Not tested on the board

As of 2026-10-22: `import_note()`'s reminder (`main/main.c`) on a board with no saved network (it
would mean erasing them); a real NVS wipe and restore; the guard clearing state here; the I2C
host link (not built); an OTA from a release since the kit bump.
