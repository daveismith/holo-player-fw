# Updating the firmware

Once the board is installed, a new version of the firmware can go on in two ways, neither of which
needs the board's buttons or its auto-reset circuit:

- **[Over Wi-Fi](#updating-over-wi-fi)**, from a phone or a computer: the easy way once the board is
  on your network or you have joined its own access point.
- **[Over serial](#updating-over-serial)**, through the console on the USB-C port: when there is no
  Wi-Fi.

Either way the new image goes into the application slot that is **not** running, and the board
checks the whole of it before it can boot. [When it goes wrong](#when-it-goes-wrong), the image
that was running still boots.

## Updating over Wi-Fi

Open the board's web app (see [The board over Wi-Fi](web.md) for how to reach it) and go to
**Update**. There you can:

- **Install the latest release.** The page shows the newest release next to what the board runs.
  **Download and install** has the board fetch the release from this site itself, checked against
  the release's SHA-256.
- **Upload a file.** Choose `holo-player-fw-vX.Y.Z.bin` from a release, or `build/holo-player-fw.bin`
  from your own build. Before sending, the page reads the file and says which firmware and version
  it is. It warns about a downgrade, or about a file for another project. The bootloader and
  partition-table files from a release are not firmware, and the page says so.
- **Switch slots.** The other slot still holds the previous firmware. **Switch to this** restarts
  into it.

After an upload or download, the new firmware is **staged**: checked and ready, but not yet what
the board boots. **Restart into it** makes it the boot image and restarts. The page then waits for
the board to come back and reports what it is running. Leave **Restart into it once the board has
checked it** ticked, and an upload does all of this in one go.

A 1.6 MB image uploads in about 15 seconds over Wi-Fi, a third of the time it takes over serial.

### From a script

The web app is a front end for the board's [HTTP API](../reference/http-api.md), so the same steps
automate. Upload, activate and restart in one call:

```sh
curl -T build/holo-player-fw.bin "http://holo-2db0.local/api/v1/ota/image?activate=1&reboot=1"
```

Or have the board fetch the latest release, and install it only once it is staged:

```sh
curl -X POST -H "Content-Type: application/json" -d '{"channel":"latest"}' http://holo-2db0.local/api/v1/ota/pull
curl http://holo-2db0.local/api/v1/ota/image          # until "state" is "staged"
curl -X POST -H "Content-Type: application/json" -d '{}' http://holo-2db0.local/api/v1/ota/activate
```

### From the console

The board can fetch an update from its console too: a release channel, a release's manifest, or an
image's URL. It downloads the image, checks it, makes it the boot image and restarts, as `ota put`
does:

```
ota pull latest
```

| Flag | Does |
|---|---|
| `-s` | Stage it only; `ota activate` makes it the boot image later |
| `-n` | Make it the boot image, but do not restart |
| `-f` | Accept an image built for another project |
| `--sha256 <hex>` | Refuse the image unless it has this SHA-256 |

`ota activate` makes the staged image the boot image and restarts. `ota activate ota_1` does the
same for the image already in a slot, which is how to go back to the firmware before.

## Updating over serial

[Installing over USB](../install/flashing.md) needs the USB-C port and its auto-reset circuit. Once the board is mounted inside a
droid that may not be reachable, so the firmware can also replace itself through the console it is
already talking on.

```sh
python external/esp-console-kit/tools/fs_xfer.py -p <port> ota build/holo-player-fw.bin
```

A 1.26 MB image takes about 44 seconds. As with any host-side tool here, **close `idf.py monitor`
first** — only one program can hold the serial port.

### What happens

The flash carries two application slots, `ota_0` and `ota_1`, of 2.25 MB each.

1. The board erases the slot that **is not** running and receives the image over XMODEM-1K at
   460800 baud, writing each block as it arrives.
2. ESP-IDF checks the complete image — its header, that it was built for this chip, and its
   SHA-256 — *before* it is allowed to become the boot image.
3. The board makes that slot the boot image and restarts into it.
4. The tool waits for the console to come back and confirms that the new slot is the one running.

### When it goes wrong

A failed transfer, a truncated image, or an image built for something else changes nothing: the
image that was running still boots. There is no state in which the board is left with no working
firmware, because the slot being written is never the slot being run.

### Rollback

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is on, so a newly installed image boots **on trial**. It
confirms itself once the application is up and the console is running. A reset before that point
boots the previous image instead.

!!! note "Rollback lives in the bootloader"
    The trial-and-confirm logic is part of the bootloader, and the bootloader is only written by
    [installing over USB](../install/flashing.md) — from the browser, with esptool, or with
    `idf.py flash`. A board that has never been installed over USB with this firmware does not
    have it.

### Checking the slots

```
ota
```

lists both application slots: the partition label, where it is, how big it is, which one is
running and which boots next, and for each one the project name, **version** and build date of the
image it holds, plus its state.

This is where the release version becomes visible on the board:

```text
ota_0    0x010000  2304 KB  running, boots    holo-player-fw v0.1.0, built Sep 20 2026 14:02:11
```

An image built from an untagged tree reports the commit it came from instead of a tag. See
[RELEASING.md](https://github.com/daveismith/holo-player-fw/blob/main/RELEASING.md) for how the
version gets there.

### Options

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

### Going back

[Installing over USB](../install/flashing.md) always writes to `ota_0` and boots it, whichever
slot was running before — whether from the browser, with esptool, or with `idf.py flash`.
That is the way back if an update leaves the board in a state you would rather leave behind.
