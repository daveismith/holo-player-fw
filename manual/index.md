# Holo Player

Firmware for the **Waveshare ESP32-S3-Touch-LCD-1.28**, driving a droid's holoprojector: it plays
Motion-JPEG clips and shows images — animated GIFs included — on a 240×240 round panel, lights a NeoPixel ring, and aims two servos the way a
dome's basic holoprojectors move. Everything is driven from a console on the board's USB-C port.

The board is an ESP32-S3R2 with 2 MB of PSRAM and 16 MB of flash, carrying a GC9A01 round IPS
panel, a CST816S touch controller and a QMI8658 IMU. Clips live on an 11 MB LittleFS volume and go
onto the board over the same serial console, as does new firmware.

<div class="grid cards" markdown>

-   **[Build and flash](install/flashing.md)**

    ---

    Clone with submodules, build with ESP-IDF, and flash over USB-C.

-   **[The board](connect/board.md)**

    ---

    Pins, the P2 expansion header, and what is wired to what.

-   **[Play a clip](use/video.md)**

    ---

    Make a Motion-JPEG file, put it on the board, and play it.

-   **[Show an image](use/images.md)**

    ---

    A PNG, JPEG or GIF on the panel, where it stays until something replaces it. Animated GIFs
    play.

-   **[Wire the LED ring](connect/leds.md)**

    ---

    Three wires, and the power budget that decides whether it needs its own supply.

-   **[Aim the holoprojector](use/holo.md)**

    ---

    The motions, and calibrating each axis' travel.

-   **[Console commands](reference/console.md)**

    ---

    Every command the firmware registers.

</div>

!!! note "The screen is off whenever nothing is showing"
    At boot, after `screen clear`, and when a clip ends, the panel is asleep with its backlight
    off — not black, but unpowered. That is deliberate, and it is why the first frame of a clip is
    never drawn to a dark screen. A colour or an image stays up until something replaces it.
