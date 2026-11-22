# LED patterns

The `leds` command drives the NeoPixel ring: a solid colour, a pattern, or off. For getting the
ring connected in the first place, see [Wiring the LED ring](../connect/leds.md).

## What it is showing

```
leds
```

reports the current mode and colour, whether a pattern is looping, a pulse's or a blink's period,
a level other than 100 (see [the holo's light](#the-holos-light)), the brightness, a timed mode's
time left and what follows it, and the hardware it is driving — how many LEDs, on which GPIO, at
which data rate and in which colour order. That last part is the quickest way to confirm the strip is configured the way your
hardware actually is.

## A solid colour

```
leds colour red
leds colour #ff8000
leds colour 255,128,0
leds colour 255 128 0
```

A colour is a name (`red`, `orange`, …), `#RRGGBB` or bare `RRGGBB`, or three decimal values
either comma-separated or as separate words. `color` is accepted as well as `colour`.

```
leds off
```

turns the strip off; `leds stop` does the same thing.

## Patterns

| Command | Pattern |
|---|---|
| `leds wipe [<c>] [loop]` | Each LED to the colour in turn, 250 ms apart. White if no colour is given |
| `leds rainbow [loop]` | The colour wheel five times round the ring in 12.8 s, spread once round it and gamma-corrected |

Each pattern plays once, holds for a second, and then turns the strip off. With `loop` it repeats
until `leds off`.

```
leds wipe
leds wipe blue
leds wipe blue loop
leds rainbow loop
```

`rainbow` takes no colour — it is the whole wheel — so `leds rainbow green` is an error rather
than a tint.

### Flicker, pulse and blink

```
leds flicker
leds flicker #4da6ff
leds pulse
leds pulse blue -p 3000
leds blink red -p 400
```

`flicker` makes the colour flicker like a failing hologram: mostly bright and restless, each LED a
little apart from the others, with the odd dropout to near dark and a short stutter. `pulse` fades
the colour up from dark and down again once a period (2 s unless `-p` gives another, 200–60000 ms),
evenly to the eye. `blink` lights the colour for the first half of each period and turns it off for
the second (1 s unless `-p` gives another). White if no colour is given. Unlike the wipe and the
rainbow these have no end: each runs until something else takes the strip, such as `leds off`,
another pattern, or a scene. Paired with a clip in a scene, a flicker makes the ring look like part
of the hologram.

## The holo's light

The ring is also the holo's light: a controller on [Cyphal](cyphal.md#the-light-is-the-led-ring)
sets it with the light modes a dome panel's own holos have. The same can be done here. Every mode
but `off` takes a level, and `colour`, `off`, `flicker`, `pulse` and `blink` a time:

| Option | Does |
|---|---|
| `-l <pct>` | The level, 0–100: a share of the brightness this mode lights at. 100 unless given. 0 lights nothing. A wipe's or a rainbow's comes after its `loop` |
| `-t <s>` | For this long, 0.1–3600 s, and then back to what the ring was doing before: its mode, colour and level |

```
leds pulse orange -l 40
leds wipe blue loop -l 30
leds colour red -t 5
leds off -t 2
```

A timed mode during another timed mode goes back, when its time is up, to what the first would
have gone back to, so a chain of them unwinds in one step. Any other `leds` command but `bright`,
or a scene, during a timed mode replaces it for good. A wipe or a rainbow playing once goes back to off, where it
would have ended by then. `leds` shows a timed mode with how long it has left and what follows:

```text
solid #ff0000; brightness 30%; for 4.2 s more, then pulse #ffa500, every 2000 ms, at 40%; 16 LEDs on GPIO16, 800 kHz, GRB
```

The level is a share of the brightness rather than a brightness of its own, so that the light at
full is the ring at the brightness it is set to (`leds bright`), never more: the limit below holds
whatever a controller asks for.

## Brightness

```
leds bright 30
```

Brightness is a percentage from 1 to 100, applied to everything the strip shows, and it is the
main thing standing between you and a brown-out.

!!! warning "30 is the ceiling on VSYS"
    Sixteen WS2812s at full white can draw about 1 A, which is more than the P2 header's contact
    and a shared USB supply can give. Powered from VSYS, keep `leds bright` at 30 or below. Full
    brightness needs the strip on its own 5 V supply — see
    [the power section](../connect/leds.md#power).

The boot brightness is set in menuconfig (`LEDS_BRIGHTNESS`, 100% by default); see
[Configuration](../reference/config.md).
