# Wiring the servos

Two hobby servos aim the holoprojector: **pan** (left and right) and **tilt** (up and down). They
run on the ESP32-S3's MCPWM, one timer each.

For the motions themselves, and for calibrating each axis, see
[The holoprojector](../use/holo.md).

## Connections

GPIO17 and GPIO18 are pins 9 and 10 of the [P2 header](board.md#p2-expansion-header).

| Wire | Goes to |
|---|---|
| Servo 1 signal (orange, or white) | P2 pin 9, GPIO17 |
| Servo 2 signal | P2 pin 10, GPIO18 |
| Both servos' power (red) | A separate 5 V supply |
| Both servos' ground (brown, or black) | That supply's ground, **and** P2 pin 1 or 5 |

## Power

!!! warning "Do not take servo power from P2's VSYS pin"
    A small hobby servo draws 0.5–1 A as it starts moving or when it stalls. Two of them at once
    would brown out the board, and a brown-out mid-write is how filesystems get damaged. Give the
    servos their own 5 V supply.

Join the servo supply's ground to the board's ground. The pulse the board sends is measured
against ground, so without a shared reference the servos see noise rather than a signal.

## Signal level

The signal is 3.3 V, which hobby servos accept — unlike the LED strip, a servo's input is a
comparator with a low threshold, so no level shifting is needed.

## Which servo is which

By default **servo 1 (GPIO17) pans** and **servo 2 (GPIO18) tilts**.

The sketch this firmware grew out of never recorded which servo was which, so if `holo wag` nods
instead of shaking its head, the two are swapped. Rather than rewiring, change *Which servo pans*
in `idf.py menuconfig` — see [Configuration](../reference/config.md).

## Travel

Each servo has a working range, defaulting to the range the original sketch used: **1150–1900 µs**
for servo 1 and **1300–1950 µs** for servo 2. Both sit inside an absolute **1000–2000 µs** range
that no servo is ever driven outside, whatever it is asked for.

The low end of a range is CLOSED, meaning full left or full down. If an axis runs backwards, or
its travel needs tuning for the mount you have built, calibrate it with `holo endpoints` rather
than rewiring — the values are saved in NVS under the pin, so they follow the wiring. See
[The holoprojector](../use/holo.md#calibrating-the-travel).

## Going limp

The servos are not driven at boot, and stay limp until something moves them. `holo off`, or
`servo_off all`, stops driving them again; the next move picks up from wherever they were left.
This matters mechanically — a servo held against a hard stop for hours will cook itself.
