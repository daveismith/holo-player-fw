# Troubleshooting

**The screen is dark and nothing seems wrong.**
: That is the normal resting state. The panel sleeps with its backlight off whenever nothing is
  showing — at boot, after `screen clear`, and when a clip ends. Try `screen colour red`.

**Every LED shows white, whatever colour I send — including `leds off`.**
: The data rate is set to 400 kHz, which is for WS2811 strips. A WS2812 reads a 400 kHz "0" as a
  "1", so every frame comes out white, including the one meant to turn the strip off. Set 800 kHz
  in menuconfig ([details](connect/leds.md#data-rate)).

**The first LED flickers or shows the wrong colour; the rest are fine.**
: A 3.3 V data line into a 5 V-powered strip, which is marginal by design. Add a 74AHCT1G125
  buffer powered from the strip's +5 V between GPIO16 and DIN
  ([details](connect/leds.md#data-level)).

**The board resets or browns out when the servos move.**
: Servo power is coming from the board. A small servo draws 0.5–1 A starting or stalling. Give
  the servos their own 5 V supply and share only ground
  ([details](connect/servos.md#power)).

**`holo wag` nods instead of shaking its head.**
: The two servos are swapped relative to the default. Change *Which servo pans* in menuconfig
  rather than rewiring ([details](connect/servos.md#which-servo-is-which)).

**One axis moves the wrong way.**
: Save its endpoints reversed — closed above open turns an axis round:
  `holo endpoints v 1950 1300` ([details](use/holo.md#calibrating-the-travel)).

**The servos are limp and ignore me.**
: They are limp until the first move, and after `holo off` or `servo_off all`. Any motion command
  drives them again.

**`holo led` does nothing.**
: There is no light on this board. The holo is registered with its light unset, so `holo led` and
  the light half of `holo leia` have nothing to drive. The NeoPixel ring is separate, and is
  driven by [`leds`](use/leds.md).

**A 240×240 clip stutters, but a 120×120 one is fine.**
: Check the dimensions are both multiples of 8. If they are not, the player falls back to decoding
  whole frames, which roughly doubles the per-frame cost at 240×240
  ([details](use/video.md#decoding-modes)).

**Playback is occasionally late for no obvious reason.**
: Something wrote to flash. A flash write pauses both cores while the cache is off, which can make
  a frame late. Saving a Wi-Fi network mid-clip will do it
  ([details](reference/video-internals.md#flash-writes-and-late-frames)).

**`fs_xfer.py` hangs, or the console does not answer.**
: `idf.py monitor` is still holding the serial port. Only one program can have it at a time.

**Downloads from the board are corrupt, but uploads are fine.**
: On macOS, WCH's CH34x driver loses data coming from the board above 230400 baud, whatever the
  block size. That is why downloads default to 230400. Try a lower `--get-baud`
  ([details](use/files.md#speed)).

**`i2cdetect` does not show the touch controller at `0x15`.**
: A CST816S only answers on I2C while the screen is being touched. Hold a finger on the glass and
  run it again.

**`imu id` reports an unexpected address.**
: The QMI8658's SA0 is tied low, but Waveshare's own headers disagree about which address that
  gives, so both `0x6B` and `0x6A` are probed. Either is normal.

**`version` reports a commit hash instead of a version number.**
: The firmware was built from an untagged commit, which is what a development build looks like.
  A `-dirty` suffix means there were uncommitted changes. Release builds report their tag
  ([details](use/ota.md#checking-the-slots)).

**An update over serial finished, but the board still runs the old firmware.**
: If the new image was installed with `ota put -n` it was made the boot image without restarting.
  Otherwise, a reset before the new image confirmed itself rolls back to the previous one by
  design ([details](use/ota.md#rollback)).

**`idf.py flash` put back an old version.**
: Flashing over USB always writes `ota_0` and boots it, whichever slot the console-installed
  firmware was using.
