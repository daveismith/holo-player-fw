# Video internals

How `components/video` puts clips and images on the panel, and why it is built the way it is.
For using it, see [Video clips](../use/video.md) and [Images](../use/images.md).

The component's parts:

- **`quicktime.cpp`** — a QuickTime atom parser, ported from the Flash_PNG Arduino sketch to
  stdio and the VFS, and hardened considerably: real chunk-table seeking through `stsc` with
  `stco`/`co64`, frame durations from `stts`, a codec check in `stsd`, 64-bit atom sizes, and
  enough bounds validation that a damaged file fails to open rather than playing garbage.
- **`video_player.cpp`** — the player, decoding with Espressif's `esp_new_jpeg` on a task pinned
  to core 1. It plays animated GIFs too, and owns what the screen is showing.
- **`image.cpp`**, **`png_image.c`** and **`gif_image.cpp`** — `image show`, with libpng and
  bitbank2's AnimatedGIF; see [Still images](#still-images) and [GIFs](#gifs) below.

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

## Still images

`image show` is in the same component, and lands in the same place: 16 lines at a time into one of
two DMA buffers, each block on its way to the panel while the next is prepared. A still has the
same problem a frame does — 115 KB at 240×240, more than the largest free internal block — so it
gets the same answer, minus the container and the clock.

**JPEG stills reuse the player's decoder outright**, and inherit its limits: baseline only, and
4:2:0. A 4:4:4 file is refused by the header parse; a 4:2:2 file parses and then fails to decode,
in blocks and whole alike. Because there is no way to tell in advance which files block mode will
turn down, a still that fails to stream falls back to decoding whole and drawing again — the
half-drawn attempt is simply overwritten. It costs nothing on the files that work, and it is why
a JPEG can report `whole image` at a size that is a multiple of 8.

 `decode_frame()` and `decode_blocks()` moved
out of `video_player.cpp` into `jpeg_decode.h` so both callers share them; `decode_blocks()` is a
template so the caller's per-block work inlines into the decode loop, which is why it is in a
header rather than a source file. The one thing a still needs and a clip does not is
`jpeg_probe()`: a clip reads its frames' dimensions from the QuickTime sample description before
it reads a frame, while a still has only the file, so it parses the header first and sizes its
buffers from that.

**PNG is libpng**, in `png_image.c`. Rows come out one at a time and are converted to big-endian
RGB565 straight into the block buffers, so no whole image is held.

Three details of that path are load-bearing:

- **The file is read buffered**, unlike a clip. A clip sets `setvbuf(_IONBF)` because whole frames
  are read straight into the decoder's input and stdio would only add a copy. libpng is the
  opposite: it reads 8-byte chunk headers and 4-byte CRCs constantly, and buffering turns dozens
  of tiny reads into one.
- **Transparency is composited over black** with `png_set_background()`, not stripped. Stripping
  the alpha channel keeps whatever RGB sits under a transparent pixel, which in most exports is
  white — a transparent PNG would arrive with a white halo. Black is also what the panel wakes to,
  so a smaller image and its transparent regions match.
- **No gamma is set.** With libpng's screen gamma left at zero it builds no gamma tables, which
  for a 16-bit input would be tens of KB of internal RAM for a correction nobody asked for.

Interlaced PNGs cannot stream. Adam7 writes each of its seven passes into rows the later passes
fill in, so `png_read_image()` needs every row resident: 173 KB of RGB, which goes to PSRAM,
zeroed so a decode that dies partway can only ever show black. The same block loop then feeds off
it. This mirrors the clip player's whole-frame-in-PSRAM fallback, and `image show` names which
path it took.

### Why `png_image.c` is C

libpng reports errors by `longjmp`. A jump out of a C++ frame skips every destructor on the way,
so the `setjmp` lives in a C file among locals that have none. Two rules follow, and both are
easy to get wrong:

- The jump target is set **before** libpng is handed anything at all, including
  `png_create_read_struct()`, which can itself fail through the error handler. Without a valid
  target, libpng's default is `abort()` — a corrupt file would panic the firmware rather than
  print a message.
- Every local the cleanup reads is `volatile`. Otherwise the compiler may keep it in a register
  that `longjmp` restores to the value it held at the `setjmp`, and the cleanup frees a stale
  pointer or leaks a live one. GCC does not warn about this unless asked.

The error handler also installs a warning function that does nothing, because libpng's default
writes to `stderr` — which here is the console. An ordinary Photoshop export would otherwise
print `iCCP: known incorrect sRGB profile` over the prompt.

### What it costs

libpng and zlib add **78 KB** to the image, which sits in a 2304 KB slot. Peak memory for a
240×240 RGBA PNG is about 34 KB of internal RAM — zlib's inflate state, libpng's two filter rows,
the IDAT read buffer and the two DMA blocks — plus zlib's 32 KB inflate window, which
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` sends to PSRAM. An interlaced file adds its 173 KB there
too.

Console commands run on the main task, so `CONFIG_ESP_MAIN_TASK_STACK_SIZE` was raised from 7168
to 8192 to carry libpng's read path. Measured afterwards with `tasks`, on the worst file to hand —
240×240, 16-bit RGBA, interlaced, with an ancillary text chunk — `main` still had **3240 bytes**
free, so the margin is real rather than assumed. `tasks` reports the same number for a file of
your own.

## GIFs

`image show` takes GIFs too. The decoder is [bitbank2's AnimatedGIF](https://github.com/bitbank2/AnimatedGIF),
because the Espressif registry has none. It is a submodule at `external/AnimatedGIF`, pinned to
upstream `c2478ec` (library version 2.2.3, which upstream has not tagged — the last tag, 2.2.0,
lacks fixes for disposal method 2 and for a crash on corrupt files). `components/animatedgif` is
only packaging: upstream is used unmodified, and a small forced-in header supplies the `millis()`
and `delay()` it still calls when it is built for neither Arduino, Linux nor macOS.

**An animated GIF plays on this component's player task**, the one that plays clips. What a GIF
shares with a clip is everything around the decoder: a clock, a way to stop, being replaced
without the panel blinking, `video status`, and the console history being held back while it
plays (see [Flash writes and late frames](#flash-writes-and-late-frames)). A second task would
have had to reproduce all of it. `image show` opens the file, checks it and allocates everything
before the panel is touched, then hands it to the player already open.

### A canvas, not frames

A GIF frame is rarely a picture on its own. Most are a rectangle drawn over the frames before
it; some have transparent holes the earlier picture shows through; some ask for their area to be
erased to the background when the next frame arrives (disposal method 2). So the decoder keeps a
canvas and composites every frame onto it — AnimatedGIF's *cooked* mode — and the player paints
from the canvas.

It has to be the full-sized frame buffer with **no draw callback**, 3 bytes a pixel: the canvas
as palette indices, then again as big-endian RGB565. That is 173 KB at 240×240, in PSRAM. The
smaller arrangement the library also offers, a callback per line out of `allocFrameBuf()`, is
wrong twice over here:

- Lines come out of one shared line buffer, and a transparent pixel is simply not written — so
  it arrives carrying the colour of the same column on the line above.
- Disposal method 2 writes the background into the RGB565 half of the canvas, which that buffer
  does not have. It would write past the end of it.

What changed is then painted in 16-line blocks, copied out of the canvas: this frame's
rectangle, joined with the previous frame's. The previous one is included because it may have
just been erased to background, and the decoder keeps the previous frame's disposal to itself.
For a well-made GIF, where each frame is a small rectangle, that is a small area — which is why
ffmpeg's GIFs cost a few milliseconds a frame.

### Timing and integrity

Delays are the file's own, with one rule: **10 ms or less becomes 100 ms**, as in Chrome and
Firefox, so a GIF runs at the speed it runs in a browser. The decoder is inconsistent about this
itself — it rounds only a zero up when it plays, and rounds short delays to 20 ms when it counts
them — so the rule lives in one place, `gif_frame_delay_ms()`, and both the player and
`image info` use it. `image info` counts frames and adds up delays by walking the file's blocks,
not with the decoder's own walk, so the length it reports is the length it plays.

The walk also refuses a file that ends before its trailer byte. The LZW decoder carries on
through data that runs out partway, so a GIF cut short in transfer would otherwise play with its
last frame half garbage.

The NETSCAPE loop count is a count of repeats: 0 is forever, *n* plays *n* + 1 times, and a file
without the extension plays once. When the passes run out, the last frame stays up and `screen`
reports it as an image.

### What it costs

The GIF path adds **11 KB** of flash. At 240×240 the decoder's state is 24 KB, in internal RAM
for speed, with the 173 KB canvas and two 7.5 KB DMA blocks alongside it. The player task keeps
6.7 KB of its 8 KB stack free while a GIF plays, and repeated plays leave the heap where it was.

The ceiling is a GIF that changes every pixel of a 240×240 panel with incompressible content:
30.9 ms to decode and 14 ms to paint, about **22 fps**. The decoder's *turbo* mode, which trades
memory for decoding speed, does not help. Its 82 KB buffer only fits in PSRAM, where it decoded
the same frames slower (33.6 ms), and its decoding path skips disposal method 2 altogether.
