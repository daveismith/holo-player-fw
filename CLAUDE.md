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
example this started from. The tests are the board pages' JavaScript (`node --test`, in
`make docs-check`) and the Cyphal node's (`tests/`, "CAN and Cyphal" below); nothing else has any.

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
  once (seen 2026-10-22); the board is fine, retry. A log line after a command's prompt (Wi-Fi
  joining after a restart) leaves the output not ending at a prompt, and linenoise won't print
  another: `tests/hardware`'s `Board.command` sends an empty line once the output goes quiet, and
  then hands what came after the first prompt to its log.
- **A console that echoes nothing and gives no prompt, on a board that otherwise runs** (HTTP, CAN, its
  log lines all fine) is linenoise waiting for the answer to an `ESC[6n` it sent while no script was
  reading (seen 2026-11-20 after a harness gave up mid-command): every key goes into parsing that
  answer. Send a cursor report, `ESC[1;80R`, then a CR, and the prompt comes back. Seen again
  2026-11-21 in the middle of `test_holo.py` with no harness giving up first (a command's echo came
  twice, then nothing): there the first report drew another `ESC[999C ESC[6n`, which needed its own
  answer before the CR brought the prompt. A session that ends with the console stuck can't make the
  board quiet to close the adapter, which then wedged: recover it (below) before the next run.

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

**A known, temporary exception (2026-11-21): `tools/board_console.py`'s `_Port`.** The kit's
`tools/fs_xfer.py` `Console.pump()` answers linenoise's `ESC[6n`/`ESC[5n` only when a query is whole
within one serial read, and once a read however many came: a query split across two reads went
unanswered, and linenoise swallowed the next command (the console's stuck prompt). `_Port` overrides
`pump()` here, keeping the last bytes of each read (`_tail`) and answering each query by count. It is
not a patched kit file, but it is a kit fix living outside the kit, left so until the user decides on
kit changes. The kit change is that `_tail` and count logic moved into `fs_xfer.Console.pump()`;
once the kit has it and the pin is bumped, `_Port` goes and `Console` uses `fs_xfer.Console` again.

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
  replacement board restored from a backup takes the old one's place. **So do the Cyphal node's
  fixed node-ID and name** (NVS `cyphal`; 2026-11-20): the user's condition is that both can be
  changed and erased, which `cyphal id <n>|auto`, `cyphal name <name>|--reset` and `cyphal reset`
  do; keep that true. `manual/use/backup.md` tells the user that a second
  board restored from the same backup needs its own node-ID. **Touch reporting** (NVS `holo_cfg`,
  `touch`: `settings_t.touch`, the board's settings beside the backlight, which `/api/v1/settings`,
  `settings touch` and the Cyphal register `r2.touch.reporting` all change; 2026-11-21) is a setting,
  not a credential or an identity: it stays in. The holo's calibration registers are the kit servo's
  own NVS keys, in a backup as they always were.
- **The holo's Cyphal port numbers stay in too** (NVS `cyphal`, keys `pub.status` …
  `srv.media_info`, one a port of `r2_cyphal_dsdl`'s `ports/holo.csv`; 2026-11-20). The setpoint
  subject is per board (two holos on one subject move together), so they are a per-board setting
  like the node-ID: kept by the precedent of the user's condition for the node-ID and name, decided
  by the coordinator and flagged to the user for confirmation. The condition holds for them: each
  can be changed (`cyphal register uavcan.<kind>.<name>.id <n>`) and erased (`cyphal reset`, which
  puts every register back to its default); keep that true. `manual/use/backup.md` tells the user
  that a second holo restored from the same backup needs its own setpoint subject.
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
plays, 40 KB at the lowest. With the Cyphal node on the bus (2026-11-20, a 15-minute soak with a
clip, the ring and the web app): 103.6 KB free idle, 61-70 KB under load, 39.8 KB at the lowest.
With the holo's Cyphal ports (2026-11-20, 10 minutes of motions, setpoint bursts and lights over
Cyphal during a clip): 92-93 KB free idle, 50-62 KB under load, 39.9-40.5 KB at the lowest; the
worker's stack 3,052 of 6,144 used, the node task's 1,356 of 3,072, libcanard's heap 2.2 KB at most.
After the phase 3 clean-up (2026-11-21, Motion without JSON, the ring's task kept, 3 minutes): 92.4 KB
free idle at boot, 58.5-59.2 KB at the end of the run, 40.3 KB at the lowest after the holo's tests
and 38.4 KB after the register and command tests too (the ring's 3 KB stack is now held throughout,
where it used to come and go); the worker's stack 1,912-2,064 used. `make api-smoke WRITE=1`
takes the lowest to 23.4 KB, before and after: the kit's file routes (`PUT /fs/file`, then
`GET /fs/entry?sha256` and `GET /fs/file`), each tens of KB of internal RAM for a moment, not Cyphal.
With the holo's scenes, screen and events (2026-11-21, after phase 4's clean-up): 84.4 KB free idle at
boot (83.9 before it), 43-46 KB at the lowest at boot. What moves the lowest is the screen, not Cyphal: a
clip's JPEG decoder takes 8.9 KB for every frame and gives it back (free reads about 50 KB between frames,
41 KB during one), so anything else that allocates during a frame (a request, a console command, a Wi-Fi
packet) can set a new lowest at any minute (steps of 0.3-3.9 KB seen). That is not a leak: the holo's load
test (`tests/hardware/test_holo_load.py`) now checks that nothing is kept at rest (free after the run within
1 KB of free before it) and that the lowest moves by at most one frame's decoder (`DECODER_BYTES`), not to
the byte. A worst-case PNG takes it to about
32 KB, a clip replacing a clip to 24-26 KB, a GIF replacing a GIF to 5-12 KB (both GIFs' buffers at once;
the decoder is 24 KB, internal first by design, so whenever a block that big is free it takes it, and the
lowest there is whatever is left: more free RAM elsewhere lowers it). **Rapid screen replacement starves
internal RAM** (pre-existing, in the video player; not fixed): clips replaced 8-10 times a second, over HTTP
or Cyphal Screen alike, took the free heap to under 1 KB, and GIF replacing GIF to about 5 KB; shows are then
refused ("it could not be drawn") until the screen is cleared, and nothing crashed. The events store keeps
its 32 happenings' JSON in internal RAM (150-250 B each), so the free heap falls a little until it is full.
The largest free block read 31,744 B at rest throughout: no fragmentation seen. Two ways to give the Cyphal
side more room were offered to the user on 2026-11-21 and not taken: running the slow jobs (Scene, Screen,
identify) on the scenes runner's task instead of a second worker (7.5 KB of stack back), and a cJSON
allocator that prefers PSRAM (the events store, Event's parse, the routes' JSON).
With the lists and the holo's registers (2026-11-21, phase 5): 85.0 KB free idle after a restart, 44.9 KB
lowest at boot. 15 minutes with cyphal_service on the adapter, a clip, the ring and the web app polled
(6 routes every 2 s): 41.5-52.4 KB free, 31.0 KB at the lowest. 15 minutes with pycyphal, the same plus a
scene every 30 s, a setpoint burst a minute and the lists every 10 s: 39.6-51.8 KB free, falling 3.3 KB over
the first 4 minutes as the events store filled and then flat to the byte; the lowest stepped down with it,
to 15.5 KB, and stayed there for the last 10 minutes. libcanard's heap 3.5 KB at most, the send
queue 41 frames at most; stacks never used: the node task's 1,496 of 3,072, the worker's 2,228 of 4,608,
the second worker's 2,420 of 7,680 (MediaInfo and scenes on it).
**The 15.5 KB low was MediaInfo's probes** (measured in the review, 2026-11-21, with the heap's local-minimum
monitor around single calls): a probe took internal RAM for the GIF decoder (24 KB, internal first by the
player's choice), a clip's QuickTime tables (malloc puts anything under 16 KB in internal RAM: 9-28 KB for
these clips), a JPEG's bytes and libpng's state: at rest 8.9-27.9 KB for a clip, 28.9 KB for a GIF, 10.5-16.4
KB for a JPEG, 5.3-9.9 KB for a PNG; while a clip plays, a GIF's probe took the free heap to 13.8 KB. Probes
now take PSRAM first (`new_decoder(true)`, `heap_caps_malloc_prefer()` for the JPEG buffer, libpng's
`png_create_read_struct_2()` allocator, the parser's `Table` allocator while a `QuickTimeFile(f, true)`
parses: a `thread_local` flag), playback untouched: 5.6 KB for a clip, 4.3 KB for a GIF or a PNG, 8.9 KB for a
JPEG (esp_new_jpeg's decoder, which it places itself). The 4.3 KB every probe still takes is probably
stdio's buffer for its `fopen()` (newlib sizes it from the volume's block size); not checked.
After the review (2026-11-21, the final build): 15 minutes with cyphal_service holding the adapter, as
above: 42.0-51.3 KB free, 27.0 KB at the lowest, at rest 81.6 KB before and 81.2 KB after. The one-hour
pycyphal soak (`test_soak.py`: a clip, the ring, the web app polled, a scene every 30 s, a setpoint burst a
minute, the lists every 10 s and the whole root listed once a minute): free at its best 47.96 KB in the
first minutes, then 47.8 KB to the end, flat; at rest 81.1 KB before and 77.9 KB after (the events store
filling). **The lowest still stepped down, to 11.1 KB, by minute 14, and then held for 46 minutes**: lower
than the 15.5 KB before the probe fix, so MediaInfo was not the only part of it. Not traced: the soak adds
List walks and GetInfo to the phase 5 run, and a frame's decoder (8.9 KB), a MediaInfo (5.6 KB), `GET
/events` of a full store, `GET /scenes` and Wi-Fi buffers at the same instant would reach it. Nothing was
refused or ran out. libcanard's heap 2.4 KB at most, the send queue 25 frames; stacks never used: the node
task's 1,704 of 3,072, the worker's 2,508 of 4,608, the second worker's 3,356 of 7,680.
The kit's `SERVO_TASK_STACK_IN_PSRAM` would add 4 KB and is deliberately off
(`sdkconfig.defaults`). The kit's task-walk hook is not wired: the LCD is SPI with DMA, and the
ring is RMT with DMA when it gets a DMA channel, so `tasks`/`top` holding the kernel lock for
~2 ms tears nothing. `components/leds/leds.c` falls back to interrupt refills
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

## CAN and Cyphal

`link mode can` hands the host link's pins A and B to `components/cyphal`, a Cyphal/CAN node on
`components/canbus` (the TWAI controller) and libcanard; `can` is the raw-frame bench command on the
same pins. Built 2026-11-20; the user's side is `manual/use/cyphal.md` and `manual/connect/host-can.md`.

- **Tests.** `make unit-test`: the node's pure parts (`cyphal_util.c`, and the register table in
  `cyphal_registers.c`) and the holo's (`main/holo_cyphal_util.c`: requests, Status's parts, Event, the
  file services' paths and errors, MediaInfo, ListScenes, the calibration registers' values), built with
  the computer's `cc`, no IDF, and `yakut_can.py`'s refusals; the board's
  happenings decoder (`components/scenes/happening.c`) and the host link's event records (`hl_record.c`) with
  the cJSON the component manager puts in `managed_components/` (a first build fetches it; or `CJSON_DIR=`).
  `make can-test`: pytest in `tests/hardware/` against the console and a gs_usb
  adapter, pycyphal on the computer (its allocator for plug-and-play); `make can-setup` makes
  `.venv-can` from `requirements-can.txt` (or pass `CAN_PY=`). `ARGS="--board-host <ip>"` adds the
  test that uploads to the spare OTA slot without activating it, and asserts the board still boots
  `ota_0`. 33.5 minutes with `--board-host --holo-minutes 10` (measured 2026-11-21, 84 tests: 82
  passed, the timed scan's strict xfail, the soak skipped); about 26 with the load test at its default
  3 minutes. `--soak-minutes <n>` runs `test_soak.py` (skipped otherwise; `-k test_soak` to run it
  alone). Stop cyphal_service first. The suite sets fixed node-ID 52 and leaves it.
- **The adapter's firmware** (read with gs_usb's control-IN requests only, 2026-11-21): until that day a
  canable.io build of candleLight, features 0xF3 (listen-only, loop-back, hardware timestamps,
  identify, user ID, padded packets; no one-shot, bus-error reporting or GET_STATE). On 2026-11-21 the
  user flashed "the latest `main` of candleLight" (a build from its main branch, not a tagged
  release): the same strings ("canable.io", "canable gs_usb"), 1d50:606f, bcdDevice 0, and
  DEVICE_CONFIG sw_version 2 / hw_version 1 as before (constants: they don't name a build); features
  0x530BB (adds one-shot, bus-error reporting, GET_STATE and two flags past bit 13; drops user ID),
  fclk_can 48 MHz (an STM32F042/F072). The notes below are from the old firmware, marked so; the rules
  are kept until shown otherwise. **On the new firmware** (to the end of the phase 5 review, 2026-11-21):
  no incident at all (no degradation, no wedge, no `dfu-util`, no flush beyond the routine one, which
  read nothing each time) over 480 of 480 GetInfo, the targeted tests, the whole suite (84 tests,
  33.5 minutes), 15 minutes of cyphal_service (`get-info` 90 of 90, against 10-15 % timing out on the old
  firmware; no `Receive error`, no `InvalidFrame`) and the one-hour pycyphal soak. Yakut not tried on it.
- **The adapter (CANable, candleLight firmware) is fragile** (seen on the old firmware). Open and close it only while the
  board sends nothing: inside a fixed node-ID's 2 s listen after `cyphal restart` (a plug-and-play
  node is never quiet). The host sending unacknowledged (no node up, or a bitrate other than the
  board's: never let it) or a close while the board sends degrades it and then wedges it: it goes on
  acknowledging the bus and passes nothing to the computer. One program holds it at a time.
- python-can's gs_usb asks macOS to detach a kernel driver, refused without root;
  `can_check.patch_gs_usb_for_macos()` (the tests use it too) makes `is_kernel_driver_active` say
  no. The library is not patched.
- **After any python-can session, `tools/can_check.py flush` before cyphal_service**, or the
  service's first read can overflow (python-can's 24-byte timestamped frames into its 20-byte
  reads), macOS halts the pipe, the service never clears it and receives nothing ("Receive error"
  in a tight loop). Not a cure every time: once on 2026-11-20 the service still received nothing
  after a flush, and quitting it (in a listen) and repeating flush and start worked.
- **The adapter handed frames to the computer out of order now and then** (old firmware), so the service
  dropped the transfer: 10-15 % of its `get-info` calls timed out. pycyphal got 500 of 500. Not the board.
  On the new firmware 90 of 90 (15 minutes, 2026-11-21).
- **A degraded adapter loses frames both ways before it wedges** (old firmware, 2026-11-20, after a day of
  sessions): `test_worker_responses_prompt` lost 1 to 22 of 420 calls, some requests never on the bus
  (pycyphal's `out_frames` above the adapter's loopback count) and some responses the board sent and
  had acknowledged never reaching the computer; the phase 2 firmware lost as many. The DFU reboot
  below (detach, then leave) cleared it: 0 of 420. Compare `out_frames` with `in_frames_loopback`
  before suspecting the board.
- **Recovery** when the computer receives nothing while the board's `cyphal` shows frames sent and
  acknowledged: `flush`; failing that `dfu-util -e` (kill it if it hangs), wait, then
  `dfu-util -a 0 -s 0x08000000:16:leave -U <a new scratch file>` (reads 16 bytes, leaves DFU), and
  `ioreg -p IOUSB` should show `canable gs_usb`. **Never `-D`**: it writes the adapter's firmware.
  `!registered, !matched` in `ioreg` is macOS's accessory popup waiting for a click: ask the user.
- **`link mode uart` with a CAN transceiver on the pins loops**: the transceiver returns TX on RX,
  the link's `ready` comes back as a bad line, and so does each answer (bad lines ~40 a second).
  Harmless with nothing else on the bus; `link mode can` and a restart end it. RS485 sends nothing
  unasked and doesn't loop. No UART request has been made on this bench (no host serial port).
- **Why not 87.5 %** (`manual/connect/host-can.md` has the 85 % sample point): TSEG1 at most 16
  with an even prescaler from 80 MHz rules it out at 1 Mbit/s.
- **libcanard is pinned at v4.0.0**: v5 can't send anonymous messages, which plug-and-play v1
  needs. v4's anonymous transfers are single-frame (7 bytes); the request fits. Read the pinned
  header, not memory, before writing against it. `cyphal.c`'s `next_due()` walks v4.0.0's private
  deadline tree (`deadline_root`, `deadline_base`) as its own expiry does, so that "dropped" counts
  whole transfers: on a bump, re-read those fields and `txFlushExpiredTransfers()` first.
- **The DSDL C headers are generated and committed** (`components/cyphal/dsdl/`, from
  `external/public_regulated_data_types` at `a229bb7` and `external/r2_cyphal_dsdl` at `30acd0f`,
  by `tools/gen_dsdl.py` with Nunavut, one `nnvg` run a root with the other as lookup): the build
  and the release image need no Python. Don't edit them by hand. `r2/holo/ports.h` is the port
  table (`ports/holo.csv`) as an X-macro, which `main.c` turns into the node's port list, so the
  defaults are written once. Nunavut puts `uavcan.register` in `uavcan/_register/` (C keyword).
  Nunavut's `_initialize_()` isn't used: GCC 15 raises false array-bounds errors in it under
  `-Werror`; zero is the default. `r2_cyphal_dsdl` is changed in its own repository and the pin
  bumped here, as with the kit; a type that has shipped gets a new version, never an edit. **Push
  `r2_cyphal_dsdl` before pushing a pin that names an unpushed commit of it**: the release workflow
  and every clone fetch the pin from GitHub. As of 2026-11-21 the pin is `30acd0f` (a comment in
  `MotionSpec`), which is only in the local checkout; its `origin/main` is `aac2fe0`.
- **Submodules**: `external/libcanard` (the build needs it), `external/public_regulated_data_types`
  and `external/r2_cyphal_dsdl` (`gen_dsdl.py` and the tests). Clone `--recursive`.
- **Only the node's task calls libcanard** (its heap has no lock). Anything that may wait (a
  register's NVS write, restart, factory reset, identify; later the holo's routes) goes to the
  worker through a job slot (`cyphal.c`'s header): a full pool answers busy at once, and the
  worker wakes the node's task with `canbus_wake()` (a marker `canbus_receive()` skips, so its
  false still means "queue empty"); a stop wakes it the same way, and nothing uses
  `xTaskAbortDelay()`. `flush()`'s own `drain()` can read and skip the worker's wake, so the task
  looks at the slots again before it sleeps (`jobs_done()`): without that a response waited for
  the next timer, up to a second (`test_worker_responses_prompt`: 16 of 90 over 250 ms before). Responses are pushed by the node's task and sent only by `flush()`, so the
  order below holds. The node's own ports are one table, `SERVICES[]`, that subscribes them and
  makes the port list; a later phase's port is a row.
- **A restart over Cyphal happens with the adapter open.** The harness then sends nothing until
  the board is back (`Net.muted()` in `tests/hardware/conftest.py`); a factory reset brings the
  node back with plug-and-play, never quiet, which the tests handle with pycyphal's allocator.
- **Yakut wedged the CANable** (2026-11-20, on its old firmware; not tried on the new). `tools/yakut_can.py` opens and closes it in the
  board's listens, runs each `cyphal restart` to its prompt (linenoise swallows input until its
  `ESC[6n` is answered: a `cyphal restart` written without reading the reply was lost, and the
  adapter closed while the board sent) and closes while pycyphal still reads. It empties the
  adapter afterwards only with `--flush` (another open and close); without it,
  `tools/can_check.py flush` before cyphal_service, as after any python-can session. Its console is
  `tools/board_console.py`, the hardware tests' too. Even so, 4 of 13 Yakut sessions after that
  wedged it (all three `execute-command`s, and a read and a list), each needing the `dfu-util`
  recovery below, while the pycyphal tests never did. So the wrapper refuses
  `execute-command` and a `call` to ExecuteCommand (435), and the tests are the way to exercise
  commands. It also refuses a write of 65535 to `uavcan.node.id` (the closing `cyphal restart`
  would come back in plug-and-play, never quiet), `register-batch`, and a `--bitrate` other than
  the one `cyphal` shows. Click 8.2 and later name Yakut's command `execute`, not `execute-command`
  (8.3.1 in r2_rp2040_rust's venv), so the refusals are checked twice: by the static spellings before
  anything is imported, and again with the command word resolved through Yakut's own click group
  once Yakut is imported (`yakut_resolver()`), its value-taking global options (`--node-info`) skipped
  too. Its close always closes the adapter, after giving the node its fixed
  node-ID back if it isn't listening. Those refusals are `tools/test_yakut_can.py` (stdlib, in
  `make unit-test`); the wrapper's adapter paths have not been run on the bench since (2026-11-20).
  Yakut is not in `requirements-can.txt`: r2_rp2040_rust's venv has it (use it, install nothing
  into it).
- **canbus has one owner at a time, by handle**: `canbus_start()` returns it, and stop, send,
  receive and recover with any other are refused. `can` only reports while the node holds it.
- **Nothing is sent before every received frame has been checked for our node-ID** (`cyphal.c`'s
  header): the task empties the receive queue after the wait for room and just before each
  hand-over, and canbus is started with one send slot, so one frame at most goes after a frame from
  our node-ID arrives. Keep that order if the transmit path changes.
- **The holo's servo registers are read on the node's worker** (`CYPHAL_REG_READ_ON_WORKER`,
  `main/holo_cyphal_registers.c`): the kit's `servo_get_drive_policy()` takes the servos' mutex, held
  through an NVS write, and nothing on the node's own task may wait, so `on_access()` hands such a
  read to the worker in a job slot as it does a write. With every slot held the read goes unanswered
  (Access has no busy) and counts as busy; the client asks again. (Until 2026-11-21 the reads came
  from a copy refreshed once a second, which lagged a console or HTTP change by up to a second.) The
  kit's rules come through: clearing endpoints clears the policy, a saved policy's settle is 100 ms
  at least (and 100 is its default, not 0). No kit API is missing for these; none was patched.
- **The application's services on fixed IDs** (`cyphal_app_t.fixed_services`: `uavcan.file.List`
  and `GetInfo`) count as the node's own fixed ports for the port registers' rule; four at most. They
  are `slow` (the second worker): both wait for the littlefs lock, which an HTTP upload holds through
  each write (a List took up to 528 ms during a 1 MB upload, and on the worker held Status 1.36 s
  apart). A `busy()` that returns 0 sends no response (List's empty name is its end of list).
- **littlefs keeps a directory in name order** (its SPEC; byte order, as `strcmp`), so `readdir()`
  gives names sorted, and `uavcan.file.List` walks it to the index with nothing sorted or allocated
  (`test_files_against_http` checks the order). A filesystem other than littlefs would need a sort.
- **Touch start and stop take a mutex** (`components/board_ws128/touch.c`): the console, the settings
  (HTTP, console) and the Cyphal worker all call them. `board_touch_running()` is a pointer's read.
- **Plug-and-play checks an allocated ID before taking it up** (`manual/use/cyphal.md`, "A node-ID
  that is already taken"): an allocator that lost its records can hand out one in use. Online 1.1 s
  after the node's start by the board's log against cyphal_service, 1.3-1.4 s after a
  `cyphal restart` sent from the computer (the console's round trip included). Nothing is saved:
  the allocator's database is the record. The service gives this board node 2 (pseudo-ID
  `0b3af4648a21`); its row for 52 is from fixed-ID runs.
- **Anonymous frames are retransmitted until acknowledged**: IDF 6.1's TWAI has no per-frame
  one-shot (single-shot is the whole node's). Two allocatees whose requests share a CAN ID
  (1 in 128) could collide in lockstep; running the controller single-shot while waiting would fix
  it. Left for the user.
- **Nothing on the node's worker may wait** (`main/holo_cyphal.c`): its poll carries out the setpoints and
  builds Status, Axes and Event. Work that can take long is a `slow` handler (`cyphal_handler_t`, and
  `cyphal_app_t.identify_slow`): Scene, Screen, MediaInfo, the file services, and identify, run on the second worker (`cyphal_slow`, 7.5 KB
  stack: a palette PNG shown used 5,708 B on 2026-11-21; in internal RAM because it reads flash; the host link's
  10 KB task doesn't run in `link mode can`), two at most, in order. The worker's stack is 4.5 KB (2,088 B used
  at most over the tests). The poll reads the screen only through `screen_peek_state()`, which gives up rather
  than wait for the screen's lock: a PNG decode holds that lock for up to ~half a second. A new read in the poll
  must not take a lock a slow job holds.
- **Event is published from the kit's events store** (`main/holo_cyphal_events.c`), by a cursor, not from the
  listener's notify, one walk of the store a poll (8 at most). One the full queue (8) refuses is kept serialized
  and offered again, alone, 10 ms later and then at twice the interval to 160 ms, the store unread meanwhile; the
  store keeps 32, past which the gap shows in `seq`. The node has one events listener (`EVENTS_MAX_SINKS` is 4:
  the web server's, the host link's, this) for the happenings and for Status's ring, screen and scene parts, which
  are read only when their state kind says they changed (and once more as a Status goes); the board re-marks the
  screen and the scene every 5 s while a clip or a scene runs, which the parts' comparison ignores. `touch` is
  declared only when touch starts, so the kinds are looked at again each second.
- **The board's happenings are read back in one place**: `components/scenes/happening.c` (`happening_of()`, host-
  tested), for both the host link's native records (`hl_record.c`, host-tested byte for byte) and Cyphal's Event;
  each consumer keeps its own rounding (the host link's whole seconds, rounded up; Event's milliseconds).
- **A published message waits in its mailbox until the transmit queue has room for all of it**
  (`collect_mail()`), so a burst of Events can't be dropped by libcanard; `cyphal` shows the queue's peak.
  Such a message is no new mail to `node_task()`: `collect()` says it held one, and the task then waits as
  the queue does (a tick while bus-off, when canbus refuses at once). Read as new mail it made the wait 0,
  and the task, the highest on core 0, spun until frames expired (found by review 2026-11-21; the bus-off
  method under "Not tested on the board" reproduces it).
- **What phase 4 saw of Event on the board** (2026-11-21): with the queue (8) full, 19 happenings were
  queued and all went in order as room came; 90 made at once went out 60 delivered and 30 as jumps in `seq`,
  once the store (32) had overwritten them before their turn: by design, as for a receiver that missed
  them. A happening the poll can't parse for want of memory is tried again (cursor kept), up to about 4 s.
- **Two workers, nothing shared that either writes**: the worker's Access response is built in `s_ws`; the
  second worker (Scene, Screen, identify) builds every response on its own stack, `work_command()`'s
  included (the two once shared `s_ws`, and an identify could zero a register write's answer). The poll's
  state (`s_poll`, `s_again`, the events statics) is the worker's alone; the second worker reaches only the
  scenes, screen and LEDs code that HTTP and the host link already call from their own tasks. Keep any new
  state on one side, or lock it.
- **A file at a 159-byte path can't be shown** (seen 2026-11-21), though 159 is the routes' limit
  (`FS_PATH_MAX`) and `Status`'s path holds it. Probably the video player's 160-byte `PATH_LEN` against
  the absolute path (`/data` and 159 bytes); not looked into further, not fixed.
- **The console's stuck prompt was linenoise's cursor query split across two serial reads**: fs_xfer looks
  for `ESC[6n` within one read, so a query cut in two (or a second one in the same read) went unanswered, and
  linenoise swallowed the next command waiting for it. `tools/board_console.py` answers across reads; the kit's
  `fs_xfer.Console.pump()` still has the bug (a kit fix, not made here: "esp-console-kit" has the exception).
- **The `gpio` table's host link entries are live** (`main.c`): `hostlink_start()` fills them
  through `hostlink_set_on_pins()`'s callback, as `can` fills its own through `on_pins`, named for
  the holder, empty if the start failed.
- **The holo's ports are the application's** (`main/holo_cyphal.c`, its pure parts
  `holo_cyphal_util.c`, host-tested): `components/cyphal` knows no board. `cyphal_app_t.handlers`
  gives a port its decode (on the node's task, into a job slot), work (on the worker) and busy
  answer, or a subscription's `on_message` (on the node's task: a copy into a mailbox and
  `cyphal_wake_worker()`), or a published port's mailbox size and depth; `poll` runs on the worker
  and builds Status and Axes, which `cyphal_publish()` drops into the node's mailboxes for the node's
  task to send. A mailbox of depth 1 keeps the newest (Status, Axes); a deeper one is a queue that
  refuses with `ESP_ERR_NOT_FINISHED` when full and never overwrites: that is for phase 4's `Event`,
  whose happenings mustn't be lost unseen. No copy is made under a mailbox's spinlock: a publisher
  claims a buffer, fills it and commits it, and the node's task pushes straight from the buffer it
  took. A port without a handler is neither used nor in `port.List`. Nothing outside the node's task
  touches libcanard; keep it so.
- **The poll looks only at what can go.** Inside 250 ms of a Status and 100 ms of an Axes it builds
  nothing and sleeps the rest; then it looks at the holo every 50 ms (`holo_status()` once a pass,
  the holo part compared with the last sent, times aside). The ring is read again only when the
  kit's `events` says `leds` changed (a listener of its own, one of `EVENTS_MAX_SINKS`; it must
  never want `holo`, or `api_events.c`'s 250 ms JSON watcher starts), and for the time left when a
  Status goes. Phase 4's screen and scene parts should come the same way, by their events.
- **Motion runs the HTTP route's code** (`scene_holo_change()`, what `POST /api/v1/holo/motion` runs
  once `scene_do_holo()` has read its JSON, with only the fields the motion uses): never a second
  motion implementation, and no JSON on the way. Light runs `scene_leds_change()`, PATCH /leds's own
  code, which takes a leia's mark JSON can't. A refusal's code comes from `api_scene_code()`, and the
  host link's from `api_code_of()` (`components/board_api/api_codes.c`, host-tested): one mapping. Motion's `off` also turns the ring off (the holo's light
  here); the HTTP and host link `off` don't, since a scene's holo part would undo its LED part.
- **A setpoint is a direct move**: `holo_motion()` STOP if anything runs, then `servo_set_percentage()`
  per axis, as the panel does for ROS joint commands. The engine's default axis hook only reads the
  servo's pulse once *it* has placed the axis since boot, so the first setpoint after a boot goes
  through the engine as a MOVE with duration 0; without that the engine would think the axes
  unplaced and the next motion would start from the centre. A MOVE is whole percent, so the same
  setpoint goes again the direct way once the engine has carried it out (`s_again`, polled 20 ms
  apart, dropped if a newer setpoint, a motion or anything else asked of the engine comes first).
- **The ring keeps its own light implementation** (decided 2026-11-20/21; the user was offered the
  alternative, a light hook in the kit's holo engine driving the ring, and chose not to for now):
  Light, pulse, blink, levels, timed modes and a leia's mark are `components/leds` and
  `scene_leds_change()`, beside the engine's own light, which stays `none` here. So the console's
  `holo led` and `holo leia`'s light do nothing on this board, and a kit change to the engine's light
  doesn't reach the ring. Revisit only with the user.
- **The light's level is a share of the ring's brightness** (decided in phase 3, a change from the
  reviewed pages): Light's default level 100 would otherwise light a VSYS-powered ring at full.
- **`r2.holo.MotionSpec`'s kind numbers are the host link's HOLO frame's**, not `holo_motion_kind_t`'s
  (the engine has SCAN at 4): `holo_cyphal.c`'s `SPEC_OF_KIND[]` maps them, both ways. The DSDL's
  comment says otherwise; it's only a comment.
- **The ring's pattern task is made once and kept** (`components/leds/leds.c`): it sleeps on its
  notification until a frame, a blink's edge or a timed setting's end, and a change or a brightness
  wakes it. Its 3 KB stack is internal RAM from the first animated or timed setting on.
- **A timed scan never ends** (found 2026-11-20, a kit bug, not fixed here): the kit's `holo.c`
  `tick_holo()` looks at `motion_until` only while no segment is in flight, and a scan starts its next
  segment the moment one ends. Console, HTTP and Cyphal alike; `test_holo.py`'s timed-scan case is a
  strict xfail, so it fails once the kit is fixed and the pin bumped. Fix it in the kit.
- **NOT_READY can't happen on this bench**: `holo_servos.c` sets `allow_uncalibrated`, and both servos
  are MCPWM pins that always attach, so the holo is always ready and the heartbeat nominal. Status's
  `calibrated` is the servo's saved endpoints, not the engine's notion.
- **`make can-test ARGS=tests/hardware/test_holo.py` runs the whole suite** (the Makefile passes
  `tests/hardware` as well): select with `-k` instead (`ARGS="-k test_holo"`). `test_holo.py`
  compares each request with the same one over HTTP when `--board-host` is given (the `http`
  fixture, `tools/api_smoke.py`'s `Board`, with `$HOLO_PASSWORD`). The holo's load test runs
  `--holo-minutes` (3 by default); the phase gates run it with 10.

## Not tested on the board

As of 2026-10-22: `import_note()`'s reminder (`main/main.c`) on a board with no saved network (it
would mean erasing them); a real NVS wipe and restore; the guard clearing state here; the I2C
host link (not built); an OTA from a release since the kit bump.

CAN, as of 2026-11-20: flooding and a bitrate mismatch never reach bus-off on the bench, but moving
CAN RX (link pin B) to the unconnected GPIO15 does, hands-free: `link pins 21 15 -1` and a restart
(the adapter closed; the driver pulls RX up, so the controller never reads back its own dominant
bits). Bus-off then comes within the first frame, every time. Seen that way: the node's recovery
and back-off, `can recover`, and the send slots across a bus-off (each costs one frame, counted
failed; none leaks). `link pins 21 33 15` and a restart put it back. Still by reading only: frames
queued behind a bus-off going out after the recovery (the console can't queue a second in time).
Done by hand the same day: the adapter unplugged for a minute (error-passive, no bus-off, node-ID
kept; cyphal_service goes on running with a dead handle and has to be restarted), and CANH shorted
to CANL for 5 s (4 bus-offs, the back-off as designed, error-active again 11 s after the first,
one frame lost per bus-off, none leaked; the service logs `InvalidFrame` at 13,000 lines a second
during the short and recovers without a flush). A second plug-and-play board on the same bus (the
shoulder sensor, 6.3) was dropped from the plan and has never been tried; nor has bus-off from a
short under `can`. By reading only: a failed controller stop in `cyphal restart`, a stop's wake
read by the task's last `drain()` (the task looks at `s_running` once more before it sleeps), a
node start that fails at boot. Plug-and-play's hold while another node is heard on the allocated
ID is proven by the host tests, not by timing on the bench.

The holo on Cyphal, as of 2026-11-21: Motion's and a leia's refusals for an uncalibrated or
unfitted axis, and Status and the heartbeat while the holo can't move (the bench's holo is always
ready: see "NOT_READY can't happen on this bench" above); Motion `off` turning the ring off when
the holo engine didn't start; a message dropped because the transmit queue was full (counted as
dropped, not published): all by reading only. A timed scan's end can't be tested until the kit is
fixed. Phase 5's, by reading only: a register write to an axis with no servo; a failed NVS save in
a register write; the file services on a directory larger than the root here (44 entries: 43 files
and `bench/`; the tree holds 45 clips and images, `GET /api/v1/media`'s count) and littlefs's order
across a directory of several metadata blocks; MediaInfo on a 159-byte path; touch reporting set by
the register surviving a power cycle (a `restart` was checked); a register read left unanswered with
every job slot held; a ListScenes entry that doesn't parse.
