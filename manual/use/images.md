# Still images

`image show` puts a **PNG** or a **baseline JPEG** from `/data` in the middle of the round panel,
where it stays until something else takes the screen. There is no task and no timing behind it:
a still is drawn once and then simply left, exactly as a [colour](../reference/console.md) is.

For moving pictures, see [Video clips](video.md).

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

## Looking at a file

```
image info r2.png
```

reports what the file contains without decoding it or touching the panel:

```
/data/r2.png: PNG 200x150, 8-bit RGBA, non-interlaced, 14233 bytes
parsed in 3.2 ms
```

As with [`video info`](video.md#looking-at-a-clip), it is the quickest way to find out whether a
file survived its upload intact.

## How it is decoded

Both formats end up in the same place: 16 lines at a time into one of two small DMA buffers, each
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

## What it costs

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
