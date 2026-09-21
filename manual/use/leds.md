# LED patterns

The `leds` command drives the NeoPixel ring: a solid colour, a pattern, or off. For getting the
ring connected in the first place, see [Wiring the LED ring](../connect/leds.md).

## What it is showing

```
leds
```

reports the current mode and colour, whether a pattern is looping, the brightness, and the
hardware it is driving — how many LEDs, on which GPIO, at which data rate and in which colour
order. That last part is the quickest way to confirm the strip is configured the way your
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
