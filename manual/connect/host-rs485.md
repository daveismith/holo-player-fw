# Host link: RS485

!!! note "Not yet tried with a transceiver"
    RS485 is in the firmware, and its addressing has been tried through a plain UART adapter, but
    not yet on a bus with a transceiver.

RS485 is the [host link](../use/host-link.md) for long runs and for several boards on one bus. It
is the UART over a twisted pair, through a small transceiver on each board: runs of tens of metres
work, it shrugs off the noise of motors, and it passes through a slip ring. It carries both
[JSON lines and native frames](../reference/host-protocol.md), with an address on every request.

RS485 here is half duplex: one pair of wires, used by one device at a time. The host is in
charge. It sends a request to one board, and only that board answers. Boards never speak unasked,
so events are fetched rather than pushed.

## What you need

- **A 3.3 V RS485 transceiver** for each board: a MAX3485, SP3485 or THVD1410, or a module built
  on one.

  !!! warning "Not the common blue MAX485 module"
      The MAX485 is a 5 V part, and its receive output would put 5 V on GPIO33. Use a 3.3 V part,
      or a module that says it's 3.3 V.

- **A transceiver for the host.** A USB-RS485 adapter for a computer or Pi (one with automatic
  direction control, such as FTDI's USB-RS485-WE), an RS485 HAT, or another transceiver for a
  microcontroller.
- **Twisted pair** for A and B, such as one pair of a Cat5 cable, plus a ground wire.
- **Two 120 Ω resistors**, one at each end of the bus.

## Wiring a board

| Transceiver | Board | P2 pin |
|---|---|---|
| DI (driver in) | GPIO21 | 11 |
| RO (receiver out) | GPIO33 | 12 |
| DE and /RE, joined | GPIO15 | 7 |
| VCC | 3V3 | 6 |
| GND | GND | 1 or 5 |

The board drives GPIO15 high while it sends, and low the rest of the time, so it listens unless
it's answering.

A module with automatic direction control (no DE or /RE pins) works too: leave GPIO15
unconnected.

## The bus

```text
    host                     board 1                  board 2
  120 Ω A–B                                           120 Ω A–B
     A ──────────────────────── A ──────────────────────── A
     B ──────────────────────── B ──────────────────────── B
   GND ────────────────────── GND ────────────────────── GND
```

- **One line, not a star.** Run the pair from board to board, with only a short stub to each.
- **120 Ω across A and B at each end**, and nowhere else. Many transceiver modules have one
  fitted: remove it, or leave its jumper open, on every board but the last.
- **Bias, once, at the host.** A pull-up from A to 3.3 V and a pull-down from B to GND (680 Ω each)
  keep the idle bus in a known state. USB adapters and HATs often have these already.
- **Connect the grounds.** RS485 tolerates some difference between them, but not an unlimited
  one, and the boards may be on separate supplies.
- **A and B aren't always labelled the same way.** If nothing is heard, swap them at one end.

A standard transceiver is one "unit load", and a bus takes 32. Parts rated at 1/8 of a unit load
(the THVD1410 is one) allow 256.

## Turning it on

Give each board its own address, from 1 to 223, on its USB console:

```
link mode rs485 -b 115200 -a 3
restart
```

**Groups** let one request reach several boards: a board in groups 2 and 5 answers requests to
its own address, and acts on requests to `@g2` and `@g5`, without answering them.

```
link groups 2 5
```

Every board on a bus must use the same baud rate.

## Talking to a board

Every request names a board with `@`, and the reply carries its address:

```text
> @3 POST /scenes/apply {"name":"cantina"}
< @3 200 {"screen":{...},"leds":{...},"holo":{...}}
> @g2 POST /scenes/end
                                       (no reply: a group request isn't answered)
> @* PATCH /leds {"mode":"off"}
                                       (no reply)
```

A line without `@` is ignored on RS485, and counted in `link`.

- **Wait for each reply**, or 2 seconds, before the next request.
- **After a request to a group or to every board**, leave 10 ms before the next request, so
  every board has read it.
- **The board waits 1 ms before it answers**, for the host's transceiver to stop sending. A host
  that turns around more slowly can ask for longer: `reply_delay_ms`, 0–50, in the link settings.

## Events on a bus

Nobody on the bus speaks unasked, so the host polls for what happened (`clip_ended`,
`scene_ended`, `touch`); a resource's changes are only pushed, on UART, so here the host reads the
resource when it wants it. It doesn't need to poll hard, because every reply says how many events
are waiting on the board that sent it:

```text
> @3 GET /screen
< @3 200 +1 {"powered":true,"showing":"nothing",...}
> @3 GET /link/events
< @3 200 {"seq":7,"lost":0,"events":[{"event":"scene_ended","seq":7,"name":"cantina",...}]}
```

A host that isn't otherwise talking to a board sends it a cheap request now and then, such as
`GET /link`, or the native `STATUS`, and fetches events when the count isn't 0.

Ask each board for the events it should report first, with
`@3 PATCH /link/events {"kinds":["scene_ended"]}`, or once and for all in its saved settings with
`link events scene_ended`.

## A first test

With a USB-RS485 adapter on a computer, and one board on the bus, open the adapter in a terminal
at the board's baud rate and send:

```text
@3 GET /link
```

If nothing comes back, run `link log on` on the board's USB console and try again:

- **Nothing arrives:** check A and B (swap them), the ground, and that the adapter's direction
  control is automatic.
- **Lines arrive, but no reply:** the address in the line isn't the board's.
- **The reply is garbled at the host:** try a longer `reply_delay_ms`.
