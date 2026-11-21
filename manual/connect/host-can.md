# Host link: CAN

On CAN, the board is a node on a [Cyphal](https://opencyphal.org) bus: Cyphal/CAN, on classic CAN
frames. It suits a build that already has a CAN bus through the body and dome, with other Cyphal
nodes on it. [The board on Cyphal](../use/cyphal.md) covers what the node does once it's wired:
how it gets its node-ID, what it reports, and its settings.

Over Cyphal a controller can do most of what a host does over [UART](host-uart.md) or
[RS485](host-rs485.md): move the holo and set its light, apply scenes, show clips and stills, set
the LED ring, hear the board's events, list the scenes and the files, and calibrate the holo, as
well as manage the node with Cyphal's standard registers and commands
([the holo on Cyphal](../use/cyphal.md#the-holo-on-cyphal)). Uploading files, Wi-Fi and the
configuration backup stay on Wi-Fi and the USB console.

## What you need

- **A CAN transceiver** with 3.3 V logic. The ESP32-S3 has the CAN controller, and the
  transceiver puts it on the wires.

    - A **TJA1051T/3** is what this page was written with. It runs from 5 V, and the `/3` version
      has a separate pin (VIO) that sets its logic level to 3.3 V.
    - A 3.3 V part, such as an **SN65HVD230** or a **TCAN332**, needs no 5 V at all.

  !!! warning "No 5 V on GPIO33"
      A 5 V transceiver without a VIO pin (a TJA1050, an MCP2551, or a plain TJA1051T) drives its
      receive output to 5 V. Use one with VIO, or a 3.3 V part.

- **Twisted pair** for CANH and CANL, plus a ground wire.
- **Two 120 Ω resistors**, one at each end of the bus.
- **Something to talk to.** For a first test, a USB CAN adapter on a computer: one that runs the
  candleLight (`gs_usb`) firmware, such as a CANable, works with
  [cyphal_service](https://github.com/daveismith/cyphal_service) on Linux and macOS.

## Wiring a TJA1051T/3

| Transceiver | Pin | Goes to | P2 pin |
|---|---|---|---|
| TXD | 1 | GPIO21 | 11 |
| GND | 2 | GND | 1 or 5 |
| VCC | 3 | 5 V | — |
| RXD | 4 | GPIO33 | 12 |
| VIO | 5 | 3V3 | 6 |
| CANL | 6 | the bus | — |
| CANH | 7 | the bus | — |
| S | 8 | GND | 1 or 5 |

- **VIO to 3V3.** It sets the level of TXD and RXD. Left unconnected, the board receives nothing.
- **S to GND.** S high is silent mode: the board hears the bus but never sends, and never
  acknowledges anyone else's frames; its controller goes bus-off as soon as it tries. Left
  unconnected, it may float high.
- **5 V for VCC.** The part wants 4.5–5.5 V. P2's VSYS pin is not a 5 V rail: it is about 4.6 V on
  USB, which is just inside the range, and the cell's voltage on a battery, which is not. Take
  5 V from the bus's supply or a regulator.
- **A 100 nF capacitor** from VCC to GND, and another from VIO to GND, close to the part.

A 3.3 V transceiver is the same with fewer wires: its supply to 3V3, TXD (often labelled D or CTX)
to GPIO21, RXD (R or CRX) to GPIO33, and its mode pin (RS or S) to GND.

GPIO15, the host link's third pin, isn't used on CAN.

## The bus

```text
   adapter                    board                  another node
  120 Ω H–L                                           120 Ω H–L
  CANH ─────────────────────── CANH ─────────────────────── CANH
  CANL ─────────────────────── CANL ─────────────────────── CANL
   GND ──────────────────────── GND ──────────────────────── GND
```

- **One line, not a star,** with a short stub to each node.
- **120 Ω across CANH and CANL at each end**, and nowhere else. With the power off, a meter
  across CANH and CANL reads about 60 Ω when both are there, 120 Ω when one is missing, and 40 Ω
  when there's one too many. Many adapters and modules have a resistor behind a jumper.
- **Even two nodes on a short cable need both resistors.** At 1 Mbit/s an unterminated bus
  doesn't work.
- **Connect the grounds.**
- **Every node uses the same bitrate.** The board's is 1 Mbit/s unless changed. It samples each
  bit at 85 % of its length, at every bitrate it offers (20 time quanta; SJW 1): the nearest the
  ESP32-S3's CAN controller comes to the 87.5 % that CANopen and Cyphal suggest. Nodes a few
  percent apart work together on a short bus.

## Turning it on

On the board's USB console:

```
link mode can
restart
```

After the restart the node asks the bus for a node-ID by
[plug-and-play](../use/cyphal.md#the-node-id), and `cyphal` shows it waiting until an allocator
answers:

```text
cyphal: waiting for a node-ID, r2.dome.holo.front, 1 Mbit/s on GPIO21 (TX), GPIO33 (RX)
```

then running on the node-ID it was given, within a second or two of the start:

```text
cyphal: node 2 (allocated), r2.dome.holo.front, 1 Mbit/s on GPIO21 (TX), GPIO33 (RX)
```

On a bus with no allocator, give it a fixed node-ID instead, any free one from 0 to 125:

```
cyphal id 52
cyphal restart
```

It listens for 2 seconds, then runs:

```text
cyphal: node 52 (fixed), r2.dome.holo.front, 1 Mbit/s on GPIO21 (TX), GPIO33 (RX)
```

## A first test

With a candleLight adapter on a computer, and the board the only other node, give
`cyphal_service` a transport at the board's bitrate. Its allocator is on unless turned off; its
database goes to `/var/lib/cyphal_service/` unless `pnp_db_path` says otherwise, which on macOS it
has to, since that folder can't be created there without root:

```toml
[[transport]]
type        = "can"
name        = "can-usb"
driver      = "gsusb"
node_id     = 43
bitrate     = 1000000
pnp_db_path = "can-usb-allocations.db"
```

```sh
cyphal_service --foreground --config cyphal.toml
```

Start the board first, so that the service's heartbeats are acknowledged from the moment it opens
the adapter. The board asks for a node-ID, the service gives it the lowest one free in its
database, and lists it within a second or two:

```text
cyphal> nodes
Transport: can-usb
  Node ID  Uptime(s)  Health     Mode
  2        4          0          0
  43       15         0          0
cyphal> get-info can-usb 2
Node 2  (r2.dome.holo.front)
Hardware : 1.0
Software : 0.0
Unique ID: 68:6f:6c:6f:70:6c:61:79:00:00:e4:b0:63:8c:2d:b0
cyphal> pnp-list
Transport: can-usb
  Node ID  Unique ID (hex)                    Pseudo UID           Timestamp
  2        686f6c6f706c61790000e4b0638c2db0   156728846609014      2026-11-20 09:27:15
  43       7fda830fd867643c1f2cfe147645ebf8   NULL                 2026-04-04 23:43:52
```

The service records the board's [pseudo-ID](../use/cyphal.md#the-node-id) when it allocates the
node-ID, then asks the board for `GetInfo` and fills in its unique ID. It gives the same node-ID to
the same pseudo-ID from then on.

The service also records every node it sees with a fixed node-ID, by its unique ID alone. A board
it first saw that way keeps that row, and plug-and-play gives it a new one, with another node-ID:
the service matches a plug-and-play request by its pseudo-ID only. To keep the old node-ID, give
the board that one as a fixed node-ID, or remove the old row from the service's database.

If the board doesn't appear, `cyphal` on its console says what the bus looks like from its side:

- **`bus-off`, and `bus-off … times` going up while `frames: sent` doesn't move:** the board can't
  see its own frames on the bus. A CAN controller reads back every bit it sends; when the bus
  doesn't follow it, each try at a frame ends in an error, and it takes itself off the bus within
  the first frame. Either the bus doesn't follow TXD (S isn't at GND, so the transceiver is
  silent, or the TXD wire is loose) or RXD doesn't follow the bus (the RXD wire, or VIO, which
  sets RXD's level). Check S, VIO, RXD and TXD. The node keeps [recovering](../use/cyphal.md#bus-off),
  less and less often.
- **`tx errors` at 128 and the state `error-passive`:** the board's frames get onto the bus but
  nobody acknowledges them. The adapter isn't open, the bitrates differ, CANH and CANL are
  swapped, or a termination resistor is missing.
- **`rx errors` climbing:** frames arrive damaged. Usually the bitrate, then the termination.
- **`frames: sent` going up, `received` 0:** something on the bus acknowledges the board's frames
  but sends nothing: an adapter that is open with no program sending, say. Check that
  `cyphal_service`, or the other node, is running.
- **`stopped: node-ID … is in use by another node`:** see
  [a node-ID that is already taken](../use/cyphal.md#a-node-id-that-is-already-taken).
  `cyphal_service` is node 43 in the configuration above, so a board given 43 stops this way.

The `can` command tests the wiring below Cyphal: [raw frames](../use/cyphal.md#testing-the-wiring-raw-frames)
in and out, and a self-test that needs no second node.
