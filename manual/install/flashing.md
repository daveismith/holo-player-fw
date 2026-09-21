# Build and flash

The firmware builds with **ESP-IDF 6.1** for the `esp32s3` target. There is no Arduino path and no
prebuilt binary to download for the first install: the bootloader has to go on over USB at least
once, after which the firmware can update itself over the console.

## 1. Get the source

The shared console commands live in a git submodule, so clone recursively:

```sh
git clone --recursive https://github.com/daveismith/holo-player-fw
```

In a clone that already exists:

```sh
git submodule update --init
```

Without the submodule the build fails at `main/CMakeLists.txt`, which requires its components by
name.

## 2. Build

```sh
idf.py build
```

`sdkconfig` is not committed. Everything the build needs is in `sdkconfig.defaults`, so the first
build writes a fresh `sdkconfig` from it — see [Configuration](../reference/config.md) for what is
set and why. The component manager restores `managed_components/` from `dependencies.lock` on the
first build, which needs network access.

## 3. Flash

```sh
idf.py -p /dev/cu.wchusbserial5B910448341 flash monitor
```

Use your own port: `/dev/cu.wchusbserial…` on macOS, `/dev/ttyUSB0` on Linux, `COM3` on Windows.

The board's USB-C port goes through a **CH343P** USB-UART bridge to UART0 at 115200 baud. Its
DTR/RTS lines drive the auto-download circuit, so flashing needs no button presses. The ESP32-S3's
native USB pins are not connected on this board, which is why every host-side tool here talks to a
serial port rather than USB.

!!! note "Flashing over USB always returns to the first slot"
    `idf.py flash` writes the application to `ota_0` and resets the boot selection. That is worth
    knowing if you have been updating over the console, which alternates between two slots —
    see [Updating over serial](../use/ota.md).

## 4. Check it

The console comes up on the same port the firmware was flashed over. At the prompt:

```
version
```

It reports the firmware's own version, when it was built, and the chip it is running on. A build
from a release tag reports that tag, such as `v0.1.0`; a build from an untagged working tree
reports the commit it came from, with `-dirty` appended if there were uncommitted changes.

```
help
```

lists every command. The screen stays off until something is shown on it — try
`screen colour red`, then `screen clear`.

## Updating later

Once the bootloader is on the board, new firmware can be installed over the console without USB
access, which matters when the board is mounted inside a droid. See
[Updating over serial](../use/ota.md).
