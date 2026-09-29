# Changelog

Notable changes to the holo player firmware. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[semantic versioning](https://semver.org/spec/v2.0.0.html).

The version is not written down anywhere in the source. ESP-IDF derives it from `git describe`,
so the release tag is the version baked into the image and reported by `version` and `ota`. See
[RELEASING.md](RELEASING.md).

## [Unreleased]

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

[Unreleased]: https://github.com/daveismith/holo-player-fw/compare/v1.0.1...HEAD
[1.0.1]: https://github.com/daveismith/holo-player-fw/releases/tag/v1.0.1
[1.0.0]: https://github.com/daveismith/holo-player-fw/releases/tag/v1.0.0
[0.1.0]: https://github.com/daveismith/holo-player-fw/releases/tag/v0.1.0
