# Install

The firmware goes onto the board over its USB-C port. There are three ways to do it, and all
three write the same four images: the bootloader, the partition table, the boot selection and the
application.

- **In the browser**: nothing to install. Needs desktop Chrome, Edge or Opera.
- **With esptool**: works in any browser and on any OS, with no ESP-IDF, using the images from a
  release.
- **From source**: build it yourself with ESP-IDF, for changing the firmware.

On a board that already runs Holo Player, each way can either **update** it or **overwrite it
completely**:

| | Settings (NVS) | Clips and images (`/data`) |
|---|---|---|
| **Update** | kept | kept |
| **Fresh install / complete overwrite** | erased | erased |

An update keeps them because it writes nothing outside the four images. A fresh install erases
the whole flash first. That is the right choice for a board that is blank or runs something
else, and the fix for a board in a state you would rather not keep.

## Install the firmware

=== "In the browser"

    <div id="holo-installer" class="hi-notice" markdown>
    **The installer can't run on this page.** Opened from the documentation zip? Browsers won't
    run it from a file: run `python3 serve.py` in the unzipped folder, as described below.
    Otherwise it needs JavaScript. Or use one of the other tabs.
    </div>

    1. Connect the board's USB-C port to the computer, then press **Connect to the board** and
       pick its port. The bridge is a CH343P: on macOS it appears as `cu.wchusbserial…` (or
       `cu.usbmodem…`), on Windows as a COM port named *USB-SERIAL CH343*. If nothing is listed,
       see [Troubleshooting](../troubleshooting.md).
    2. The installer reads the board and says what it found: the Holo Player version in each
       firmware slot and which one boots, other firmware, or a blank flash. Nothing is written
       yet.
    3. Choose what to do. The installer preselects the sensible choice.

        **Update: keep settings and clips** is the default when the board runs Holo Player. It
        is unavailable when this release divides the flash up differently from the firmware on
        the board, since kept data could then be misread. It warns when the release is older
        than what is installed.

        **Fresh install** is the default for a blank board or other firmware. It is called
        **Complete overwrite** when the board runs Holo Player, and then asks you to confirm,
        because it deletes every clip and setting.
    4. Press **Install**. Every image is checked against its SHA-256 before anything is
       written, and every region is read back and checked after. Erasing the whole flash takes
       about 30 seconds; writing takes about 30 more at the default speed.
    5. The installer restarts the board and shows its boot messages. When the board reports
       the version just installed, it's done, and the port is free for a terminal.

    If writing fails part way, the board may not start until an install completes. Connect and
    install again. Setting a lower baud rate under **Advanced** helps with a poor cable or
    hub.

    ??? note "Installing without internet access"
        The documentation zip attached to each [release][releases] contains the same installer
        and firmware. A browser won't use a serial port from a page opened as a file, so unzip
        it and run the small server inside:

        ```sh
        python3 serve.py
        ```

        It serves the folder on `localhost` only, fetches nothing from the internet, and opens
        this page, where the installer then works. Stop it with Ctrl-C. To host the
        documentation on an internal web server instead, serve it over HTTPS: browsers only
        allow serial ports on secure pages.

=== "With esptool"

    For Firefox, Safari, or anywhere else the browser installer can't run. You need Python and
    [esptool](https://docs.espressif.com/projects/esptool/):

    ```sh
    pip install esptool
    ```

    From the [release][releases] you want, download the four images and `SHA256SUMS`.
    Replace `vX.Y.Z` in the commands below with its version. The documentation zip has the
    same files in its `firmware/` folder. Then check that the download is intact:

    ```sh
    sha256sum --ignore-missing -c SHA256SUMS           # Linux
    shasum -a 256 --ignore-missing -c SHA256SUMS       # macOS
    ```

    **Update**, keeping settings and clips:

    ```sh
    esptool --chip esp32s3 -p PORT -b 460800 write-flash \
        0x0     holo-player-fw-vX.Y.Z-bootloader.bin \
        0x8000  holo-player-fw-vX.Y.Z-partition-table.bin \
        0xe000  holo-player-fw-vX.Y.Z-ota-data-initial.bin \
        0x10000 holo-player-fw-vX.Y.Z.bin
    ```

    **Fresh install or complete overwrite**: erase first, then run the same command.

    ```sh
    esptool --chip esp32s3 -p PORT erase-flash
    ```

    `PORT` is `/dev/cu.wchusbserial…` on macOS, `/dev/ttyUSB0` on Linux, `COM3` on Windows.
    Only update a board whose partition layout is the release's. When a release changes it,
    its notes say so, and a fresh install is needed.

=== "From source"

    The firmware builds with **ESP-IDF 6.1** for the `esp32s3` target. There is no Arduino
    path.

    **1. Get the source.** Two things live in git submodules — the shared console commands,
    and the GIF decoder — so clone recursively:

    ```sh
    git clone --recursive https://github.com/daveismith/holo-player-fw
    ```

    In a clone that already exists, and again after pulling a change that adds one:

    ```sh
    git submodule update --init
    ```

    Without them the build fails: at `main/CMakeLists.txt`, which requires the console
    components by name, or at `components/animatedgif`, whose sources are the other
    submodule.

    **2. Build.**

    ```sh
    idf.py build
    ```

    `sdkconfig` is not committed. Everything the build needs is in `sdkconfig.defaults`, so the
    first build writes a fresh `sdkconfig` from it — see
    [Configuration](../reference/config.md) for what is set and why. The component manager
    restores `managed_components/` from `dependencies.lock` on the first build, which needs
    network access.

    **3. Flash.** This is an update: settings and clips are kept.

    ```sh
    idf.py -p /dev/cu.wchusbserial5B910448341 flash monitor
    ```

    For a fresh install or complete overwrite, add `erase-flash` before `flash`.

    Use your own port: `/dev/cu.wchusbserial…` on macOS, `/dev/ttyUSB0` on Linux, `COM3` on
    Windows.

The board's USB-C port goes through a **CH343P** USB-UART bridge to UART0 at 115200 baud. Its
DTR/RTS lines drive the auto-download circuit, so none of these needs button presses. The
ESP32-S3's native USB pins are not connected on this board, which is why every tool here talks to
a serial port rather than USB.

!!! note "Installing over USB always returns to the first slot"
    All three ways write the application to `ota_0` and reset the boot selection. That is worth
    knowing if you have been updating over the console, which alternates between two slots —
    see [Updating over serial](../use/ota.md).

## Check it

The console comes up on the same port the firmware was installed over, at 115200 baud. The
browser installer lets go of the port once it is done; only one program can hold it at a time. At
the prompt:

```
version
```

It reports the firmware's own version, when it was built, and the chip it is running on. A
release reports its tag, such as `v0.1.0`; a build from an untagged working tree reports the
commit it came from, with `-dirty` appended if there were uncommitted changes.

```
help
```

lists every command. The screen stays off until something is shown on it — try
`screen colour red`, then `screen clear`.

## Line the screen up

Before the panel is fixed behind a dome's lens, put up the alignment pattern:

```
screen calibration
```

A crosshair with an arrow pointing up, for centring the panel in the lens opening and getting its
rotation right. Every clip afterwards is drawn on those axes, so this is worth doing while the
board is still loose — see [Mounting the screen](../connect/screen.md).

## Updating later

Once the bootloader is on the board, new firmware can be installed over the console without USB
access, which matters when the board is mounted inside a droid. See
[Updating over serial](../use/ota.md).

[releases]: https://github.com/daveismith/holo-player-fw/releases
