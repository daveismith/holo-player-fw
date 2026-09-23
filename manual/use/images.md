# Images

`image show` puts a **PNG**, a **baseline JPEG** or a **GIF** from `/data` in the middle of the
round panel, where it stays until something else takes the screen. A still has no task and no
timing behind it: it is drawn once and then simply left, exactly as a
[colour](../reference/console.md) is. An animated GIF plays — see [Animated GIFs](#animated-gifs).

For clips, see [Video clips](video.md).

## Preparing a file

The panel is 240×240, and an image has to fit it — anything larger is refused rather than
scaled or cropped, so the framing stays yours. Resize on the host:

```sh
sips -z 240 240 <input> --out r2.png            # macOS, built in
ffmpeg -i <input> -vf scale=240:240 r2.png      # anywhere
```

Smaller images are centred, with the rest of the panel black.

!!! warning "A JPEG has to be baseline **and** 4:2:0"
    The decoder reads baseline JPEG at 4:2:0 chroma subsampling. Two things it will not read,
    and what each reports:

    | The file is | `image show` says |
    |---|---|
    | Progressive | `only baseline JPEG is supported` |
    | 4:4:4 | `unsupported colour sampling; re-encode as 4:2:0` |
    | 4:2:2 | `bad JPEG data: corrupt, or colour sampling the decoder cannot read` |

    This catches people out because **ffmpeg picks 4:4:4 by default when the input is a still
    RGB image** — unlike a clip, where the input is already 4:2:0. Ask for it explicitly:

    ```sh
    ffmpeg -i <input> -c:v mjpeg -q:v 3 -pix_fmt yuvj420p r2.jpg
    ```

PNG is far more forgiving: palette, greyscale, grey+alpha, RGB and RGBA, at 1, 2, 4, 8 or 16 bits,
interlaced or not, all decode. Unless you have a reason to send a JPEG, send a PNG.

Transparency is composited **over black**, which is what the panel wakes to — so a transparent
background comes out the same as a black one, not white.

A file that was truncated in transfer is refused rather than half-drawn, with libpng's own reason:
`image: /data/r2.png: Read Error`.

Put the file on the board with [`fs_xfer.py put`](files.md), then show it.

## Showing

```
image show r2.png
image show leia.jpg
```

Each reports what it did:

```
image: showing /data/r2.png (240x240 PNG, 16-line blocks, 118.4 ms)
```

The image stays until a clip, a colour, the calibration pattern or another image replaces it.
`screen` reports it while it is up, and `screen clear` takes it away and puts the panel back to
sleep.

```
screen
showing /data/r2.png (240x240, backlight 100%)
```

Nothing that fails ever disturbs the screen. The file, its format and its size are all checked
before the panel is touched, so a rejected `image show` leaves whatever was already showing in
place.

## Animated GIFs

A GIF with more than one frame plays, on its own frame delays, the same way a clip plays on its
own timing:

```
image show r2-wave.gif
image: playing /data/r2-wave.gif (240x240 GIF, 24 frames, loops forever)
```

It loops the way the file says, which is how it looks in a browser: forever, a set number of
times, or once if the file does not say. When a GIF that stops runs out of passes, **its last
frame stays up** — it was shown as an image, and an image stays until something replaces it.
`screen` says `playing` while it moves and `showing` once it has stopped.

It plays on the same task as clips, so everything that works on a clip works on a GIF:
`video stop` stops it and puts the panel to sleep, `video status` reports its frame times, and
another image, clip or colour replaces it without the panel blinking. A one-frame GIF is a
still like any other.

### Making one

From a clip or any video, at 240 pixels across and a frame rate the panel can keep up with:

```sh
ffmpeg -i <input> -vf "fps=15,scale=240:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse" r2-wave.gif
```

The palette pair builds one 256-colour palette for the whole animation instead of a generic
one; the difference is obvious. ffmpeg also writes each frame as only the rectangle that
changed, which is what makes a GIF cheap to play here — see the table below.

!!! note "Very short frame delays play at 100 ms"
    A frame with no delay, or a delay of 10 ms or less, stays up 100 ms. That is what Chrome and
    Firefox do, and files that ask for no delay were made expecting it — so a GIF runs at the
    speed it has in a browser, not faster.

### What it costs

Measured on the board with `video status`. What matters is how much of the picture changes
from frame to frame, not how big the picture is: only the changed area is decoded and painted.

| GIF | Frame budget | Decode + draw per frame | Late |
|---|---|---|---|
| ffmpeg, 240×240, 20 fps (a test pattern with moving parts) | 50 ms | 5.9 ms | none |
| ffmpeg, 120×120, 20 fps | 50 ms | 1.9 ms | none |
| Every pixel changing, flat colours, 240×240 | — | 30.7 ms | — |
| Every pixel changing, random noise, 240×240, 25 fps | 40 ms | 44.9 ms | all |

The last row is the ceiling: a 240×240 GIF that repaints the whole panel with incompressible
content every frame manages about **22 fps**. Real animations rarely come near it. If one
stutters, `video status` will show frames running late; drop its frame rate, or its size.

## Looking at a file

```
image info r2.png
```

reports what the file contains without decoding it or touching the panel:

```
/data/r2.png: PNG 200x150, 8-bit RGBA, non-interlaced, 14233 bytes
parsed in 3.2 ms
```

For a GIF it counts the frames and adds up their delays, by the same rule it plays them:

```
/data/r2-wave.gif: GIF 240x240, 24 frames, loops forever, 1.60 s a pass, 412306 bytes
```

As with [`video info`](video.md#looking-at-a-clip), it is the quickest way to find out whether a
file survived its upload intact. A GIF cut short in transfer is refused outright —
`truncated: it ends before the GIF trailer` — rather than played with its last frame half
garbage.

## How it is decoded

PNG and JPEG end up in the same place: 16 lines at a time into one of two small DMA buffers, each
block sent to the panel while the next is prepared. It is the mechanism the clip player uses,
because a still has the same problem — a 240×240 frame is 115 KB, more than the largest free
block of internal RAM.

Two things fall back to decoding the whole image first, and `image show` names which:

| Case | Mode reported |
|---|---|
| The usual one | `16-line blocks` |
| A JPEG whose width or height is not a multiple of 8 | `whole image` |
| An **interlaced** PNG | `whole image, in PSRAM` |

Adam7 interlacing cannot be read row by row: each of its seven passes writes only its own pixels
into rows the later passes fill in, so every row has to be resident at once. That is 173 KB, which
goes to PSRAM. A still has no frame to miss, so the slower path costs nothing but the wait.

A GIF is different, because its frames are rarely whole pictures: most are a rectangle drawn
over the ones before. So it is composited onto a 240×240 canvas held in PSRAM, and then only
what changed is sent, in the same 16-line blocks. `video status` calls this
`changed area only, from a PSRAM canvas`.

## What a still costs

Measured on this board, from the command to the last pixel on the panel — decode, convert and
paint together, which is what `image show` reports:

| File | Mode | Time |
|---|---|---|
| 240×240 PNG, RGB | 16-line blocks | 101 ms |
| 240×240 PNG, RGBA | 16-line blocks | 107 ms |
| 240×240 PNG, interlaced | whole image, in PSRAM | 175 ms |
| 120×120 PNG, RGB | 16-line blocks | 40 ms |
| 240×240 JPEG, 4:2:0 | 16-line blocks | 27 ms |
| 120×120 JPEG, 4:2:0 | 16-line blocks | 17 ms |

JPEG is three to four times quicker than PNG at the same size, which is the difference between a
decoder written for this chip and a general-purpose one doing zlib on the CPU. Neither matters
much for something shown once — but the very first `image show` after a restart takes about twice
as long as the table says, while libpng and zlib claim their buffers for the first time.

Why it is built this way is in [Video internals](../reference/video-internals.md#still-images).
