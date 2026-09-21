# The holoprojector

Two servos aim the holo — `pan` for left and right, `tilt` for up and down — and the shared holo
engine moves them the way a dome's basic holoprojectors move. For getting them connected, see
[Wiring the servos](../connect/servos.md).

This board has exactly one holo, named `holo`, so its name can be left out of every command:
`holo wag` rather than `holo holo wag`.

```
holo            # status: the light, the current motion, and both axes
holo help       # every verb and its options
```

## Motions

| Verb | Does |
|---|---|
| `holo center` | Both axes to the middle |
| `holo move <x> <y>` | To a point; `x` and `y` are −100…100, the percentage of the way from centre to an endpoint. `+` is right and up |
| `holo nudge <dx> <dy>` | The same, relative to where it is now |
| `holo twitch` | Random glances, until stopped |
| `holo wag` | Side to side, and back |
| `holo nod` | Up and down, and back |
| `holo scan` | A slow sweep, until stopped |
| `holo circle` | Round and back |
| `holo stop` | Motion stops where it is |
| `holo off` | Stop, and both axes go limp |

Options, depending on the verb: `-d <ms>` how long a move takes, `-r <pct>` how far it ranges,
`-n <cycles>` how many times, `-p <ms>` the period of a cycle, `-t <s>` how long to keep going,
and `-i <s-s>` the interval between twitches.

```
holo wag -n 3 -r 60
holo scan -p 4000 -t 30
holo twitch -i 2-8
```

## Going limp

The servos are not driven at boot and stay limp until something moves them. `holo off`, or
`servo_off all`, releases them again; the next move picks up from wherever they were left.

Leaving a servo driven against a hard stop will cook it, so `holo off` is the right way to finish.

## Calibrating the travel

Each axis has a working range in microseconds, defaulting to the range the original sketch used —
1150–1900 µs and 1300–1950 µs — inside an absolute 1000–2000 µs limit no servo is driven outside.

```
holo endpoints h 1150 1900     # pan: closed (full left), then open (full right)
holo endpoints v 1950 1300     # tilt reversed: closed above open turns the axis round
holo endpoints v clear         # back to the default travel
```

`h` is the horizontal axis and `v` the vertical. The first number is **CLOSED** — full left or
full down — and the second is **OPEN**. Putting closed *above* open reverses the axis, which is
the fix when one runs backwards.

Endpoints are saved in NVS under the **pin** (`gpio17`, `gpio18`), not the axis name, so they
follow the wiring rather than the configuration. `servo_list -o wide` shows each servo's absolute
and working ranges.

!!! note "Motion works before you calibrate anything"
    The holo engine normally refuses to move until both axes have saved endpoints. This board
    opts out, because the defaults compiled in are the servos' real travel rather than a guess.
    Calibrate when your mount needs different limits, not to get started.

## The individual servos

The `servo_*` commands reach the two servos one at a time, in raw microseconds rather than
percentages — useful for finding the endpoints to save.

```
servo_list -o wide
servo_move pan 1500
servo_sweep pan -t 5
servo_config pan 1150 1900
servo_off all
```

`servo_register` re-attaches both servos; it is harmless to run, since a pin already attached is
left alone.

## The light

The holo engine also supports a light, with `holo led` and `holo leia`.

!!! warning "There is no light on this board"
    This firmware registers the holo with no light attached, so `holo led` and the light half of
    `holo leia` do nothing here. `holo leia` still centres and moves the axes. The NeoPixel ring
    is driven separately by [`leds`](leds.md), and is not wired to the holo engine.
