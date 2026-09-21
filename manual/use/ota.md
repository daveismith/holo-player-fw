# Updating over serial

`idf.py flash` needs the USB-C port and its auto-reset circuit. Once the board is mounted inside a
droid that may not be reachable, so the firmware can also replace itself through the console it is
already talking on.

```sh
python external/esp-console-kit/tools/fs_xfer.py -p <port> ota build/holo-player-fw.bin
```

A 1.26 MB image takes about 44 seconds. As with any host-side tool here, **close `idf.py monitor`
first** — only one program can hold the serial port.

## What happens

The flash carries two application slots, `ota_0` and `ota_1`, of 2.25 MB each.

1. The board erases the slot that **is not** running and receives the image over XMODEM-1K at
   460800 baud, writing each block as it arrives.
2. ESP-IDF checks the complete image — its header, that it was built for this chip, and its
   SHA-256 — *before* it is allowed to become the boot image.
3. The board makes that slot the boot image and restarts into it.
4. The tool waits for the console to come back and confirms that the new slot is the one running.

## When it goes wrong

A failed transfer, a truncated image, or an image built for something else changes nothing: the
image that was running still boots. There is no state in which the board is left with no working
firmware, because the slot being written is never the slot being run.

## Rollback

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is on, so a newly installed image boots **on trial**. It
confirms itself once the application is up and the console is running. A reset before that point
boots the previous image instead.

!!! note "Rollback lives in the bootloader"
    The trial-and-confirm logic is part of the bootloader, and the bootloader is only written by
    `idf.py flash` over USB. A board that has never been flashed over USB with this firmware does
    not have it.

## Checking the slots

```
ota
```

lists both application slots: the partition label, where it is, how big it is, which one is
running and which boots next, and for each one the project name, **version** and build date of the
image it holds, plus its state.

This is where the release version becomes visible on the board:

```
ota_0    0x010000  2304 KB  running, boots    holo-player-fw v0.1.0, built Sep 20 2026 14:02:11
```

An image built from an untagged tree reports the commit it came from instead of a tag. See
[RELEASING.md](https://github.com/daveismith/holo-player-fw/blob/main/RELEASING.md) for how the
version gets there.

## Options

`ota put` is the on-board half, and `fs_xfer.py ota` drives it. Driving it by hand:

```
ota put [-b baud] [-n] [-d] <size>
```

| Flag | Does |
|---|---|
| `-b <baud>` | Transfer at this rate instead of the default |
| `-n` | Make the image the boot image, but do not restart |
| `-d` | A link test: receive and hash the image, write nothing |

`-d` is the useful one when a transfer is failing and you want to know whether the problem is the
link or the flash. `fs_xfer.py ota --dry-run` does the same from the host.

## Going back

`idf.py flash` over USB always writes to `ota_0` and boots it, whichever slot was running before.
That is the way back if an update leaves the board in a state you would rather leave behind.
