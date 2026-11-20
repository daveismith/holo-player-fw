# Host link: CAN

!!! note "Not in the firmware yet"
    This page describes the CAN link ahead of the firmware, for review. UART and RS485 work now.

On CAN, the board is a node on a [Cyphal](https://opencyphal.org) bus: Cyphal/CAN, on classic CAN
frames. It suits a build that already has a CAN bus through the body and dome, with other Cyphal
nodes on it. [The board on Cyphal](../use/cyphal.md) covers what the node does once it's wired:
how it gets its node-ID, what it reports, and its settings.

What the node does so far is the baseline: it gets a node-ID, sends a heartbeat, and answers
`uavcan.node.GetInfo`. Playing scenes and reporting events over Cyphal come later; until then a
host runs the board over [UART](host-uart.md), [RS485](host-rs485.md) or Wi-Fi.

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
  acknowledges anyone else's frames. Left unconnected, it may float high.
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
- **Every node uses the same bitrate.** The board's is 1 Mbit/s unless changed.

## Turning it on

On the board's USB console:

```text
link mode can
restart
```

After the restart, `cyphal` shows the node. With nothing else on the bus it waits for a node-ID:

```text
cyphal: waiting for a node-ID, r2.dome.holo.front, 1 Mbit/s on GPIO21 (TX), GPIO33 (RX)
```

## A first test

With a candleLight adapter on a computer, and the board the only other node, give
`cyphal_service` a transport at the board's bitrate:

```toml
[[transport]]
type    = "can"
name    = "can-usb"
driver  = "gsusb"
node_id = 43
bitrate = 1000000
```

```sh
cyphal_service --foreground --config cyphal.toml
```

Within a few seconds the board has a node-ID, and the service lists it:

```text
cyphal> nodes
Transport: can-usb
  Node ID  Uptime(s)  Health     Mode
  43       12         0          0
  52       3          0          0
cyphal> get-info can-usb 52
Node 52  (r2.dome.holo.front)
```

If the board doesn't appear, `cyphal` on its console says what the bus looks like from its side:

- **`tx errors` climbing, and the state `error-passive`:** nobody is acknowledging the board's
  frames. The adapter isn't open, the bitrates differ, CANH and CANL are swapped, or a
  termination resistor is missing.
- **`rx errors` climbing:** frames arrive damaged. Usually the bitrate, then the termination.
- **Nothing received and no errors:** the board hears nothing at all. Check VIO, the RXD wire,
  and that the other node is sending.
- **The other node sees nothing, and the board's counters don't move:** check that S is at GND,
  and the TXD wire.
- **`stopped: node-ID … is in use by another node`:** see
  [a node-ID that is already taken](../use/cyphal.md#a-node-id-that-is-already-taken).

The `can` command tests the wiring below Cyphal: [raw frames](../use/cyphal.md#testing-the-wiring-raw-frames)
in and out, and a self-test that needs no second node.
