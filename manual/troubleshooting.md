# Troubleshooting

**The screen is dark and nothing seems wrong.**
: That is the normal resting state. The panel sleeps with its backlight off whenever nothing is
  showing — at boot, after `screen clear`, and when a clip ends. Try `screen colour red`.

**A transparent PNG shows white where it should be transparent.**
: It is not transparent any more — the panel has no alpha, so transparency is composited over
  black when the file is decoded. White means the transparent pixels really are white in the
  file, with the alpha channel hiding them. Export with the background you want
  ([details](use/images.md#preparing-a-file)).

**A JPEG that opens everywhere else will not show on the board.**
: The decoder reads baseline 4:2:0 only. `only baseline JPEG is supported` means the file is
  progressive; `unsupported colour sampling` means it is 4:4:4, which is what ffmpeg writes by
  default from a still RGB image; `bad JPEG data` on a file that is not corrupt means 4:2:2.
  All three re-encode the same way:
  `ffmpeg -i in.jpg -c:v mjpeg -q:v 3 -pix_fmt yuvj420p out.jpg`
  ([details](use/images.md#preparing-a-file)). A PNG avoids the question entirely.

**`image show` refuses a picture that looks the right shape.**
: It is larger than 240×240. Images are not scaled or cropped to fit — the framing stays yours —
  so resize it on the host first ([details](use/images.md#preparing-a-file)).

**A GIF plays slower than it does in a browser.**
: If its frames ask for 10 ms or less, the board shows each for 100 ms — but so do Chrome and
  Firefox, so check it in one of those rather than an image viewer. Otherwise `video status` while
  it plays will show frames running late: it changes too much of a 240×240 panel per frame. The
  ceiling is about 22 fps when every pixel changes; lower its frame rate or its size
  ([details](use/images.md#what-it-costs)).

**`image: … truncated: it ends before the GIF trailer`.**
: The file was cut short, almost always in transfer. Send it again with `fs_xfer.py put`, which
  verifies what arrives ([details](use/files.md)).

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

**Installing over USB put back an old version.**
: The browser installer, esptool and `idf.py flash` all write `ota_0` and boot it, whichever slot
  the console-installed firmware was using.

**The Install page says the browser can't talk to serial ports.**
: Web Serial is only in desktop Chrome, Edge and Opera — not Firefox, Safari, or any phone. Use
  one of those, or install with esptool ([details](install/flashing.md#install-the-firmware)).

**The Install page says there is no firmware to install.**
: Only a release's documentation carries firmware; the development version (`dev`) and previews
  do not. Switch to a release with the version selector at the top of the page.

**Opened from the documentation zip, the Install page says the installer can't run.**
: Browsers won't use a serial port from a page opened as a file. Run `python3 serve.py` in the
  unzipped folder; it serves the pages on `localhost` with no internet needed
  ([details](install/flashing.md#install-the-firmware)).

**The browser lists no serial port for the board.**
: Try another USB-C cable — some carry only power. On Windows and older macOS, the CH343P needs
  WCH's driver. The board appears as `cu.wchusbserial…` or `cu.usbmodem…` on macOS and as
  *USB-SERIAL CH343* on Windows.

**The installer says the port is in use.**
: Another program has it: `idf.py monitor`, a serial terminal, `fs_xfer.py`, or another browser
  tab. Close it and connect again.

**The installer says the board didn't answer.**
: Its reset into the bootloader didn't take. Hold **BOOT**, tap **RESET**, let go of BOOT, and
  connect again.

**Writing fails part way through.**
: Install again; the board may not start until an install completes. If it keeps failing, lower
  the baud rate under **Advanced**, and avoid USB hubs.

**The installer won't offer to update: the partition layout changed.**
: The release divides the flash up differently from the firmware on the board, so the settings
  and clips it would keep could be misread. Copy off anything you want to keep with `fs_xfer.py`,
  install with **Complete overwrite**, and put them back.
