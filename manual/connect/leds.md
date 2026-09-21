# Wiring the LED ring

The firmware drives a **16-LED WS2812 ring** on GPIO16, using Espressif's `led_strip` component on
the RMT peripheral, in GRB order at 800 kHz. The strip is cleared at boot.

For what to do with it once it is wired, see [LED patterns](../use/leds.md).

## Three wires

GPIO16 is pin 8 of the [P2 header](board.md#p2-expansion-header).

| Strip | P2 pin | Signal |
|---|---|---|
| DIN (data in — the tail of the arrow printed on the strip) | 8 | GPIO16, through a 300–500 Ω resistor **at the strip end** |
| GND | 1 or 5 | Ground. Always connect it, even when the strip has its own supply |
| +5 V | 2 | VSYS, or a separate 5 V supply — see below |

The series resistor goes at the strip, not at the board: it is there to damp reflections on the
data line, and it only does that near the far end.

## Power

Each WS2812 draws up to about **60 mA at full white**, so sixteen of them can ask for **1 A**.
That is too much for a single SH1.0 contact and its thin wire, and too much for a USB port that is
also powering the board.

- **From VSYS (P2 pin 2).** Keep `leds bright` at **30 or below**, which is roughly 300 mA. Note
  that VSYS is about 4.6–4.7 V on USB and 3.0–4.2 V on battery, not a clean 5 V.
- **From a separate supply.** For full brightness, feed the strip's +5 V from the supply directly
  and **join the grounds**. The board and the strip must share a ground reference or the data
  signal has nothing to be measured against.
- **Always** put a 470–1000 µF capacitor across the strip's +5 V and GND, at the strip, to absorb
  the current steps as LEDs switch.
- A 12 V WS2811 strip needs its own 12 V supply. Share only GND and DIN with the board; 12 V on
  P2 would destroy it.

## Data level

GPIO16 drives 3.3 V. A WS281x wants 0.7 × its own supply voltage as a logic high — 3.3 V when the
strip runs from VSYS on USB, and 3.5 V from a true 5 V supply. So the margin is nil to negative,
though short wires usually work anyway.

If the first LED flickers or shows the wrong colour while the rest behave, that is the symptom.
Add a **74AHCT1G125** buffer between GPIO16 and DIN, powered from the strip's +5 V: an HCT input
treats 3.3 V as a solid high and re-drives it at 5 V.

## Data rate

800 kHz is correct for WS2812, WS2812B and SK6812, and is the default.

!!! warning "If the LEDs stay white whatever you send, the rate is wrong"
    menuconfig also offers 400 kHz, for WS2811 strips. At 400 kHz a "0" bit is sent as a 0.5 µs
    pulse, and a WS2812 reads that as a "1". Every frame then comes out as full white — including
    the frame that is supposed to turn the LEDs **off**, which is what makes it confusing rather
    than merely wrong.

## A note on GPIO16

GPIO16 is the ESP32-S3's XTAL_32K_N pin. This board has no 32 kHz crystal fitted, so it is an
ordinary GPIO and free to use. The `gpio` command still refuses to drive it, because the firmware
has claimed it for the strip.
