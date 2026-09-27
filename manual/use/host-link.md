# The host link

!!! note "Not in the firmware yet"
    This page describes the host link ahead of the firmware, for review. Nothing here works on
    a board yet.

The host link lets another controller run the board over a wire: an Arduino or MarcDuino, a dome
controller, another ESP32, or a Raspberry Pi. It plays scenes, shows clips, sets the LEDs and moves
the holo, and hears back when a clip or scene has ended.

It uses three spare pins on the [P2 header](../connect/board.md#p2-expansion-header), and doesn't
touch the USB console or Wi-Fi: all three work at once.

## Choosing a transport

| Transport | Wires | Boards per bus | Speaks | Good for |
|---|---|---|---|---|
| [UART](../connect/host-uart.md) | TX, RX, GND | one | JSON lines and native frames | One host close by: a Pi, an ESP32, an Arduino with a spare serial port. The simplest to start with |
| [RS485](../connect/host-rs485.md) | A, B, GND, via a transceiver | up to 32, or more with low-load transceivers | JSON lines and native frames | Long runs through the body and dome, and several boards on one pair of wires |
| [I2C](../connect/host-i2c.md) | SDA, SCL, IRQ, GND | many, one address each | native frames | A host that already has an I2C bus in the dome |

CAN (with Cyphal) and plain GPIO triggers are planned for later.

**JSON lines** are the [HTTP API](../reference/http-api.md), one request a line:

```text
POST /scenes/apply {"name":"cantina"}
200 {"screen":{...},"leds":{...},"holo":{...}}
```

**Native frames** are small binary messages with a CRC, for hosts with little memory. The
[protocol reference](../reference/host-protocol.md) specifies both.

## Pins

| Role | Default | P2 pin | UART | RS485 | I2C |
|---|---|---|---|---|---|
| A | GPIO21 | 11 | TX (board → host) | DI of the transceiver | SDA |
| B | GPIO33 | 12 | RX (host → board) | RO of the transceiver | SCL |
| C | GPIO15 | 7 | ATTN, if turned on | DE and /RE of the transceiver | IRQ (open drain) |

A GND wire, P2 pin 1 or 5, goes with every transport. The pins can be changed in the settings, to
any of the free GPIOs that the LEDs and servos aren't using. While the link holds a pin, the `gpio`
command refuses to drive it.

Everything is 3.3 V, and **no pin takes 5 V**. Each transport's page says what to put between the
board and a 5 V host.

## Turning it on

The link is off until it's given a transport. The settings are saved, and can be changed from the
USB console, the HTTP API, or the web app.

**On the console:**

```text
link mode uart -b 115200
restart
```

After the restart, `link` shows it running:

```text
link: uart on GPIO21 (TX), GPIO33 (RX), 115200 baud, protocol auto, address 1
  requests 0, events 0 (none asked for), errors 0, last request never
```

**Over the HTTP API:**

```sh
curl -X PATCH -H "Content-Type: application/json" \
  -d '{"mode":"uart","baud":115200}' http://holo-2db0.local/api/v1/link
```

**In the web app:** the Host link card on the Settings page.

Changing the transport, the pins, `attn`, the I2C address or `general_call` waits for a restart,
and the reply says so with `"restart_required": true`. The rest applies at once: the baud rate,
the address and groups, the protocol, and the events it starts with. A new baud rate takes effect
after the reply, which is sent at the old one.

## The `link` command

| Command | Does |
|---|---|
| `link` | The transport, its pins and settings, the counters, and the events asked for |
| `link mode <off\|uart\|rs485\|i2c> [-b <baud>] [-a <address>]` | The transport. `-a` is the board's address: 1–223, or for I2C its 7-bit address (`0x42`) |
| `link proto <auto\|json\|native>` | What the board accepts on UART and RS485. I2C is always native |
| `link address <1-223>` | The board's address on UART and RS485 |
| `link groups [<group>…\|--clear]` | The groups it belongs to, 1–31, up to 8 |
| `link pins <a> <b> <c>` | The GPIOs for roles A, B and C |
| `link events [<kind>…\|--none] [--push\|--keep]` | The events the link starts with, and whether it pushes them |
| `link log [on\|off]` | Print every line and frame, in and out, on the USB console. For wiring up a host |
| `link reset` | Back to the firmware's defaults |

## What a host can do

Over JSON lines, most of the HTTP API: the screen, the LEDs, the holo and servos, scenes, settings,
the board's information, and the link itself. [The protocol reference](../reference/host-protocol.md#what-is-served)
lists the paths. Files, firmware updates, the network and servo calibration stay on the USB
console and Wi-Fi.

Over native frames, less, but what a show needs: apply a scene by its slot, end it, the screen's
colour and backlight, the LEDs, a holo motion, and the board's status.

**Scenes do the heavy lifting.** Set up the scenes on the web app, give each one a
[slot](../reference/host-protocol.md#scene-slots), and a host needs only "scene 3" to change what
the screen, the LEDs and the holo are all doing.

## Events

A host can hear about things as they happen, rather than asking:

- `clip_ended`: a clip has stopped;
- `scene_ended`: a scene has ended, and how;
- `touch`: the screen was touched (when touch reporting is on);
- `ready`: the board has started (always sent once on UART).

Nothing but `ready` is sent until the host asks, with `PATCH /link/events` or the native
`EVENTS_SET`. On UART, the board sends events as they happen, with no extra wire. On RS485 and
I2C, the host fetches them, and every reply says how many are waiting.
[Events](../reference/host-protocol.md#events) has the details.

## Status

`link` on the console, and `GET /api/v1/link`, count what the link has seen:

| Counter | Counts |
|---|---|
| `requests` | Requests answered |
| `events` | Events given to the host |
| `bad_lines` | JSON lines that didn't parse, or were too long |
| `crc_errors` | Native frames dropped for a bad CRC |
| `framing_errors` | Bytes the UART received damaged: usually a wrong baud rate, or noise |
| `overruns` | Bytes lost because the host sent faster than the board read |
| `ignored` | RS485 lines with no address |

A host that gets no reply at all is usually a wiring or baud-rate problem: `link log on` shows
whether anything arrives. Each transport's page has a first test to try.
