# Files on the board

Clips live on an **11 MB LittleFS volume** mounted at `/data`. It is formatted empty on first
boot, and filled over the serial console — there is no SD card and no USB mass storage.

## On the board

The `fs` command works on the volume; every path is relative to the mount point, so `leia.mov`
means `/data/leia.mov`.

```
fs ls
fs df
fs stat leia.mjpeg.mov
fs mkdir clips
fs rm old.mov
fs mv a.mov b.mov
fs cat notes.txt
fs hexdump leia.mjpeg.mov
fs sha256 leia.mjpeg.mov
fs bench
```

`fs df` reports how much of the volume is used; `fs bench` measures read and write throughput,
which is worth running once if playback is stuttering.

## From the host

`fs put` and `fs get` move bytes over the console itself, using XMODEM-1K. You could drive them by
hand, but the host-side tool does the framing, the baud switching and the verification:

```sh
python external/esp-console-kit/tools/fs_xfer.py -p /dev/cu.wchusbserial… put ~/clips/*.mov
```

!!! warning "Only one program can hold the serial port"
    Close `idf.py monitor` first. This is the single most common reason the tool appears to hang.

Other subcommands:

```sh
fs_xfer.py put -f --to clips data/leia.mjpeg.mov   # replace, into a directory on the board
fs_xfer.py get leia.mjpeg.mov /tmp/leia.mov        # download
fs_xfer.py sha256 leia.mjpeg.mov                   # hash it on the board
fs_xfer.py run "fs ls" "fs df"                     # run any console command
```

`-f` replaces a file that already exists; without it, an existing name is an error rather than a
silent overwrite. Uploads land as `<path>.part` and are renamed into place only once complete, so
an interrupted transfer cannot be mistaken for a good file.

## Verification

`put` sends each file's length up front, so the board stores exactly that many bytes — plain
XMODEM would otherwise pad the last block out with padding bytes. It then checks the SHA-256
**twice**: once against the bytes the board received, and once against the file read back from
flash. A clip that passes both is byte-identical to the one on your disk.

## Speed

| Direction | Baud | Measured |
|---|---|---|
| Upload | 460800 | 21 KB/s on a 100 KB file; 7–10 KB/s on 1.6–1.9 MB clips (2.5–4.5 minutes each) |
| Download | 230400 | about 21 KB/s |

The console returns to 115200 afterwards.

Those rates are a property of the host, not the protocol. They were measured on macOS with the
CH343P bridge and WCH's CH34x driver, where writing a whole 1029-byte block in one go loses data
between the driver and the bridge at 460800 and above; the tool works around it by draining each
512-byte piece, which costs about 35 ms a call. Raising the rate therefore does not help — 921600
managed only 7.5 KB/s. In the other direction, data coming *from* the board above 230400 loses
bytes whatever the block size, which is why downloads are slower than uploads.

On another host, try higher rates with `--xfer-baud` and `--get-baud`.
