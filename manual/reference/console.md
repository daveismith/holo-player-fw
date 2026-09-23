# Console commands

The console is the board's whole interface. It runs on UART0 at 115200 baud, reached over the
USB-C port. Type `help` for the full list with hints; this page is the annotated version.

Command history is kept in `/data/history.txt` and survives a restart.

!!! note "A colour is written the same way everywhere"
    `screen colour` and `leds colour` both take a name (`red`, `orange`, …), `#RRGGBB` or bare
    `RRGGBB`, or three decimal values as `R,G,B` or `R G B`. `screen colour` additionally accepts
    a raw RGB565 value written as `0x…`.

## The board

| Command | Does |
|---|---|
| `screen [colour <c> \| calibration \| clear]` | Show a solid colour or the alignment crosshair, or clear the screen. Alone, reports what is showing, including a clip or an image. The screen is off — panel asleep, backlight off — whenever nothing is |
| `lcd [bl <0-100> \| bench [frames]]` | LCD hardware: power state, backlight level while on, and a fill-rate benchmark |
| `touch [on\|off\|status]` | CST816S touch reporting, off at boot. Prints down/move/up with x,y |
| `imu [-r <hz>] [-n <count>] \| imu id` | Streams accelerometer (g), gyro (dps) and temperature until a key is pressed. `imu id` reports the part and the address it answered on |

`screen calibration` (or `screen calib`) draws a centred crosshair with an arrow pointing up, for
lining the panel up behind a dome's lens — see
[Mounting the screen](../connect/screen.md).

`touch` is only present when `BOARD_TOUCH_ENABLE` is set, which it is by default. See
[Configuration](config.md).

## Images

| Command | Does |
|---|---|
| `image show <file>` | Show a PNG, baseline JPEG or GIF, centred. It stays until something else takes the screen. An animated GIF plays, looping as the file says, and leaves its last frame up if it stops |
| `image info <file>` | What the file is — format, size, bit depth; for a GIF, frames, loops and length — without decoding it or touching the panel |

An image larger than 240×240 is refused rather than scaled. Transparency is composited over
black. An animated GIF plays on the video player's task, so `video stop` and `video status` work
on it. Full detail in [Images](../use/images.md).

## Video

| Command | Does |
|---|---|
| `video play <file> [loop] [frame]` | Play a Motion-JPEG QuickTime clip, centred, on the clip's own timing. `loop` repeats it; `frame` forces whole-frame decoding |
| `video stop` | Stop a clip or an animated GIF, and turn the screen off |
| `video status` | Per-frame read, decode, decode-plus-draw and paint times, for a clip or an animated GIF |
| `video info <file>` | What the file contains, without playing it |
| `video verify <file> [step]` | Decode sample frames both ways and compare them byte for byte |

Full detail in [Video clips](../use/video.md).

## LEDs

| Command | Does |
|---|---|
| `leds` | What the strip is showing, plus the hardware it is driving |
| `leds colour <c>` | A solid colour |
| `leds off` | Off |
| `leds wipe [<c>] [loop]` | Each LED to the colour in turn, 250 ms apart. White if no colour given |
| `leds rainbow [loop]` | The colour wheel five times round the ring in 12.8 s |
| `leds bright <1-100>` | Brightness, applied live |

Full detail in [LED patterns](../use/leds.md).

## The holoprojector

| Command | Does |
|---|---|
| `holo` | Status: the light, the current motion, and both axes |
| `holo <verb> …` | `center`, `move`, `nudge`, `twitch`, `wag`, `nod`, `scan`, `circle`, `stop`, `off`, `endpoints`. `holo help` lists the options |
| `servo_list [-o wide]` | Both servos, with their absolute and working ranges |
| `servo_move <ident> <pos> [-p]` | Move one servo, by percentage or raw pulse |
| `servo_sweep <ident>… [-t n]` | Sweep one or more servos |
| `servo_config <ident> <min> <max> [-i]` | Set a servo's working range, with optional invert |
| `servo_off <ident>\|all` | Stop driving a servo, so it goes limp |
| `servo_register` | Re-attach both servos; harmless to repeat |

`holo led` and `holo leia`'s light half do nothing on this board — there is no light attached.
Full detail in [The holoprojector](../use/holo.md).

## Files and firmware

| Command | Does |
|---|---|
| `fs ls\|df\|stat\|mkdir\|rmdir\|rm\|mv\|cat\|hexdump\|sha256\|bench …` | The LittleFS volume at `/data`. Paths are relative to it |
| `fs put [-f] [-b baud] <path> [size]` | Receive a file over XMODEM-1K |
| `fs get [-b baud] [-s 128\|1024] <path>` | Send a file over XMODEM-1K |
| `ota [status]` | Both application slots: what is in each, its version, and which runs and boots |
| `ota put [-b baud] [-n] [-d] <size>` | Receive a new image into the slot that is not running |

The host side of all of these is `tools/fs_xfer.py` in esp-console-kit — see
[Files on the board](../use/files.md) and [Updating over serial](../use/ota.md).

## System

| Command | Does |
|---|---|
| `version` | The running firmware's version and build, and the chip it is on |
| `restart` | Software reset |
| `free` | Free heap |
| `heap` | Heap details |
| `membench` | Memory throughput benchmark |
| `flash-stats [reset]` | Flash operation counters |
| `tasks` | Every FreeRTOS task |
| `top` | Task CPU usage over an interval |
| `log_level` | Change a log tag's level at runtime |
| `gpio get <n> \| set <n> 0\|1 \| release <n>` | Read or drive a pin. Pins the board owns are refused, with the reason |
| `deep_sleep` | Enter deep sleep |
| `light_sleep` | Enter light sleep |

`tasks` and `top` need the FreeRTOS trace and run-time-stats options, and `flash-stats` needs the
flash counters; all are enabled in `sdkconfig.defaults`.

## Networking

Wi-Fi is not needed for anything on this page, but it is available.

| Command | Does |
|---|---|
| `wifi [on\|off]` | Turn the radio on or off |
| `wifi_save <ssid> [pass]` | Remember a network. The board rejoins the last one at boot |
| `wifi_forget` | Forget a saved network |
| `wifi_known` | List saved networks |
| `join` | Join a network without saving it |
| `wifi_link` | Link status |
| `wifi_ps` | Power-save mode |
| `wifi_txpower` | Transmit power |
| `ip addr` | Addresses, with `-f inet\|inet6\|link` and `-s` |
| `ping` | Ping a host |
| `iperf` | Throughput test |
| `traceroute` | Trace a route |
| `dig` | DNS lookup |

## NVS and I2C

| Command | Does |
|---|---|
| `nvs_set` / `nvs_get` | Read and write a key |
| `nvs_erase` / `nvs_erase_namespace` | Erase a key, or a whole namespace |
| `nvs_namespace` | Select the namespace the other commands act on |
| `nvs_list` | List what is stored |
| `i2cconfig` | Configure the bus the I2C tools use |
| `i2cdetect` | Scan the bus |
| `i2cget` / `i2cset` | Read and write a register |
| `i2cdump` | Dump a device's registers |

The I2C tools borrow the bus the firmware already created on port 0, and never tear down a bus
they did not create. Remember that the touch controller only answers while the screen is being
touched.

Servo calibration lives in the `servo` NVS namespace, keyed by pin. Saved Wi-Fi networks live in
their own namespace, set in menuconfig.

## Where these come from

The board-specific commands — `screen`, `lcd`, `touch`, `imu`, `video`, `image`, `leds` — are in
this repository, under `components/`. Everything else comes from the
[esp-console-kit](https://github.com/daveismith/esp-console-kit) submodule, whose README is the
reference for the commands this page only summarises.

`tools/check_command_docs.py` checks that every command the firmware registers appears on this
site, so the tables above cannot quietly fall behind the code.
