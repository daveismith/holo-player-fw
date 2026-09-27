# Changelog

Notable changes to the holo player firmware. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[semantic versioning](https://semver.org/spec/v2.0.0.html).

The version is not written down anywhere in the source. ESP-IDF derives it from `git describe`,
so the release tag is the version baked into the image and reported by `version` and `ota`. See
[RELEASING.md](RELEASING.md).

## [Unreleased]

### Added

- **Updates over Wi-Fi, and a web app to do them from.** The board serves a small web app, for
  phones and computers, at `http://holo-xxxx.local/` or its address on your network (`web` on the
  console prints both). It has two pages to start with, built to take settings and clips later:
  - **Status**: the firmware, the network, and whether a newer release is out;
  - **Update**: both firmware slots, with a switch back to the other one; the latest release, which
    the board downloads itself from this documentation site, checked against the release's
    SHA-256; and uploading a `.bin`. The page reads the file first, and warns about a downgrade or
    a file for another project. Then it restarts into the new image and reports it running.

  An upload streams straight into the slot that isn't running, and takes about 15 s for 1.6 MB.
  As over serial, a wrong or broken image never becomes the boot image. Documented in
  [The board over Wi-Fi](manual/use/web.md) and [Updating the firmware](manual/use/ota.md).
- **An HTTP API**, which the web app uses and scripts can too, under `/api/v1`:
  - upload an image;
  - follow its state;
  - see which slot runs and boots;
  - switch slots;
  - have the board pull an image from a URL, a release manifest, or a release channel (`latest`);
  - check for an update without downloading it.

  It is described in OpenAPI 3.1, rendered on the documentation site
  ([HTTP API explorer](manual/reference/http-api-explorer.md)) and served by each board at
  `/api/v1/openapi.json`. A guide with `curl` recipes is at [HTTP API](manual/reference/http-api.md).
  `make docs-check` fails if the firmware's routes and the description drift apart.
- **The board's own access point**, for when there is no network: `wifi ap on` starts `holo-xxxx`
  (WPA2, with a random passphrase it keeps) at `192.168.4.1`, and a phone that joins is offered the
  board's page. The access point is off again after every restart.
- **An optional password** for changes over the web: `web password <password>`. Even without
  one, pages on other sites can't drive the board from your browser. Also `web on|off` and
  `web hostname`.
- **`ota pull <url|channel>` and `ota activate [slot]`** on the console: the same pull and switch,
  without a browser.

- **The board in the browser.** A new documentation page talks to a board running Holo Player
  over Web Serial (desktop Chrome, Edge or Opera), with no tools to install:
  - a file manager for `/data`: upload by dropping files, download, rename, delete, move by
    dragging onto a folder (after a confirmation), and play or show what is there, with every
    transfer checked by SHA-256 as `fs_xfer.py` checks it;
  - every command the board lists in `help`, in groups by what it is for (display, images, video,
    LEDs, …, and "Other" for any the page doesn't know), with an entry for each sub-command
    (`video play`, `video stop`, …) to edit and run, and a picker for the board's files;
  - the console, with everything the board prints.
- **Run buttons throughout the documentation.** In those browsers, each board command in the
  pages has a ▶ that runs it on a connected board, with the output underneath. The connection
  shows in the header on every page and follows you from page to page. Commands that change
  something ask first. "Files on the board" embeds the file manager.
- Connecting doesn't restart the board, and it works in the offline documentation through
  `serve.py`, like the installer.

### Changed

- Board output in the documentation is in `text` code blocks. Bare code blocks hold only board
  commands, and `tools/check_command_docs.py` checks that, so every ▶ runs a real command.

### Known issues

- On macOS, `fs_xfer.py put` fails through Apple's driver (`cu.usbmodem…`): 1 KB XMODEM blocks
  arrive corrupted. Use `cu.wchusbserial…`. The browser's file manager switches to 128-byte blocks
  there instead.

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

[Unreleased]: https://github.com/daveismith/holo-player-fw/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/daveismith/holo-player-fw/releases/tag/v1.0.0
[0.1.0]: https://github.com/daveismith/holo-player-fw/releases/tag/v0.1.0
