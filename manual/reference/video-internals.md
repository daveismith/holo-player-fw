# Video internals

How `components/video` plays a clip, and why it is built the way it is. For using it, see
[Video clips](../use/video.md).

The component has two parts:

- **`quicktime.cpp`** — a QuickTime atom parser, ported from the Flash_PNG Arduino sketch to
  stdio and the VFS, and hardened considerably: real chunk-table seeking through `stsc` with
  `stco`/`co64`, frame durations from `stts`, a codec check in `stsd`, 64-bit atom sizes, and
  enough bounds validation that a damaged file fails to open rather than playing garbage.
- **`video_player.cpp`** — the player, decoding with Espressif's `esp_new_jpeg` on a task pinned
  to core 1.

Each frame is read from `/data` and drawn centred on the panel, timed by the clip's own
time-to-sample table.

## Why frames are streamed in blocks

A 240×240 frame of RGB565 is **115 KB**. That is larger than the biggest free block of internal
RAM on this board, so the whole frame cannot be held in a DMA-capable buffer at all.

Instead, `esp_new_jpeg`'s block mode decodes **16 lines at a time** into one of two small DMA
buffers. Each block is handed to the panel while the next one decodes, so decoding and the SPI
transfer overlap and neither waits for the other. The panel is locked for the duration of the
frame, between `board_lcd_stream_begin()` and `board_lcd_stream_end()`, and
`board_lcd_stream_wait(1)` is what stops the decoder from overwriting a buffer that is still
going out.

Block mode needs both dimensions to be multiples of 8, because JPEG is coded in 8×8 blocks. Clips
that are not fall back to decoding whole frames — correct, but roughly twice as slow at 240×240.
PSRAM is large enough to hold a whole frame, which is what makes the fallback possible at all.

`video verify` decodes sample frames both ways and compares them byte for byte, which is how the
fast path is kept honest against the slow one.

## Measured

At 30 fps, a frame budget of 33.3 ms:

| Clip | Streamed: decode+draw | Streamed: paint | Whole frame: decode+draw | Whole frame: paint |
|---|---|---|---|---|
| 120×120 | 4.6 ms | 4.1 ms | 4.9–5.2 ms | 3.2 ms |
| 240×240 | 14.2–14.6 ms, none late | 13.6 ms | 28.3–30.2 ms, some late | 17.8 ms (frame in PSRAM) |

*Paint* is the time from the first pixels of a frame reaching the panel to the last — how long the
panel spends halfway through an update, which is what tearing depends on. `video status` reports
all of these.

## Turning the screen on

The panel is off — asleep, backlight off — whenever nothing is showing. Starting a clip therefore
has an ordering problem: a freshly woken panel still holds whatever was in its memory, and its
backlight coming up before there are pixels shows it.

So the player:

1. wakes the panel and fills it black **while the display is still off**, so the stale contents
   are never visible;
2. turns the display on;
3. waits two panel refreshes;
4. turns the backlight on;
5. only then draws frame 0 and starts the clip's clock.

`board_lcd_power_on(rgb565)` exists precisely to make steps 1 and 2 a single operation that
cannot be got wrong.

When the clip ends, or on `video stop`, the panel goes back to sleep. Starting another clip, or a
`screen colour`, over a playing one keeps it on.

## Flash writes and late frames

Writing to flash pauses **both** CPU cores for the duration of the write, because the cache is
disabled while it happens. During playback that shows up as an occasional late frame.

The console's command history is the one flash write the firmware controls, and the machinery
around it is worth knowing about because it explains behaviour that otherwise looks arbitrary:

- The console task is the **only** task that ever writes the history file. Other tasks call
  `console_history_request_save()` and the write happens later, on the console task.
- While a clip is playing, the save waits until the screen turns off: when the clip ends by
  itself, on `video stop`, or on `screen clear`.
- If a `screen colour` replaces the clip, the screen stays on, so the save happens with the next
  command instead.
- When a clip ends by itself, the player signals the console task, which does the write while
  waiting for the next keystroke — inside linenoise's input wait. Anything already typed on the
  command line is left alone.

`board_lcd_set_off_hook()` is the general form of this: a callback invoked after the panel goes
from on to off, from whichever task turned it off. Nothing is showing at that moment, so it is the
safe point for work that would disturb playback.

LittleFS is also tuned for this workload — 4096-byte caches, 256-byte reads, and
`CONFIG_LITTLEFS_MMAP_PARTITION` so reads go through the flash cache rather than
`esp_flash_read()`, which would stall both cores the same way a write does. See
[Configuration](config.md).

## Choosing the decoder

`esp_new_jpeg` was picked after measuring it against JPEGDEC 1.5.0 and 1.8.4 and TJpgDec on this
hardware. On a 120×120 clip frame it decoded in **1.79 ms**, against 2.63 ms for JPEGDEC 1.8.4.
JPEGDEC 1.5.0 also leaves the last 8 rows of a 120-row frame unwritten, which rules it out
regardless of speed. The full results are in the commit message of `3a7182e`.

The build uses `CONFIG_COMPILER_OPTIMIZATION_PERF` (`-O2`) partly for this reason: `esp_new_jpeg`
ships prebuilt and optimised, so at the default `-Og` a source-built decoder would lose the
comparison for reasons that have nothing to do with the decoders.
