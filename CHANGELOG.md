# Changelog

Notable changes to the holo player firmware. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[semantic versioning](https://semver.org/spec/v2.0.0.html).

The version is not written down anywhere in the source. ESP-IDF derives it from `git describe`,
so the release tag is the version baked into the image and reported by `version` and `ota`. See
[RELEASING.md](RELEASING.md).

## [Unreleased]

### Fixed

- **A twitch or a scan with a time limit holds at that time** (Motion's `time_limit_ms`, the HTTP API's
  `time_s`, the console's `-t`): a timed scan never ended before, a fault in the kit's holo engine, which
  looked at the time only between a motion's segments. A glance or a sweep in flight at the limit now
  stops there. The kit pin is `c9cc126`, which also brings the kit's Cyphal client, off in this firmware
  (its `cyphal`, `libcanard` and `canbus` objects are those of `38dcbf9` but for one line-number constant).

### Changed

- **The DSDL generator moved to `r2_cyphal_dsdl`** (`external/r2_cyphal_dsdl/tools/gen_dsdl.py`), where
  the dome panel's firmware runs it too, and `tools/gen_dsdl.py` is gone. The pin is `137f454`; the types
  are unchanged, and the headers it generates here are byte for byte those committed before.

- **The kit pin is `38dcbf9`**: the kit's Cyphal node can now be built as a plug-and-play allocator
  (`CONFIG_CYPHAL_PNP_ALLOCATOR`), off in this firmware, whose `cyphal` objects are identical to before
  except one line-number constant.
- **The CAN controller wrapper, libcanard and the Cyphal node are the kit's** (`canbus`, `libcanard`
  and `cyphal` in `external/esp-console-kit`, shared with the dome panel's firmware), and this
  repository's copies of them are gone, with the `external/libcanard` and
  `external/public_regulated_data_types` submodules (now the kit's `third_party/`). Nothing the board
  does changed: the Kconfig values and the node's code are the same (the kit's `can` gained an optional
  print hook, a NULL check on its pin callback and Kconfig symbols for its task, defaulting to this
  board's values), and the image is 112 bytes larger. The holo's own Cyphal types (`r2/holo/`, with the
  port table) moved to `main/dsdl/`; `tools/gen_dsdl.py` generates only those, and checks the kit's DSDL
  revision; `tools/can_check.py` loads the kit's; `make unit-test` builds the kit's node tests against
  the holo's port table. Clone with `--recursive`.

### Added

- **`can`**: plain CAN frames on the host link's pins A and B, for checking a CAN transceiver and
  a bus before anything runs on it. `can start` takes the pins (1 Mbit/s unless `-b`; `--listen`
  never drives the bus, `--selftest` hears its own frames with no second node), `can send`
  sends one frame, `can dump` prints what arrives, `can echo` sends each frame back on the next
  ID, and `can` alone shows the bus's state, its error counters and the frames sent, received
  and dropped. `can recover` brings the controller back from bus-off; `can stop` gives the pins
  back. It refuses to start while the host link runs on those pins, and `gpio` refuses them
  while it runs. Documented in the manual's *The board on Cyphal*, "Testing the wiring".
- **`tools/can_check.py`**, the computer's side, through a gs_usb adapter (candleLight,
  CANable): `listen`, `send`, `flood`, and `echo`, which sends numbered frames to a board
  running `can echo on` and checks that every one comes back, in order. `flush` empties the
  adapter after a python-can session, so that the next program to open it (cyphal_service)
  doesn't stall on a frame left for the computer.
- **A Cyphal node on CAN**. `link mode can` (and a restart) hands the host link's pins A and B
  to it. It gets its node-ID by plug-and-play (`uavcan.pnp.NodeIDAllocationData.1.0`, from any
  allocator on the bus, asked for again at every start and never saved) or from a fixed setting.
  It sends `uavcan.node.Heartbeat` once a second and answers `uavcan.node.GetInfo` with its name
  (`r2.dome.holo.front` unless changed), the firmware's version and commit, and a unique ID made
  from the chip's factory MAC address, so that an allocator gives the same node-ID across
  reflashes and settings wipes. With a fixed node-ID it listens 2 s for another node with it
  before going online; with plug-and-play it listens at least 1 s from its start (a heartbeat's
  longest period) before taking up an allocated node-ID, and doesn't if another node was heard on
  it in that time or in the last 3 s; and if it ever hears its node-ID from another node it stops
  sending, with one warning, until told to start again. The board keeps acknowledging the bus
  throughout.
  Documented in the manual's *The board on Cyphal* and *Host link: CAN*.
- **`cyphal`**: the node's state, node-ID, name, unique ID and plug-and-play pseudo-ID, the bus
  and its counters; `cyphal id` (a fixed node-ID, or `auto` for plug-and-play), `cyphal name`
  and `cyphal bitrate` (saved, and in a configuration backup), `cyphal restart` and
  `cyphal reset`.
- **Bus-off**: the node brings the controller back by itself, within a second; when bus-offs come
  back to back (a board that can't see its own bits on the bus: a silent transceiver, or VIO, RXD
  or TXD not connected), it waits 1, 2, 4, 8, 16 and then 30 s between recoveries until the bus
  has stayed up for a minute, and logs the 1st, 2nd, 4th, 8th, ... bus-off in a row, not one a
  second. `cyphal` shows the wait. The bus's state in `can` and `cyphal` is read from the error
  counters beside it (`error-warning` at 96, `error-passive` at 128); `bus errors` counts every
  error but lost arbitration, CRC errors on received frames included; and `can send` while
  bus-off, or `can recover` when not, refuses without an error line from the driver.
- **libcanard** v4.0.0 (`external/libcanard`) and the DSDL (`external/public_regulated_data_types`)
  as submodules. A build needs libcanard (`git submodule update --init`); the DSDL is needed only
  by `tools/gen_dsdl.py` and the hardware tests. The C headers for the data types are generated
  by `tools/gen_dsdl.py` (Nunavut) and committed, so the build needs no Python packages.
- **Tests**: `make unit-test` builds the node's unique ID, pseudo-ID, name rules, parsing and
  plug-and-play's request, reply, timing and in-use check with the computer's compiler;
  `make can-test` runs the hardware tests in `tests/hardware/` (pytest and pycyphal, through a
  gs_usb adapter, with pycyphal's allocator for plug-and-play; `make can-setup` makes their venv).
  The unit tests also cover the holo's Cyphal conversions (requests, Status's parts, Event, the file
  services' paths and errors, MediaInfo, the calibration registers' values), and the hardware tests the
  holo's ports (motions, light, setpoints, scenes, screen, ring, events, the lists against the HTTP API,
  the calibration registers, a clip under load), with a soak, `test_soak.py`, run with `--soak-minutes`.
- **The Cyphal node is managed the standard way.** Its settings are registers
  (`uavcan.register.Access` and `List`): `uavcan.node.id`, `r2.node.name` and
  `uavcan.can.bitrate`, the same saved settings as `cyphal id`, `name` and `bitrate`, and a pair
  for each of the holo's eleven ports, `uavcan.pub.status.id` and so on (the port number, saved,
  65535 for off) and `uavcan.pub.status.type` (its type, read-only), with the defaults of
  `r2_cyphal_dsdl`'s port table. A write is checked, saved and applies from the node's next start;
  one the board can't take leaves the register as it was, a port number another port has (the
  node's own included) among them. `uavcan.node.ExecuteCommand` restarts
  the board, resets it to the factory settings, all or nothing (the node's registers, touch reporting
  and the board's settings, keeping the scenes, files, servo calibration and drive policy, network and
  host link), identifies it (the ring blinks white and the screen shows the crosshair for 5 s, then
  both go back) or stores (nothing to do); other commands are refused. `uavcan.node.port.List` goes out every 10 s, with the ports the node
  really has. Documented in the manual's *The board on Cyphal* and *Cyphal interface*.
- **The holo on Cyphal** (`r2.holo.*`): `Motion` (service 100) starts any motion with every
  parameter, through the code the HTTP API's `POST /holo/motion` runs, so it is refused in the same words;
  its `off` lets the servos go limp and turns the ring off. `Light` (service 101) is the holo's light
  on the LED ring: off, on, pulse, flicker, blink at a level and a period, an optional colour, a time
  limit after which the ring goes back to what it was doing, and `leia` (a timed flicker at full and
  the holo to the centre, which answers whether the centre ran). `Axes` subscribed (subject 2303) is
  a stream of setpoints, each a direct move with no easing that stops the motion running; `Axes`
  published (2302) is where the board has put the axes, ten times a second while they move and once a
  second at rest. `Status` (2300) is the holo's motion and readiness, the last motion asked for, both
  axes, the ring with its timed mode, the screen (what shows, a clip's progress, the backlight) and the
  scene running to its end, within 250 ms of a change and once a second. The heartbeat's health is advisory while the holo can't move. `cyphal`
  counts the holo's requests and setpoints, and each port's messages queued for the bus with those a
  newer one replaced before they were.
- **The board's own features on Cyphal**: `Scene` (service 102) applies a saved scene by its slot or
  its name, or ends the one running, through the code `POST /scenes/apply` and `/scenes/end` run;
  `Screen` (103) shows a clip, an image, a colour, the crosshair or nothing and sets the backlight,
  through `POST /screen/show`'s, `DELETE /screen`'s and `PATCH /screen`'s; `Ring` (104) sets the LED
  ring as `PATCH /leds` does. Each is refused as the HTTP API refuses it, in the same words. `Event`
  (subject 2301) carries every clip and scene starting and ending, and touches while touch reporting
  is on, with the board's own sequence number, from the record the HTTP API's events come from: two
  at once both go, in order, and none is lost while that record still has it. Scene and Screen
  requests, and identify, are carried out one at a time by a second worker task (7.5 KB stack), so that
  a still being decoded never holds up the setpoints or Status. `cyphal` shows that worker, Event's
  queue, and the most frames the transmit queue has held. Documented in the manual's *The board on
  Cyphal* and *Cyphal interface*.
- **The lists and the calibration on Cyphal**: `ListScenes` (service 105) gives the saved scenes one
  a call with their slots, as `GET /scenes` lists them; the standard `uavcan.file.List` (406) and
  `uavcan.file.GetInfo` (405) give the board's files, as `fs ls` sees them (a directory's entries by
  index in name order, directories marked with a `/`; each one's size and time, read-only; with the
  board busy, List isn't answered, since its empty name would end a listing, and the client asks
  again); `MediaInfo` (106) describes a clip or an image from its header, through the lookup
  `GET /media/info` runs, and says whether the screen can show it and why not. The file services and
  MediaInfo run on the node's second worker, so that an upload holding the storage volume never holds
  up the setpoints. The holo's registers: `r2.holo.horizontal.endpoints_us` and
  `r2.holo.vertical.endpoints_us` (closed and open, `0 0` to clear), `….release` and `….settle_ms` (the
  drive policy), over the servos' own saved values, the ones `holo endpoints`, the web app's
  calibration and `PUT /servos/policy` change, written and read at once (by the node's worker); and
  `r2.touch.reporting`, the board's touch setting. Documented in the manual's *The board on Cyphal*
  and *Cyphal interface*.
- **The LED ring pulses and blinks**, and has the holo light's level and timed modes: `leds pulse`
  and `leds blink` with `-p <ms>`; `-l <pct>` (a share of the brightness) on every mode but `off`, and
  `-t <s>` (then back to what it was doing) on `colour`, `off`, `flicker`, `pulse` and `blink`. A
  timed mode and a blink no longer wake the board 50 to 100 times a second, and the ring's task is made
  once and kept. The web app's status card shows a level other than 100 and counts a timed mode's time
  left down. `PATCH /api/v1/leds` and a
  scene's `leds` take `pulse`, `blink`, `period_ms`, `level` and `time_s`; `GET /api/v1/leds` reports
  `period_ms`, `level`, a timed mode (`timed`: its time left and what follows) and `leia`. The host
  link's `LEDS` frame takes modes 5 (pulse) and 6 (blink) with an optional period after its flags,
  and the status block reports them. The web app's Show and Scenes pages offer pulse and blink, and the
  scene editor a pulse's or a blink's period, the level and a time. A
  scene that restores the ring puts back a timed mode for the rest of its time.
- **`cyphal register [<name> [<value>…]]`**: the same registers on the console, listed, read or
  written with the same checks. `cyphal` shows the services' counters and the worker that does
  their waiting (a register's write to flash, a restart, identify), so that the node's own task
  never waits.
- **`r2_cyphal_dsdl`** (`external/r2_cyphal_dsdl`, the holo's own Cyphal types and port table) as
  a submodule; `tools/gen_dsdl.py` generates the C headers from it and the standard types
  together, and the port defaults from its port table.
- **`tools/yakut_can.py`** (`make yakut`): Yakut on the board through a gs_usb adapter on macOS,
  opening and closing the adapter only while the board is quiet; registers only (it refuses
  `execute-command`, by every name Yakut's click version gives it, and calls to ExecuteCommand, during
  which a candleLight stopped passing frames on the bench, and what would leave the board never quiet
  as the adapter closes).

### Changed

- **The host link on CAN** (`link mode can`): `link` shows the pins and who has them ("for the
  Cyphal node"), and leaves out the protocol, baud rate, address, groups, events and counters,
  which CAN doesn't use; they stay saved for UART and RS485, and the console says so when one is
  changed. The web app's Host link card hides them while CAN is chosen. `GET /api/v1/link` is
  unchanged, and its description says which fields CAN ignores.
- **`gpio` names what holds a host link pin**: `host link UART TX`, `host link RS485 DE`, or
  "CAN TX, for the Cyphal node", in place of `host link (A)`. They are reserved
  from the moment the transport has started, and not at all if it failed to start: `link` then
  says `off: can didn't start (...)`. A Cyphal node whose start failed is restarted only with the
  board (`cyphal restart` says so), since it no longer has the pins.
- `can` refuses to start, stop, send, recover, dump or echo while the Cyphal node holds the
  controller; `can` alone still reports the bus. Before, `can stop` stopped the controller
  whoever had started it.
- The manual's Run buttons ask before running a `link` command that changes a setting (`link
  mode`, `pins`, `reset`, `proto`, `address`, `groups`, `events`).
- The CAN controller's sample point is 85 % of the bit, not the 87.5 % asked for: IDF 6.1's timing
  on the ESP32-S3 gives 20 time quanta at every bitrate, and 87.5 % is out of the controller's
  reach at 1 Mbit/s. The manual says so.
- `cyphal reset` puts the port numbers back to their defaults too.
- Looking at a clip's or an image's header (`GET /media/info`, Cyphal's MediaInfo, and the check before
  one is shown) takes its memory from PSRAM first: the GIF decoder's 24 KB, a clip's frame tables, a
  JPEG's bytes and libpng's state, up to 29 KB of internal RAM before, 4 to 9 KB now, which a playing
  clip's frames need. Playback itself is unchanged.
- **Touch reporting is one of the board's settings**: `GET /api/v1/settings` has `touch.reporting`,
  `PATCH` sets it (and turns touch on or off now), `DELETE` puts it back to its default, the console's
  `settings` shows it and `settings touch on|off` sets it, the web app's Settings page has a switch for
  it, and the Cyphal register `r2.touch.reporting` is the same setting. The board starts with touch
  reporting on if it says so, `CONFIG_BOARD_TOUCH_AUTOSTART` being only its default; `touch on` and
  `touch off` still change it until the next start without saving. It is in a configuration backup
  (NVS `holo_cfg`, `touch`).
- The Cyphal node's libcanard heap is 32 KB of PSRAM (was 16 KB), its transmit queue 160 frames
  (was 64) and its task's stack 3 KB (was 2.5 KB); a worker task with a 4.5 KB stack joins it at its
  first start.
- A message the Cyphal node publishes waits in its mailbox until the transmit queue has room for the
  whole of it, rather than being dropped.
- The hardware tests' console answers linenoise's terminal queries even when one arrives split
  between two reads of the port; one that went unanswered left the console with no prompt (seen
  three times in the tests).
- `leds` shows a pulse's or a blink's period, a level other than 100, and a timed mode's time left
  and what follows. `leds rainbow` keeps the ring's colour for the next mode that has one, rather than
  forgetting it.
- **`GET /api/v1/events` (the kept happenings as JSON) is sent in chunks** straight from the stored
  happenings, rather than parsed back into one tree and printed whole: at most a few KB of internal RAM
  where it took 16-18 KB with all 32 kept, which set the board's low-water mark while a clip played and
  the web app polled. The body is byte for byte the same; the reply has `Transfer-Encoding: chunked` in
  place of a `Content-Length`. (esp-console-kit `b8d24e2`.) A reader that stalls while happenings are made now has its
  connection closed, never a reply with one missing (`59f2670`, on the kit's main).
- **The video player's task is made once, at start**, and waits between plays, rather than being made
  for every clip, GIF and scene restore: its stack, now 4.5 KB (8 KB before; 1.8 KB is the least it was
  seen to leave free, in rapid replacement), is held for good, about 5 KB less internal RAM free at rest, and is no longer taken at
  each start, where it could be refused. A stop now also cuts short the wait for a GIF's next frame. Any
  show (a clip, an image or GIF, a colour, the calibration pattern) that finds the last playback still not
  stopped after 2 s is refused, with `what was playing did not stop within 2 s` over HTTP and Cyphal: nothing
  is drawn, the stop stands and the screen goes dark at once. Before, a clip would have started alongside
  it, and a colour or a still was drawn and then put out by the old playback's end.
- **A GIF takes its memory only once what was playing has stopped**: `image show` and `POST /screen/show`
  check it first, as before, but open it (its 24 KB decoder and its two DMA paint buffers) after the panel
  is taken, the paint buffers first. The cost: running out of memory now comes after the show is accepted,
  and ends the animation at once (`clip_ended` with `finished: false`, the screen dark, the reason on the
  console).
- esp-console-kit moved to its main (`2311a74`): radio and MAC handling for the dome panel's ESP32-P4,
  and `CMD_I2C_DEFAULT_PORT`; nothing changes on this board.

### Fixed

- A timed scene past its time, in the moment before it ends, read as one with no duration: `GET
  /scenes`'s `remaining_s` null, the host link's status block 0xFFFF, now 0.
- `GET /screen` could catch a clip that had just started half-recorded (no path, 0 frames). The
  player's record is now copied under a lock of its own, never held for longer than the copy.
- Touch could be started or stopped from two places at once (the console and the Cyphal register, or
  the settings over HTTP), making two drivers or deleting one under its task: start and stop now take
  turns.
- A 240-pixel-wide animated GIF shown over a playing clip or GIF was always refused (`it could not be
  drawn`): it was opened before the playback it replaced had given back its memory. See Changed.
- A clip or GIF that failed after it was accepted (the file not opened, no memory for its buffers, a
  broken GIF frame) was reported as played out: `clip_ended` said `finished: true`, and a scene that ends
  with its clip ran its `then` at once. Now `finished: false`, and the panel sleeps rather than keep what
  the failed clip replaced.
- A clip or GIF that ended at once could have its `clip_ended` sent before its `clip_started`. The start is
  now told before the player has the clip.
- A scene that ends with its clip could miss that end and run on until another scene or `POST
  /scenes/end`: a clip that failed or played out within moments of being shown had its end thrown away
  with those of the playback the scene replaced, and a scene showing the clip it replaced could be ended
  at once by the old playback's end. The scene now knows its own clip's playback, and a clip that fails
  ends its scene `by: replaced`, without its `then`. A scene's time running out as another scene is
  applied no longer lets its `then` put things back over the new scene. A colour or the calibration
  pattern refused because the playback before it did not stop now says so, rather than `the panel did
  not take it`.

## [1.1.0] - 2026-10-22

The board's settings can now be backed up to a small text file and restored, a backup never
holding a Wi-Fi network or password; the board can be told not to rejoin its network at start;
and an ESP-IDF 6.1 I2C driver bug that could corrupt memory after a timed-out read is patched.

A board on 1.0.x updates from the [Install page](https://davidiansmith.ca/holo-player-fw/1.1/install/flashing/)
(which keeps settings and clips), over Wi-Fi from the web app, or with `ota put` over the console.

### Added

- **Backing up the settings.** `nvs_export` writes everything the board remembers -- scenes,
  start-up settings, servo calibration, the host link, the web app's name and allowed sites --
  as one small text file, and `nvs_import` restores it: from the board's `backup/` directory, a
  URL, or the console. The whole file is checked before anything is written, the configuration
  as it was is saved first, and only what differs is written. A backup never holds a Wi-Fi
  network, the access point's passphrase or the web password, so it is safe to keep and share;
  `nvs_inventory` shows what is in and why anything is not. Documented in the manual's
  *Backing up the settings*.
- **`wifi auto [on|off]`**: whether the board rejoins its network when it starts. Off, it keeps
  the network but stays off it until asked (`wifi on`, a join, or BOOT), and the access point
  only comes on by itself at start when there is no network to rejoin.

### Changed

- When a motion is cut short because an axis refused it, `holo`'s status now says which motion,
  how long ago and, when one axis refused it, which -- `; <motion> stopped <n> s ago: <reason>
  (<axis>)` -- where it said only `; stopped: <reason>`. The manual's holo page now has a sample status and what each
  line means.

### Fixed

- An I2C read that timed out could make the next probe of the bus -- the IMU's at start,
  `i2cdetect`, a servo board's attach -- write a few bytes through a stale pointer in ESP-IDF
  6.1's I2C driver, corrupting memory somewhere at random. Every probe and read now goes through
  a guard that clears that state first. (esp-console-kit's `i2c_bus_guard`, found on the dome
  panel, where it caused panics.)
- Forgetting a network whose join had failed -- on the console, the Network page or
  `DELETE /api/v1/network/known` -- now stops the board retrying it. Before, it went on trying the
  forgotten network in the background every 30 seconds. (esp-console-kit `a535a49`.)

## [1.0.1] - 2026-09-28

Everything the console does can now be done over Wi-Fi: a web app on your network, or on the
board's own access point, handles updates, files, the screen, the LEDs, the holo and scenes, all
through an HTTP API that scripts can use too. A controller on a wire can run the board over UART
or RS485, and both hear what happens as it happens.

A board on 1.0.0 updates from the [Install page](https://davidiansmith.ca/holo-player-fw/1.0/install/flashing/)
(which keeps settings and clips) or with `ota put` over the console. From this release on,
updates can also come over Wi-Fi.

### Added

#### The board over Wi-Fi

- **A web app**, for phones and computers, at `http://holo-xxxx.local/` or the board's address
  (`web` on the console prints both):
  - **Status**: what's showing, the firmware, the network, and whether a newer release is out;
  - **Show**: play any clip or show any image on the board, a colour or the alignment crosshair,
    the backlight, and the LEDs' pattern, colour and brightness;
  - **Holo**: point the holo on a drag pad, start a behaviour, and calibrate each servo;
  - **Scenes**: save the screen, LEDs and holo together, apply one with a tap, and choose the one
    the board starts with;
  - **Files**: browse the board's storage, upload several files with progress, rename, move, copy,
    download and delete;
  - **Settings**, **Network** and **Update**.

  On a phone, Status, Show, Holo and Scenes are tabs and the rest are under More. Documented in
  [The board over Wi-Fi](https://davidiansmith.ca/holo-player-fw/1.0/use/web/).
- **Updates over Wi-Fi.** Upload a `.bin` (checked before it is written: a downgrade or another
  project's file is flagged), or have the board fetch the latest release itself, checked against
  the release's SHA-256. It streams into the slot that isn't running, so a broken image never
  becomes the boot image; the other slot stays one tap away. Also `ota pull <url|channel>` and
  `ota activate [slot]` on the console. Documented in [Updating the firmware](https://davidiansmith.ca/holo-player-fw/1.0/use/ota/).
- **An HTTP API** under `/api/v1`, which the web app is built on: files, the screen and media,
  the LEDs, the holo and servos, scenes, settings, the network, the web server's settings, and
  firmware updates. It is described in OpenAPI 3.1, served by each board at
  `/api/v1/openapi.json` and browsable in the [HTTP API explorer](https://davidiansmith.ca/holo-player-fw/1.0/reference/http-api-explorer/),
  with a [guide and `curl` recipes](https://davidiansmith.ca/holo-player-fw/1.0/reference/http-api/).
- **Events, not polling.** `GET /api/v1/events` is a stream of Server-Sent Events: the screen,
  LEDs, holo, running scene, scenes, settings, network and update session as each changes, and
  `clip_started`, `clip_ended`, `scene_started`, `scene_ended` and `touch` as they happen,
  numbered, with the last 32 replayed to a client that reconnects. While a clip plays or a scene
  runs, their state comes every 5 seconds too, to sync to. The web app follows it, so a change
  made anywhere (the console, a script, another browser, a host) shows on every open page at once.
- **The board's own access point.** `wifi ap on` starts `holo-xxxx` at `192.168.4.1` (WPA2, with a
  random passphrase it keeps), and a phone that joins is offered the board's page. If the board
  can't join a network within 20 seconds of starting, the access point comes on by itself for 5
  minutes. Pressing **BOOT** with the board off the network rejoins it, or turns the access point
  on for 5 minutes. Under "Wi-Fi fallback" in menuconfig.
- **Protection.** Pages on other sites can't drive the board from your browser. An optional
  password (`web password`, or the Settings page) guards every change, downloads and the web
  settings. A short list of trusted sites -- Swagger Editor, and this project's documentation --
  may call the API from a browser; `web cors` changes it.

#### Scenes

- **Scenes**: what the screen shows, what the LEDs do and how the holo moves, saved together by
  name and applied in one call, with one chosen to run at every start. Up to 16.
- **Scenes that end**: when their clip has played (once, or `loops` times) or after `duration_s`,
  and then (`then`) put everything back as it was -- a twitch twitches again -- or turn it all
  off. `scene end` or `POST /api/v1/scenes/end` ends one early.
- **Scene slots**: a number from 1 to 255 a scene can be applied by, for small controllers
  (`slot` in the scene, `scene slot` on the console, or the scene editor).
- **Settings**: the backlight, LED brightness and LED count the board starts with.
- **An LED flicker**: the colour flickering like a failing hologram (`leds flicker [<colour>]`).

#### The host link

- **A controller on a wire** -- an Arduino, an ESP32, a Raspberry Pi, a dome controller -- runs the
  board over UART, or RS485 through a transceiver, on P2's spare pins (GPIO21, 33 and 15).
  - **JSON lines**: the HTTP API one request a line (`POST /scenes/apply {"name":"cantina"}`,
    answered `200 {...}`), with tags, and on RS485 an address on every request and groups that
    act together.
  - **Native frames** for small hosts: a few bytes with a CRC (`SCENE 3`, `LEDS`, `HOLO`, a
    12-byte `STATUS`).
  - **Events**: pushed as they happen on UART, with no extra wire, or kept for the host to fetch,
    with a count of those waiting on every reply.

  Set up with `link` on the console, `PATCH /api/v1/link`, or the Settings page.
  `tools/hostlink.py` talks to it from a computer. Documented in [The host link](https://davidiansmith.ca/holo-player-fw/1.0/use/host-link/),
  a wiring page for [UART](https://davidiansmith.ca/holo-player-fw/1.0/connect/host-uart/) and [RS485](https://davidiansmith.ca/holo-player-fw/1.0/connect/host-rs485/), and the
  [protocol](https://davidiansmith.ca/holo-player-fw/1.0/reference/host-protocol/). I2C is described ahead of the firmware, to come.

#### The board in the browser

- **A documentation page that talks to the board** over Web Serial (desktop Chrome, Edge or
  Opera), with nothing to install: a file manager for `/data` (drop to upload, drag to move, every
  transfer checked by SHA-256), every command the board lists grouped by what it's for, and the
  console.
- **Run buttons throughout the documentation**: in those browsers, each board command in the pages
  has a ▶ that runs it on a connected board, output underneath. Commands that change something
  ask first. Connecting doesn't restart the board, and it works in the offline documentation too.

#### Console and tools

- `scene`, `settings`, `link`, `wifi scan`, `wifi ap`, `web` (`on|off`, `password`, `hostname`,
  `cors`), `ota pull` and `ota activate`.
- `make api-smoke HOST=...` calls a board's API, follows its event stream, and checks every reply
  against the OpenAPI description; `make api-validate` checks the description and its examples.

### Changed

- `video play` refuses a file it can't play at once, saying why, and leaves the screen alone.
  Before, it stopped what was showing and printed the reason from the player's task.
- The console takes commands of up to 16 words. `holo twitch -r 40 -i 1-3 -t 5` lost its last word.
- Board output in the documentation is in `text` code blocks, and bare code blocks hold only board
  commands, which `tools/check_command_docs.py` checks, so every ▶ runs a real command.

### Known issues

- On macOS, `fs_xfer.py put` fails through Apple's driver (`cu.usbmodem…`): 1 KB XMODEM blocks
  arrive corrupted. Use `cu.wchusbserial…`. The browser's file manager switches to 128-byte blocks
  there instead.
- RS485 has been tried through a plain UART adapter, but not yet on a bus with transceivers.

## [1.0.0] - 2026-09-23

### Added

- **Install from the browser.** The Install page in each release's documentation flashes that
  release onto the board over Web Serial (desktop Chrome, Edge or Opera), with no toolchain. It
  reads the board first: on one running Holo Player with the same partition layout it
  **updates**, keeping settings and clips; on a blank board or other firmware it does a **fresh
  install**; and a **complete overwrite** is always available behind a confirmation. Images are
  checked against SHA-256 before writing and MD5 after, and the boot log confirms the result.
  The offline documentation zip carries the same installer and firmware, served on localhost by
  its `serve.py` with no internet. esptool and building from source stay documented for other
  browsers. Documented in [Install](manual/install/flashing.md).
- **Every image a blank board needs, on the Release.** Besides the application, each Release
  now carries the bootloader, partition table and initial OTA data, so esptool can install a
  release without ESP-IDF.
- **GIFs, animated too.** `image show` takes a GIF. A one-frame GIF is a still; an animated one
  plays on the video player's task — so `video stop` and `video status` work on it — looping as
  the file says and leaving its last frame up when it stops. Frames are composited onto a canvas
  and only what changed is painted, so a typical 240×240 GIF costs a few milliseconds a frame.
  The decoder is [bitbank2/AnimatedGIF](https://github.com/bitbank2/AnimatedGIF), a new submodule:
  in an existing clone, run `git submodule update --init` before building. Documented in
  [Images](manual/use/images.md#animated-gifs).
- **Still images.** `image show <file>` puts a PNG or a baseline 4:2:0 JPEG on the panel, centred,
  where it stays until something else takes the screen; `image info <file>` describes one without
  showing it. JPEG stills reuse the clip player's decoder, and PNG is libpng, streamed row by row
  into the same 16-line blocks — so no whole image is held except for interlaced files, which
  Adam7 makes impossible to read a row at a time. Nothing that fails disturbs the screen: the
  file, its format and its size are all checked before the panel is touched. Documented in
  [Images](manual/use/images.md).
- **Screen alignment.** `screen calibration` (or `screen calib`) draws a centred crosshair with an
  up arrow at the crossing, for mounting the panel behind a dome's lens. The lines straddle the
  seam between pixels 119 and 120, so the crossing is the panel's true centre. Documented in
  [Mounting the screen](manual/connect/screen.md).

## [0.1.0] - 2026-09-20

First tagged release. Everything below already worked; this is the point it became a version
you can name, flash and report.

### Added

- **Video.** Motion-JPEG QuickTime playback on the round panel, on the clip's own timing, with
  `video play|stop|status|info|verify`. Frames decode 16 lines at a time into alternating DMA
  buffers so decoding and the SPI transfer overlap; a 240×240 clip runs at 30 fps with no late
  frames.
- **Screen.** `screen colour` and `screen clear`. The panel sleeps with its backlight off
  whenever nothing is showing, and a clip's first frame is never drawn to a dark screen.
- **NeoPixel ring.** `leds` with a solid colour, brightness, and the `wipe` and `rainbow`
  patterns, once or looped.
- **Holoprojector.** Two servos on MCPWM as `pan` and `tilt`, driven by the shared holo engine:
  `twitch`, `wag`, `nod`, `scan`, `circle`, `move`, `nudge`, `center`. Endpoints calibrate with
  `holo endpoints` and persist in NVS under the pin, so they follow the wiring. Servos stay limp
  until the first move.
- **Board.** GC9A01 LCD, CST816S touch and QMI8658 IMU on a shared I2C bus, with `lcd`, `touch`
  and `imu`.
- **Files and updates over the console.** An 11 MB LittleFS volume at `/data`, `fs` for working
  with it, and `ota put` for installing firmware over the serial console when the board cannot be
  reached by USB. Both transfer over XMODEM-1K with SHA-256 verification; the host side is
  `tools/fs_xfer.py` in esp-console-kit.
- **Documentation site** built from `manual/`, published per version.

[Unreleased]: https://github.com/daveismith/holo-player-fw/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/daveismith/holo-player-fw/releases/tag/v1.1.0
[1.0.1]: https://github.com/daveismith/holo-player-fw/releases/tag/v1.0.1
[1.0.0]: https://github.com/daveismith/holo-player-fw/releases/tag/v1.0.0
[0.1.0]: https://github.com/daveismith/holo-player-fw/releases/tag/v0.1.0
