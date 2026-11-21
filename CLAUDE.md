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
  board restored from the same backup needs its own node-ID.
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
  `cyphal_registers.c`), built with the computer's `cc`, no IDF, and `yakut_can.py`'s refusals.
  `make can-test`: pytest in `tests/hardware/` against the console and a gs_usb
  adapter, pycyphal on the computer (its allocator for plug-and-play); `make can-setup` makes
  `.venv-can` from `requirements-can.txt` (or pass `CAN_PY=`). `ARGS="--board-host <ip>"` adds the
  test that uploads to the spare OTA slot without activating it, and asserts the board still boots
  `ota_0`. About 22 minutes with `--board-host` (estimated 2026-11-21: the holo's load test at its
  default 3 minutes; 29 with `--holo-minutes 10`); stop cyphal_service first. The suite sets fixed
  node-ID 52 and leaves it.
- **The adapter (CANable, candleLight firmware) is fragile.** Open and close it only while the
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
- **The adapter hands frames to the computer out of order now and then**, so the service drops the
  transfer: 10-15 % of its `get-info` calls time out. pycyphal gets 500 of 500. Not the board.
- **A degraded adapter loses frames both ways before it wedges** (2026-11-20, after a day of
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
  `external/public_regulated_data_types` at `a229bb7` and `external/r2_cyphal_dsdl` at `aac2fe0`,
  by `tools/gen_dsdl.py` with Nunavut, one `nnvg` run a root with the other as lookup): the build
  and the release image need no Python. Don't edit them by hand. `r2/holo/ports.h` is the port
  table (`ports/holo.csv`) as an X-macro, which `main.c` turns into the node's port list, so the
  defaults are written once. Nunavut puts `uavcan.register` in `uavcan/_register/` (C keyword).
  Nunavut's `_initialize_()` isn't used: GCC 15 raises false array-bounds errors in it under
  `-Werror`; zero is the default. `r2_cyphal_dsdl` is changed in its own repository and the pin
  bumped here, as with the kit; a type that has shipped gets a new version, never an edit.
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
- **Yakut wedges the CANable** (2026-11-20). `tools/yakut_can.py` opens and closes it in the
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
  the one `cyphal` shows; its close always closes the adapter, after giving the node its fixed
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
fixed.
