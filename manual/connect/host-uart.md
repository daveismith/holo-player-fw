# Host link: UART

!!! note "Not in the firmware yet"
    This page describes the host link ahead of the firmware, for review. Nothing here works on
    a board yet.

A UART is the simplest [host link](../use/host-link.md): three wires to one host close by, such as
a Raspberry Pi, an ESP32, an Arduino with a spare serial port, or a USB-serial adapter on a
computer. It carries both [JSON lines and native frames](../reference/host-protocol.md), and the
board can send events as they happen without an extra wire.

The board's UART is 8 data bits, no parity, one stop bit (8N1), at 115200 baud unless changed,
with no flow control.

## Three wires

The board's TX goes to the host's RX, and the host's TX to the board's RX.

| Board | P2 pin | Host |
|---|---|---|
| GPIO21, TX | 11 | RX |
| GPIO33, RX | 12 | TX, through a divider if the host is 5 V (below) |
| GND | 1 or 5 | GND. Always connect it |
| GPIO15, ATTN (optional) | 7 | An input that can wake the host |

Keep the wires under a metre or two. For longer runs, or anything through a slip ring, use
[RS485](host-rs485.md).

## 3.3 V and 5 V

The board's pins are 3.3 V, and **5 V on GPIO33 will damage it**.

- **A 3.3 V host** (a Pi, an ESP32, a 3.3 V Arduino, a 3.3 V USB-serial adapter): wire it
  directly.
- **A 5 V host** (an Arduino Uno, Mega or Nano, most MarcDuinos): its TX needs bringing down to
  3.3 V. A divider is enough at 115200 baud: 1 kΩ from the host's TX to GPIO33, and 2 kΩ from
  GPIO33 to GND. The other direction needs nothing: a 5 V AVR reads the board's 3.3 V as a high.
  A level-shifter module (BSS138 or TXS0108 style) works in both directions instead.

Check a USB-serial adapter's voltage before connecting it. Many have a 5 V / 3.3 V jumper, and
some are 5 V only.

## ATTN, for a host that sleeps

The board sends events on its TX line as they happen, so most hosts don't need a third wire.
A host that sleeps can't be woken by a UART line alone. For that, turn on `attn` in the link
settings and wire GPIO15 to an input that wakes it. ATTN goes low while events are waiting, and
high again once the host has read them.

## Turning it on

On the board's USB console:

```text
link mode uart -b 115200
restart
```

## A first test, from a computer

With a 3.3 V USB-serial adapter on the three wires, open it in any terminal program at 115200
baud, 8N1. After the board restarts, it sends:

```text
! {"event":"ready","seq":1,"t_ms":2410,"firmware":"v1.1.0","protocol":1,"address":1}
```

Type a request and press Enter. The board doesn't echo it, so turn on local echo in the terminal
to see what you type:

```text
GET /leds
200 {"mode":"off","colour":"#ffffff","loop":false,"brightness":30,"count":16,"gpio":16}
PATCH /leds {"mode":"solid","colour":"orange"}
200 {"mode":"solid","colour":"#ffa500","loop":false,"brightness":30,"count":16,"gpio":16}
```

If nothing comes back, run `link log on` on the USB console and try again. Nothing arriving means
the wiring (TX and RX swapped is the usual fault). Garbage, or `framing_errors` climbing in `link`,
means the baud rates differ.

## From a Raspberry Pi

The Pi's own UART is on its 40-pin header: TX on pin 8 (GPIO14), RX on pin 10 (GPIO15), and GND on
pin 6. Both are 3.3 V, so wire them straight to the board. Turn the serial port on and its login
shell off with `sudo raspi-config` (Interface Options, Serial Port), then use `/dev/serial0`.

In Python, with pyserial:

```python
import json
import serial

port = serial.Serial("/dev/serial0", 115200, timeout=2)

def request(line):
    """Send one request, and return (status, body). Events that arrive first are printed."""
    port.write((line + "\n").encode())
    while True:
        reply = port.readline().decode().strip()
        if not reply:
            raise TimeoutError(line)
        if reply.startswith("!"):
            print("event:", json.loads(reply[2:]))
            continue
        status, _, rest = reply.partition(" ")
        if rest.startswith("+"):                   # events waiting: +<n>
            _, _, rest = rest.partition(" ")
        return int(status), json.loads(rest) if rest else None

request('PATCH /link/events {"kinds":["scene_ended"],"push":true}')
print(request('POST /scenes/apply {"name":"cantina"}'))
```

## From an ESP32

Any of its spare UARTs, on any two pins, wired straight across. With the Arduino core:

```cpp
HardwareSerial holo(1);

void setup() {
  holo.begin(115200, SERIAL_8N1, /* rx */ 16, /* tx */ 17);
  holo.println("POST /scenes/apply {\"name\":\"cantina\"}");
}

void loop() {
  if (holo.available()) {
    String line = holo.readStringUntil('\n');
    if (line.startsWith("!")) {
      // an event: parse line.substring(2) with ArduinoJson
    } else {
      // a reply: the status is the first three characters
    }
  }
}
```

## From a 5 V Arduino

Through the divider above. An Uno has one hardware serial port, and it's also its USB, so use a
Mega's `Serial1`, or SoftwareSerial at 9600 or 19200 baud (`link mode uart -b 19200`). SoftwareSerial
can't send and receive at once, so leave events unpushed and use the pending count instead.

With little memory, [native frames](../reference/host-protocol.md#native-frames) are easier than
JSON. A `SCENE` request for slot 3 is these bytes before COBS: the board's address (1), a
sequence number the reply carries back (7), the type (0x10), the slot, and the CRC:

```text
01 07 10 03 <crc16, low byte first>
```

JSON lines work too, as long as the host only writes them and reads the status:

```cpp
Serial1.begin(115200);
Serial1.println("POST /scenes/apply {\"name\":\"cantina\"}");
// the reply starts with "200" when it worked
```
