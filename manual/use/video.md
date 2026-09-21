# Video clips

The player reads **Motion-JPEG QuickTime** files from `/data` and draws them centred on the round
panel, timed by the clip's own time-to-sample table — so a clip plays at the speed it was authored
at, not at whatever rate the decoder happens to manage.

## Making a clip

Export at **120×120**; anything up to 240×240 fits the panel. Then convert:

```sh
ffmpeg -i <input> -c:v mjpeg -q:v 9 -an <output>.mjpeg.mov
```

`-q:v 9` trades quality for size, and `-an` drops the audio track, which the board has no use for.

!!! warning "Width and height must both be multiples of 8"
    JPEG is coded in 8×8 blocks, and the fast path decodes 16 lines at a time. A clip whose
    dimensions are not multiples of 8 still plays, but silently falls back to decoding whole
    frames, which is about twice as slow at 240×240. 120×120 and 240×240 are both fine; 150×150
    is not.

Put it on the board with [`fs_xfer.py put`](files.md), then play it.

## Playing

```
video play leia.mjpeg.mov
video play leia.mjpeg.mov loop
video stop
```

`loop` repeats the clip until `video stop`. Without it, the clip plays once and the screen turns
off — the panel sleeps with its backlight off whenever nothing is showing.

Starting another clip, or a `screen colour`, over a playing clip keeps the panel on and swaps what
is on it.

## Looking at a clip

```
video info leia.mjpeg.mov
```

reports what the file contains — dimensions, frame count, durations — without playing it. It is
also the quickest way to find out whether a file survived its upload intact.

```
video status
```

reports, per frame: the time to read it from flash, to decode it, and to decode **and** draw it.
It also reports the *paint* time, from the first pixels sent to the panel to the last. Paint time
is what tearing depends on: it is how long the panel spends halfway through an update.

## Decoding modes

By default frames are **streamed**: decoded 16 lines at a time into one of two small DMA buffers,
with each block sent to the panel while the next one decodes.

```
video play leia.mjpeg.mov frame
```

forces **whole-frame** mode, which decodes the entire frame before sending any of it. This is also
the automatic fallback for clips whose dimensions are not multiples of 8.

To check the fast path against the slow one:

```
video verify leia.mjpeg.mov
```

decodes sample frames both ways and compares them byte for byte.

Measured on the test clips at 30 fps — a 33.3 ms frame budget:

| Clip | Streamed: decode+draw | Streamed: paint | Whole frame: decode+draw | Whole frame: paint |
|---|---|---|---|---|
| 120×120 | 4.6 ms | 4.1 ms | 4.9–5.2 ms | 3.2 ms |
| 240×240 | 14.2–14.6 ms, none late | 13.6 ms | 28.3–30.2 ms, some late | 17.8 ms |

Why streaming exists at all, and what the numbers mean, is in
[Video internals](../reference/video-internals.md).

## If a frame is late

Anything that writes to flash while a clip plays — saving a Wi-Fi network, an NVS write — pauses
**both** CPU cores for the duration of the write, and can make a frame late.

The firmware already handles the one case it controls: the console's command history is written
only when no clip is playing. Everything else is up to you. If playback stutters in a way that
`video status` says it should not, look for something writing to flash.
