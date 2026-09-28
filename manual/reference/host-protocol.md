# Host link protocol

!!! note "What's in the firmware so far"
    JSON lines, native frames and events, on UART and RS485. The I2C register map is described
    ahead of the firmware.

This is the specification of what goes over the [host link](../use/host-link.md): the wire
between the board and a controller (another microcontroller, an ESP32, a Raspberry Pi, a dome
controller). The wiring for each transport is on its own page:
[UART](../connect/host-uart.md), [RS485](../connect/host-rs485.md) and
[I2C](../connect/host-i2c.md).

There are two protocols, and the board can accept both on the same port:

| Protocol | Looks like | For | Transports |
|---|---|---|---|
| **JSON lines** | `POST /scenes/apply {"name":"cantina"}` | Hosts that can build and read JSON: a Pi, an ESP32, a person at a terminal | UART, RS485 |
| **Native frames** | Small binary messages with a CRC | Hosts with little memory, and I2C | UART, RS485, I2C |

JSON lines are the board's [HTTP API](http-api.md), one request a line. The requests, replies
and errors are the ones that page and its [OpenAPI description](openapi.json) document. Native
frames are a small fixed subset of the same things: scenes, the screen, the LEDs, the holo, and
status.

This is protocol version **1**. `GET /link` reports it, as does the native `PING`.

## JSON lines

### A request

One line, ending with a line feed (`\n`, 0x0A). A carriage return before it is ignored, so a
terminal's `\r\n` works.

```text
[@<address> ][#<tag> ]<METHOD> <path>[ <JSON>]
```

| Part | What it is |
|---|---|
| `@<address>` | Which board: `@12` for address 12, `@g3` for group 3, `@*` for every board. Needed on RS485. Optional on UART, where there is only one board |
| `#<tag>` | Optional. 1–16 of `A-Z a-z 0-9 _ -`. The reply carries it back, so a host can match a reply to its request |
| `<METHOD>` | `GET`, `PUT`, `POST`, `PATCH` or `DELETE`, in capitals |
| `<path>` | A path of the HTTP API, without `/api/v1`: `/scenes/apply`, `/scenes/scene?name=cantina`. A path with `/api/v1` in front is accepted too |
| `<JSON>` | The request body, on the same line: a JSON object, as the HTTP API takes it. Left out when the request has none |

A line is at most **1024 bytes**, including the line feed, and is UTF-8. An empty line is
ignored.

```text
GET /screen
PATCH /leds {"mode":"solid","colour":"orange","brightness":30}
POST /scenes/apply {"name":"cantina"}
#a7 POST /holo/motion {"motion":"nod","count":2}
@3 POST /scenes/end
```

### The reply

```text
[@<address> ][#<tag> ]<status>[ +<pending>][ <JSON>]
```

| Part | What it is |
|---|---|
| `@<address>` | The board's own address, when the request had one |
| `#<tag>` | The request's tag, when it had one |
| `<status>` | The HTTP status, three digits: `200`, `202`, `204`, `400`, `404`, `409`, `422`, … |
| `+<pending>` | How many events are waiting for the host (see [Events](#events)). Only there when it isn't 0 |
| `<JSON>` | The reply body, as the HTTP API sends it. There is none with `204` |

An error is the status with the API's error object:

```text
> POST /screen/show {"path":"/clips/nope.mov"}
< 404 {"error":"not_found","message":"no /clips/nope.mov"}
> #a8 POST /holo/motion {"motion":"wag"}
< #a8 409 +1 {"error":"not_ready","message":"the holo is not armed"}
```

(`>` is what the host sends, and `<` what the board sends back. Neither is sent.)

On top of the HTTP API's errors, a line can get:

| Status | `error` | When |
|---|---|---|
| 400 | `bad_request` | The line doesn't parse: no method, no path, or the JSON is broken |
| 404 | `not_found` | The path or method isn't served over the link (see below) |
| 413 | `too_large` | The line is over 1024 bytes. The board skips to the next line feed |

### Taking turns

- **One request at a time.** The board reads a request, does it, and replies, then reads the
  next. A host sends a request and waits for its reply before sending another.
- **Allow 2 seconds** for a reply. Most take a few milliseconds. Showing a clip or applying a
  scene checks the file first, which can take a few hundred milliseconds while the storage is
  busy.
- **Lines sent while the board is busy wait** in its 2 KB receive buffer. Past that, bytes are
  lost, and `GET /link` counts an overrun.
- **Events can arrive before a reply.** An event is never sent in the middle of a reply, but one
  can come between a request and its reply (see [Events](#events)).

### What is served

The link serves what a controller needs during a show, and leaves the rest to the USB console
and Wi-Fi:

| Served | Paths |
|---|---|
| The board | `GET /info`, `POST /restart` |
| The screen | `GET`, `PATCH` and `DELETE /screen`, `POST /screen/show`, `GET /media`, `GET /media/info` |
| The LEDs | `GET` and `PATCH /leds` |
| The holo and servos | `GET /holo`, `POST /holo/motion`, `GET /servos`, `POST /servos/move`, `POST /servos/release` |
| Scenes | `GET /scenes`, `PUT` and `DELETE /scenes/scene`, `POST /scenes/apply`, `POST /scenes/end` |
| Settings | `GET`, `PATCH` and `DELETE /settings` |
| Events | `GET /events` (the kept happenings, as JSON) |
| The link | `GET` and `PATCH /link`, `GET` and `PATCH /link/events` |

Not served: files and uploads, firmware updates, the network, the web server's settings, and
servo calibration. Those need the USB console or Wi-Fi.

### No password

The link has no password, and none of the HTTP API's `Host` or `Content-Type` rules apply. A wire
into the board is physical access, as the USB console is. A board's web password doesn't apply
to its link.

## Events

Events tell the host what changed without it having to ask. They are the board's own events, the
same ones the web app follows over Wi-Fi ([`GET /api/v1/events`](http-api.md#events)), with the
same names and the same JSON. Two sorts come over the link:

**Something happened.** Each is numbered (`seq`) and stamped (`t_ms`, the board's uptime in
milliseconds):

| Event | When | Fields |
|---|---|---|
| `clip_ended` | A clip or animation has stopped | `path`, from the root of the storage volume; `finished`: `true` when it played to its end, `false` when something replaced or stopped it |
| `scene_ended` | A scene running to its end has ended | `name`; `slot`, when it has one; `then`: what it does at its end; `by`: `clip` (its clip played), `time` (its `duration_s` was up), `end` (`POST /scenes/end`), or `replaced` (another scene or clip took over, and its `then` wasn't done) |
| `touch` | The screen was touched, or let go. Only while touch reporting is on (`touch on`) | `action`: `down` or `up`; `x`, `y`: 0–239 |

**A resource changed.** The event carries the resource under `state`, exactly as its `GET`
returns it: `screen`, `leds`, `holo`, `scene` (the scene running to its end, or null), `scenes`,
`settings`, `network`, `ota` and `system`. Changes close together come as one, the latest; the
holo's at most four times a second while it moves. While a clip plays or a scene runs to its end,
`screen` and `scene` also come every 5 seconds, so a host that counts frames or time on by itself
can put itself right.

In JSON lines an event is a line that starts with `!` and a space:

```text
! {"event":"scene_ended","seq":41,"t_ms":812345,"name":"message","slot":3,"then":"restore","by":"clip"}
! {"event":"leds","state":{"mode":"solid","colour":"#ffa500","loop":false,"brightness":30,"count":16,"gpio":16}}
```

A host reading line by line can sort every line by its first character: `!` is an event, `@` or
`#` or a digit is a reply.

**`ready`** is the link's own: sent once on UART when the link starts, after a restart, whatever the
host asked for. It tells a host that the board has restarted, and that it must ask for events
again:

```text
! {"event":"ready","firmware":"v1.1.0","protocol":1,"address":1}
```

### Asking for events

**The link reports nothing until the host asks**, so a host that never reads what the board
sends is never flooded.

```text
> PATCH /link/events {"kinds":["scene_ended","clip_ended","leds"],"push":true}
< 200 {"kinds":["clip_ended","leds","scene_ended"],"push":true,"seq":0,"pending":0}
```

- `kinds`: the events the link reports, of either sort. `[]` reports none.
- `push`: whether they are sent as they happen (`true`), or kept for the host to fetch
  (`false`). Pushing works only on UART. On RS485 and I2C the board speaks only when asked, so
  events are always kept.
- **A resource's changes are only pushed.** With `push` off, and on RS485 and I2C, the host reads
  the resource itself (or the native [status block](#the-status-block)) when it wants to know.
  The pending count and `GET /link/events` count happenings only.

What a host asks for lasts until the board restarts. The link's saved settings say what it
starts with (`PATCH /link` with `events`), and start as nothing.

### Kept events, and the ones missed

- **The board keeps its last 32 happenings.** `seq` numbers every happening on the board, from 1,
  going back to 1 after 65535. It is shared with the web app's stream, so it counts kinds this
  host didn't ask for too: a gap means *something* happened that this host may not have seen.
- **Catching up**: `GET /events?after=<the last seq seen>&kinds=<its kinds>` returns the kept
  happenings of those kinds after it, as JSON. An empty list, with `lost` 0, means nothing was
  missed. `lost` is how many after `after` are no longer kept. It is how a host catches up after a
  gap, or after it restarted itself:

  ```text
  > GET /events?after=40&kinds=clip_ended,scene_ended
  < 200 {"seq":43,"lost":0,"events":[{"event":"clip_ended","seq":41,...},{"event":"scene_ended","seq":42,...}]}
  ```

- **The pending count** is how many happenings of the host's kinds it hasn't been given yet. It's
  on every reply, as `+<pending>`, whenever it isn't 0. A happening has been given once it has been
  pushed, or returned by `GET /link/events` over the link.
- **`GET /link/events`** returns the happenings not yet given, in the same shape, and counts them
  as given.

A host that doesn't want anything sent unasked can leave `push` off and still know when to look:
the `+<pending>` on the replies to its own requests says when there is something to fetch.

### Why the link needs no attention pin

A UART is two separate wires, one each way, and the board's transmit line is its own. It can send
an event whenever it happens, without a third wire to ask for the host's attention first.

The one host that needs more is one that sleeps, because a UART line alone won't wake it. For
that, the third pin can be an **ATTN** output (`attn` in the settings): it goes low while events
are pending, and high again once they've been given. It's off unless turned on.

RS485 is one pair of wires shared by everything on the bus, and a board speaks only when spoken
to. The host polls, and the pending count on each reply tells it which boards have something to
fetch. On I2C, the third pin is the [interrupt line](#i2c).

## Native frames

A native frame is a short binary message for hosts that would rather not build JSON: an ATtiny,
an Arduino Uno, or an I2C bus. Every multi-byte field is little-endian.

### On UART and RS485

A frame is [COBS](https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing)-encoded
between two zero bytes:

```text
0x00  COBS( addr | seq | type | payload… | crc16 )  0x00
```

| Field | Size | What it is |
|---|---|---|
| `addr` | 1 | The board: 1–223, a group as 223 + the group number (224–254), or 0 for every board. The board's reply carries its own address |
| `seq` | 1 | Anything the host likes. The reply carries it back |
| `type` | 1 | The message, from the table below. A reply is the request's type + 0x80 |
| `payload` | 0–48 | The message's fields |
| `crc16` | 2 | CRC-16/CCITT-FALSE (polynomial 0x1021, initial 0xFFFF, not reflected, no final XOR) over everything before it. Its check value, over the ASCII `123456789`, is 0x29B1 |

A frame with a bad CRC is dropped without a reply, and `GET /link` counts it.

With the `auto` protocol, the board tells the two apart by the first byte: a native frame opens
with 0x00, and a JSON line with `@`, `#` or a capital letter.

### Replies

Every reply's payload starts with two bytes:

| Byte | What it is |
|---|---|
| `status` | 0 for success, or an error, below |
| `pending` | Events waiting for the host, up to 255 |

| `status` | Name | The JSON error it matches |
|---|---|---|
| 0 | ok | — |
| 1 | bad request | `bad_request` |
| 2 | not found | `not_found`, `unknown_scene`: no scene with that slot, no such file |
| 3 | not playable | `not_playable` |
| 4 | not ready | `not_ready`: the holo can't move |
| 5 | busy | `busy` |
| 6 | failed | anything else |
| 7 | unknown type | — |
| 0xFE | still working | I2C only: ask again |

A board answers a request addressed to it. It answers a request to every board, or to a group,
only on UART, where it's the only board on the wire.

### Messages

| Type | Name | Request payload | Reply payload, after `status` and `pending` |
|---|---|---|---|
| 0x01 | `PING` | — | `protocol` u8, `uptime_s` u32 |
| 0x02 | `VERSION` | — | The firmware version, ASCII, up to 32 bytes |
| 0x03 | `STATUS` | — | The [status block](#the-status-block), 12 bytes |
| 0x10 | `SCENE` | `slot` u8 | — |
| 0x11 | `END` | — | — |
| 0x20 | `SCREEN_OFF` | — | — |
| 0x21 | `COLOUR` | `r`, `g`, `b` u8 | — |
| 0x22 | `BACKLIGHT` | `percent` u8, 0–100 | — |
| 0x30 | `LEDS` | `mode` u8, `r`, `g`, `b` u8, `brightness` u8, `flags` u8 | — |
| 0x40 | `HOLO` | `motion` u8, `x` s8, `y` s8, `duration_ms` u16, `range` u8, `count` u8 | — |
| 0x50 | `EVENTS_SET` | `kinds` u8, `push` u8 | `seq` u16 |
| 0x51 | `EVENTS_GET` | `after` u16 | `lost` u8, `count` u8, then `count` [event records](#event-records) (up to 5) |
| 0x7F | `RESTART` | — | — (sent before the board restarts) |

- **`SCENE`** applies the scene with that [slot](#scene-slots), as `POST /scenes/apply` does.
  **`END`** ends the scene running to its end, doing its `then`, as `POST /scenes/end` does.
- **`LEDS`**:
  - `mode`: 0 off, 1 solid, 2 wipe, 3 rainbow, 4 flicker, or 0xFF to leave it as it is;
  - `brightness`: 1–100, or 0 to leave it;
  - `flags`: bit 0 is `loop`, and bit 1 means `r`, `g`, `b` is a new colour (without it, they are
    ignored).
- **`HOLO`**:
  - `motion`: 0 center, 1 move, 2 nudge, 3 twitch, 4 wag, 5 nod, 6 scan, 7 circle, 8 stop, 9 off;
  - `x`, `y`: -100 to 100 for `move`, -128 to 127 for `nudge`;
  - `duration_ms`, `range` and `count`: 0 for the default.

  The motion's other settings take their defaults. `POST /holo/motion` over JSON lines has them
  all.
- **`EVENTS_SET`**:
  - `kinds`: a bit for each event, bit 0 `clip_ended`, bit 1 `scene_ended`, bit 2 `touch`;
  - `push`: 1 to send events as they happen (UART only), 0 to keep them.

  The reply's `seq` is the latest event's number.
- **`EVENTS_GET`** with `after` 0 returns the happenings not yet given, as `GET /link/events`
  does, and otherwise every kept one of the host's kinds after `after`, as
  `GET /events?after=` does. A host that gets 5 asks again.

### The status block

| Byte | Field | Values |
|---|---|---|
| 0 | `showing` | 0 nothing, 1 colour, 2 calibration, 3 image, 4 clip |
| 1 | `scene` | The slot of the scene running to its end; 0 for none, 0xFF for one without a slot |
| 2–3 | `remaining` | Until that scene's `duration_s` is up, in tenths of a second; 0xFFFF for none |
| 4 | `holo` | 0 hold, 1 move, 2 twitch, 3 wag, 4 nod, 5 scan, 6 circle; 0xFF when it can't move |
| 5 | `leds` | 0 off, 1 solid, 2 wipe, 3 rainbow, 4 flicker |
| 6 | `backlight` | 0–100 |
| 7 | `brightness` | The LEDs', 1–100 |
| 8–9 | `seq` | The latest event's number, 0 for none yet |
| 10 | `last_status` | The status of the last request over the link |
| 11 | `flags` | Bit 0: touch reporting is on. Bit 1: the holo is ready |

### Event records

Native frames carry happenings (and `ready`) only: a resource's state doesn't fit a record, and a
small host reads the [status block](#the-status-block) instead. Pushed on its own, an event is a
frame of type **0x70** whose payload is one record. (Replies have the top bit set, and 0x70
doesn't: no reply can be mistaken for an event.) `EVENTS_GET` returns several. A record is 8
bytes:

| Bytes | Field |
|---|---|
| 0 | `kind`: 1 `clip_ended`, 2 `scene_ended`, 3 `touch`, 15 `ready` |
| 1–2 | `seq` (0 for `ready`, which isn't numbered) |
| 3–7 | By kind, below. Unused bytes are 0 |

| Kind | Bytes 3–7 |
|---|---|
| 1 `clip_ended` | `finished` u8 (the path is only in JSON) |
| 2 `scene_ended` | `slot` u8 (0 for none), `then` u8 (0 stay, 1 restore, 2 off), `by` u8 (0 clip, 1 time, 2 end, 3 replaced) |
| 3 `touch` | `action` u8 (0 down, 1 up), `x` u8, `y` u8 |
| 15 `ready` | `protocol` u8, `address` u8 |

### I2C

On I2C, the board is a target at its address (0x42 unless changed), and the host reads and writes
registers. A transaction writes the register number, then either writes to it, or reads it after a
repeated start. There are no frames and no COBS: the I2C address is the board's address.

| Register | Name | Access | Contents |
|---|---|---|---|
| 0x00 | `STATUS` | read, 14 bytes | `protocol` u8, the 12-byte [status block](#the-status-block), `pending` u8 |
| 0x10 | `COMMAND` | write | `seq` u8, `type` u8, `payload`…, `crc16` |
| 0x11 | `RESULT` | read, up to 56 bytes | `seq` u8, `type` + 0x80, `status` u8, `pending` u8, the reply payload, `crc16` |
| 0x20 | `EVENT` | read, 8 bytes | The oldest event not yet given, as an [event record](#event-records), and given by this read. `kind` 0 when there is none |
| 0x30 | `VERSION` | read, 32 bytes | The firmware version, ASCII, padded with zeros |

- **A command runs after the write ends.** The host writes `COMMAND`, then reads `RESULT` until
  `status` isn't 0xFE (still working). The CRCs are as on UART, over every byte before them.
- **Reads don't wait for the firmware.** The board fills each register ahead of the read.
- **The interrupt line** is the third pin: open drain, pulled low while events are pending, and
  let go when there are none. The lines of several boards can share one host input. After an
  interrupt, the host reads each board's `STATUS` to see which has `pending`.
- **Broadcast.** With `general_call` on, a `COMMAND` written to address 0x00 reaches every board
  on the bus that has it on. There is no result to read.

The commands are the native messages above, less `EVENTS_GET` (read `EVENT` instead). `push`
means nothing on I2C.

## Scene slots

A scene can have a **slot**, a number from 1 to 255, which native frames and small hosts use
instead of its name. `PUT /scenes/scene?name=cantina` with `"slot": 3` in the scene sets it; so
does `scene slot cantina 3` on the console. Two scenes can't share a slot.

A slot is part of the scene, not its position in the list, so deleting another scene doesn't
move it.

## Addresses and groups

| | Where it's set | Values |
|---|---|---|
| Address | `address` in the link settings, or `link address` | 1–223. The default is 1 |
| Groups | `groups` | Up to 8 of 1–31. A group is any set of boards that should act together, such as "every holo" or "the front of the dome" |
| Every board | — | `@*` in JSON lines, 0 in native frames |

On RS485, a request needs an address, and a JSON line without `@` is ignored and counted. On
UART it can leave the address out.

## Versions

A protocol version changes only when an existing host would break. New paths, messages, event
kinds and fields arrive without it changing, so a host ignores what it doesn't know: an unknown
event, a longer status block, an extra field.
