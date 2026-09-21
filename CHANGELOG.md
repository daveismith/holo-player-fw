# Changelog

Notable changes to the holo player firmware. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[semantic versioning](https://semver.org/spec/v2.0.0.html).

The version is not written down anywhere in the source. ESP-IDF derives it from `git describe`,
so the release tag is the version baked into the image and reported by `version` and `ota`. See
[RELEASING.md](RELEASING.md).

## [Unreleased]

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

[Unreleased]: https://github.com/daveismith/holo-player-fw/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/daveismith/holo-player-fw/releases/tag/v0.1.0
