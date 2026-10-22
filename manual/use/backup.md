# Backing up the settings

Everything the board remembers — its scenes and the one it starts with, the backlight and LED
settings, the servo calibration, the host link, the web app's name and allowed sites — lives in
its NVS partition, and a flash erase, a factory reset or a new firmware that changes how NVS is
laid out can lose all of it. A **backup** is that configuration as one small text file
(`.r2cfg`), which the board can write, check and restore by itself.

## What a backup never holds

**No credential.** The saved Wi-Fi networks and their passphrases, the access point's passphrase
and the web password are left out, as are the radio's calibration and other things the chip keeps
for itself. So a backup is safe to keep, copy and attach to a bug report, and a restored board
needs its Wi-Fi network entering again, with `wifi_save` or on the web app's Network page over
the access point.

To see exactly what a backup would hold, and why anything is left out:

```
nvs_inventory
nvs_inventory -x
```

```text
NAMESPACE       KEY             TYPE VERDICT
holo_cfg        backlight       u8   include
holo_cfg        scn0            str  include
hostlink        cfg             str  include
wifi_ap         pass            str  exclude  credential: the access point's name and passphrase (kit)
wifi_known      known           blob exclude  credential: the saved WiFi networks (kit)
...
13 entries in 7 namespaces: 5 in a backup, 8 excluded
```

The board's hostname (`web hostname`) and its host link address are in a backup, so a
replacement board restored from it answers to the same name and address. Setting up a *second*
board from one's backup (`--other-board`, below) copies them too: give it its own name and
address afterwards.

## Making one

`nvs_export` prints the file, or writes it to the volume's `backup/` directory:

```
nvs_export
nvs_export holo.r2cfg
```

```text
/data/backup/holo.r2cfg: 676 bytes, 5 entries (8 excluded), sha256 9395…
```

Copy it off the board with the file manager on [The board in the browser](board.md), or from a
computer:

```sh
python external/esp-console-kit/tools/fs_xfer.py get backup/holo.r2cfg ~/holo.r2cfg
```

The file names the board it came from (its MAC address), the firmware, and a CRC-32 over every
line, so a damaged or edited file is refused rather than half-applied.

## Restoring one

Put the file back on the board (the file manager, or `fs_xfer.py put`), look at what it would
change, then import it and restart. A bare name is looked for in `backup/`; a path starting `/`
is anywhere on the volume, which is the way after an erase, when `backup/` doesn't exist yet:

```
nvs_import -n /holo.r2cfg
nvs_import -r /holo.r2cfg
```

```text
nvs_import: /data/holo.r2cfg: 676 bytes
same     holo_cfg/backlight
change   holo_cfg/led_bright: 40 -> 100
same     holo_cfg/led_count
add      holo_cfg/scn0 str "{...}"
same     hostlink/cfg
5 entries: 3 same, 1 changed, 1 added, 0 retyped, 0 removed, 0 excluded
dry run: nothing written
```

- The whole file is checked before anything is written — the CRC, every line, the board it came
  from, the free space — and nothing at all is written if any of it fails.
- Only what differs is written, and what the board has that the file doesn't is kept (with
  `--replace`, erased, within the namespaces the file holds).
- Before writing, the board saves its configuration as it was to `backup/pre-import.r2cfg` (the
  one before that becomes `pre-import.1.r2cfg`), so an import can itself be undone.
- A file from another board is refused unless `--other-board` is given, and a file holding a
  setting a backup leaves out unless `--ignore-excluded`.
- **Restart afterwards** (`-r` does it). Several parts of the firmware keep their settings in
  memory and would otherwise write the old ones back.

The file can also come from a web server the board can reach:

```
nvs_import -n http://192.168.1.10:8000/holo.r2cfg
```

`nvs_import -` reads the file from the console itself: the board prints
`reading the file from stdin`, a script sends the file, and the board stops at its `# crc32`
line. That is for scripts on a computer holding the serial port; the pages of this site can't
send a file that way, so from them, upload it and import it by its path.
