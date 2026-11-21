# The board on Cyphal

With a [CAN transceiver](../connect/host-can.md) on the host link's pins, the board is a node on
a [Cyphal](https://opencyphal.org)/CAN bus. So far it does what every Cyphal node does:

- it gets a **node-ID**, by plug-and-play or from its settings;
- it sends a **heartbeat** once a second (`uavcan.node.Heartbeat`);
- it answers **`uavcan.node.GetInfo`** with its name, its version and its unique ID;
- its settings are **registers** (`uavcan.register.Access` and `List`);
- it **restarts, resets or identifies itself** when told (`uavcan.node.ExecuteCommand`);
- it says which **ports** it uses every 10 seconds (`uavcan.node.port.List`).

Scenes, the screen, the LEDs and the holo are not on Cyphal yet. The node is there to be found,
identified and managed, by a dome controller or by a tool such as
[cyphal_service](https://github.com/daveismith/cyphal_service) or Yakut. What is being built is
described [below](#the-holo-on-cyphal).

The bus is classic CAN, 1 Mbit/s unless changed. The USB console, Wi-Fi and the web app all work
alongside it. UART, RS485 and CAN share the same pins, so only one of them runs at a time.

## Turning it on

```
link mode can
restart
```

The node then asks the bus for a node-ID by plug-and-play. On a bus with no allocator, give it a
fixed one first (`cyphal id 52`: any free one from 0 to 125 on your bus). `link mode off` (or
another transport) and a restart turn it off again.

## The node-ID

A node-ID is the board's address on the bus, from 0 to 127.

**By plug-and-play**, which is how it starts out. The board asks the bus for a node-ID, and an
allocator on the bus gives it one. `cyphal_service` has an allocator, as does Yakut. The board
asks at random intervals of 0.15 to 1 second, for as long as it takes, and sends nothing else
meanwhile: with no allocator on the bus, the rest of the board carries on as usual and `cyphal`
shows it waiting.

The allocator remembers which board it gave which node-ID, so a board gets the same one every
time. The board itself doesn't save it. It asks again at every start, and runs on its node-ID a
second or two after the start: never sooner than one, for the [check](#a-node-id-that-is-already-taken)
it makes first. Against `cyphal_service` on the bench it was running 1.1 seconds after its start,
and about 2 seconds after an allocator turned up on a bus where it had been waiting. It takes a node-ID from whichever node answers first: nothing in it expects a
particular allocator.

This is the plug-and-play of Cyphal v1 for classic CAN: `uavcan.pnp.NodeIDAllocationData.1.0`,
on subject 8166. A request has room for 48 bits of identity, not the whole unique ID, so the board
asks with a **pseudo-ID**: the low 48 bits of the CRC-64/WE of its [unique ID](#the-unique-id).
`cyphal` shows it in hex and in decimal; allocators show one or the other (`cyphal_service`'s
`pnp-list`, decimal). It is fixed by the unique ID, so it doesn't change either. Cyphal's own tools
send these requests without the CAN controller's automatic retransmission, so that two boards
whose requests happen to share a CAN ID (one chance in 128) collide once rather than over and over;
the board's CAN driver can't do that for some frames and not others, so its requests are retried
until acknowledged, like every other frame it sends.

**Fixed**, for a bus with no allocator:

```
cyphal id 52
cyphal id auto
```

`cyphal id auto` goes back to plug-and-play. Either takes effect when the node next starts:
`cyphal restart`, or a restart of the board. Node-IDs 126 and 127 are by convention left for
diagnostic tools.

### A node-ID that is already taken

Two nodes with one node-ID corrupt each other's transfers, so the board watches for it: a frame
on the bus that carries the board's own node-ID can only have come from another node.

- **With a fixed node-ID**, the node listens for 2 seconds before it sends anything. If it hears
  its node-ID in that time, it doesn't start.
- **With an allocated node-ID**, the node listens from its start, all the while it asks, and
  doesn't take up the node-ID it is given until it has listened for at least a second, the longest
  a node may go between heartbeats, so that a node already using it has been heard. If the node-ID
  has been heard on the bus since the start, and in the last 3 seconds (the time after which a node
  that has gone quiet counts as gone), it doesn't start, and sends nothing as that node-ID. An
  allocator whose records were lost could hand out a node-ID that way.
- **Once running**, with a fixed or an allocated node-ID, hearing its own node-ID stops it at
  once. It sends nothing more, not even a heartbeat. (A frame it had already handed to the CAN
  controller still goes; the node hands over one at a time, so that is one frame at most.)

Listening or stopped, the board stays on the bus as an ordinary CAN controller: it acknowledges
the other nodes' frames, as every node on a bus must, and only its own sending stops.

Either way, the board logs a warning on the console:

```text
W (5123) cyphal: node-ID 52 is in use by another node; Cyphal stopped
```

and `cyphal` shows why it stopped. The rest of the board is unaffected. The node stays stopped
until it's given another node-ID or told to start again (`cyphal restart`), or the board
restarts: it doesn't try again by itself, and a fixed node-ID never falls back to plug-and-play.
An allocated one isn't asked for again either: the allocator would give the same node-ID.
While stopped it still listens, so `cyphal` goes on counting the bus's traffic.

The board can only notice a node that is on the bus and sending. An allocator may have set a
node-ID aside for a node that isn't there at the moment: the board can't know that, and a fixed
node-ID that clashes with it runs until the other node turns up.

## What it reports

**Heartbeat**, once a second: the time since the node started, in seconds, with its health
nominal and its mode operational. It keeps time while the board is busy: the longest gap measured
on the bench was 1.02 seconds, during a firmware update over Wi-Fi, and 1.01 seconds while a clip
played with the LED ring running. Other nodes count a node as gone after 3 seconds without one.

**`GetInfo`:**

| Field | Value |
|---|---|
| `name` | The node's name: `r2.dome.holo.front` unless changed |
| `unique_id` | 16 bytes that identify this board, below |
| `software_version` | The release's major and minor version (a pre-release such as `v1.2.0-rc1` counts); 0.0 for a build that isn't a release |
| `software_vcs_revision_id` | The commit a build between releases was made from: the short hash in its version (`v1.1.0-2-gd9593bd` gives `0xd9593bd`). 0 in a release, whose version says which it is |
| `hardware_version` | 1.0 |
| `protocol_version` | 1.0 |

### The name

The name says what the node is. The default, `r2.dome.holo.front`, is for the front
holoprojector of an R2 dome. A dome's other holos run the same firmware with another name:

```
cyphal name r2.dome.holo.top
cyphal name --reset
```

A controller can then find every holo by the start of its name, `r2.dome.holo.`, and tell them
apart by the end, with no table of node-IDs. Swapping the dots for slashes gives a name that
works in ROS: `/r2/dome/holo/front`.

A name is 1 to 50 characters: lower-case letters, digits and `_`, in parts joined by `.`. A part
can't be empty or start with a digit. Anything else is refused with the reason, and the name stays
as it was:

```text
cyphal: not a name: a part can't start with a digit: `1holo`
```

The new name is reported when the node next starts.

### The unique ID

The allocator recognises a board by its unique ID, so it has to be the same after a firmware
update, and after the board's settings are wiped. It is built from the chip's factory MAC
address, which is burnt in and never changes:

| Bytes | Value |
|---|---|
| 0–7 | `68 6f 6c 6f 70 6c 61 79`: the letters `holoplay`, the same on every board |
| 8–9 | `00 00` |
| 10–15 | The chip's base MAC address. Its last two bytes are the `xxxx` of the board's `holo-xxxx` name |

Replacing a board therefore changes the unique ID, and the new board gets a node-ID of its own
from the allocator. To give it the old board's place on the bus, give it the old board's node-ID
as a fixed one, or change the allocator's record.

## Settings

| Command | Does |
|---|---|
| `cyphal` | The node's state, its node-ID, name, unique ID and plug-and-play pseudo-ID, the bus's state, and the counters |
| `cyphal id <0-127>\|auto` | A fixed node-ID, or plug-and-play. From the node's next start |
| `cyphal name <name>\|--reset` | The name `GetInfo` reports. From the node's next start |
| `cyphal bitrate <125k\|250k\|500k\|1m>` | The bus's bitrate. From the node's next start |
| `cyphal restart` | Stop the node and start it again, with the settings as they now are |
| `cyphal reset` | Back to the firmware's defaults: plug-and-play, the default name, 1 Mbit/s, every port's default ID |
| `cyphal register [<name> [<value>…]]` | The same settings as [Cyphal registers](#settings-as-registers), with the holo's port numbers: all of them, one, or one written |

The settings are saved, and they are part of a [backup](backup.md): the node-ID setting, the name,
the bitrate and the port numbers (NVS namespace `cyphal`: `node_id`, 255 for plug-and-play;
`name`; `bitrate`; and one for each port, `pub.status` to `srv.media_info`). A replacement board
restored from a backup takes the old one's name and, if it was fixed, its node-ID. `cyphal` shows
a setting changed since the node started on a line of its own, until `cyphal restart`.

The pins are the host link's: `link pins` changes them.

## Status

```text
cyphal: node 52 (fixed), r2.dome.holo.front, 1 Mbit/s on GPIO21 (TX), GPIO33 (RX)
  unique id 686f6c6f706c61790000e4b0638c2db0, pseudo-ID 8e8b473a6a76 (156728846609014)
  bus: error-active, tx errors 0, rx errors 0, bus-off 0 times
  transfers: sent 57, received 29; frames: sent 385, received 143; dropped 0
  up 60 s; frames given to the controller 385, not sent 0, waiting 0; not read (queue full) 0
  memory: libcanard heap 72 of 32768 bytes (1932 at most), stack 1744 bytes never used; internal heap 95327 free, 57236 lowest
  services: registers read 25, written 1, refused 0, listed 26; commands 1, bad 0; busy 0; port lists 6
  worker: 0 of 4 job slots held; its stack 4808 of 6144 bytes never used
```

The first line starts with the node's state:

| State | Means |
|---|---|
| `off` | The host link isn't running in CAN mode: `link mode can`, then restart |
| `waiting for a node-ID` | Asking the bus for one by plug-and-play, and no allocator has answered yet; or one has, and the node is still in the second it [listens](#a-node-id-that-is-already-taken) before taking it up |
| `listening, before going online as node 52 (fixed)` | The 2 seconds before a node with a fixed node-ID starts |
| `node 52 (allocated)` or `(fixed)` | Running |
| `stopped: node-ID 52 is in use by another node` | [Stopped](#a-node-id-that-is-already-taken), until told to start again |
| `not started on GPIO21 and GPIO33: …` | The CAN controller didn't start at the board's start (out of memory, say): the host link then holds no pins (`link` says `can didn't start`), and only a restart of the board tries again. Or a `cyphal restart` failed: the controller didn't start again, or `would not stop`, in which case the node is stopped and the controller still runs. `cyphal restart` tries again |

The bus's state is CAN's own, read from the two error counters beside it (the larger of them):

| State | Means |
|---|---|
| `error-active` | Normal: both counters under 96 |
| `error-warning` | A counter at 96 or more: errors lately, not yet enough to change how the controller behaves |
| `error-passive` | A counter at 128 or more: many errors lately. A board alone on the bus sits here, with `tx errors 128`, since nobody acknowledges its frames |
| `bus-off` | So many transmit errors that the controller has taken itself off the bus. The node [brings it back](#bus-off) by itself, and counts how often |

### Bus-off

A controller goes bus-off when its frames keep failing in a way that is its own fault: it sends a
bit and doesn't see that bit on the bus. On a working bus that is rare, and the node brings the
controller back within a second, with a warning in the board's log:

```text
W (4663) cyphal: bus-off (1 times since the start): recovering
```

When the board can't see its own bits at all (the transceiver's S pin high, or its VIO, RXD or TXD
not connected: [what to check](../connect/host-can.md#a-first-test)), every frame it sends takes it
bus-off again within a moment of each recovery. So when a bus-off comes less than a minute after
the recovery before it, the node waits before recovering: 1 s, then 2, 4, 8 and 16 s, and 30 s
from then on, until the bus has stayed up for a minute. The log says so at the 2nd bus-off in a
row, the 4th, the 8th, and so on, ever further apart:

```text
W (5663) cyphal: bus-off again, 2 times in a row (2 since the start): recovering in 1 s; logged again at 4 in a row
W (10693) cyphal: bus-off again, 4 times in a row (4 since the start): recovering in 4 s; logged again at 8 in a row
W (72883) cyphal: bus-off again, 8 times in a row (8 since the start): recovering in 30 s; logged again at 16 in a row
```

and `cyphal` shows the wait:

```text
  bus: bus-off, tx errors 128, rx errors 0, bus-off 7 times (back to back: recovering in 21 s)
  transfers: sent 7, received 0; frames: sent 0, received 0; dropped 41
  up 50 s; frames given to the controller 7, not sent 6, waiting 1; not read (queue full) 0
```

Each bus-off costs the frame that was on the wire (`not sent`); the heartbeats due meanwhile expire
(`dropped`, about one a second). The rest of the board runs as usual.

## The holo on Cyphal

!!! note "Partly in the firmware"
    This section describes the holo's Cyphal interface ahead of the firmware. What can be used
    now is the node's management: [its registers](#settings-as-registers) and
    [its commands](#restart-factory-reset-identify). The holo's own messages and services aren't
    built yet. The [Cyphal interface](../reference/cyphal.md) reference has every type, port and
    register.

A controller on the bus, the dome panel or an app on a Linux machine, will be able to do with
this holo what the panel does with a holo of its own: every motion, the light, direct moves of
the axes, the holo's state, and letting go of the servos. It will also reach most of the board's
own features: scenes, the screen, the LED ring, and the board's events. And the node will be
managed the standard Cyphal way, with registers and commands, by any Cyphal tool.

The holo's messages and services are Cyphal data types of their own, `r2.holo.*`, kept in the
[r2_cyphal_dsdl](https://github.com/daveismith/r2_cyphal_dsdl) repository, which a controller
generates its code from. Everything else (the heartbeat, `GetInfo`, registers, restart) uses
Cyphal's standard types.

### What a controller can do

| To | It | Default port |
|---|---|---|
| Know what the holo is doing | Subscribes to `r2.holo.Status` | subject 2300 |
| Hear what happens: clips and scenes starting and ending, touches | Subscribes to `r2.holo.Event` | subject 2301 |
| Know where the axes are | Subscribes to `r2.holo.Axes` | subject 2302 |
| Move the axes directly | Publishes `r2.holo.Axes`, a stream of setpoints | subject 2303 |
| Start a motion | Calls `r2.holo.Motion` | service 100 |
| Set the holo's light | Calls `r2.holo.Light` | service 101 |
| Apply or end a scene | Calls `r2.holo.Scene` | service 102 |
| Show a clip, an image or a colour, or nothing; set the backlight | Calls `r2.holo.Screen` | service 103 |
| Set the LED ring | Calls `r2.holo.Ring` | service 104 |
| List the saved scenes | Calls `r2.holo.ListScenes` | service 105 |
| Change a setting, or a port's number | Calls `uavcan.register.Access` | service 384 |
| Restart the board, reset it, find it | Calls `uavcan.node.ExecuteCommand` | service 435 |

Each service does what the same request over the [HTTP API](../reference/http-api.md) or the
[host link](host-link.md) does, through the same code, and refuses it for the same reasons:
the answer carries the host link's status code (ok, bad request, not found, not playable, not
ready, busy, failed) and the message in words, such as `holo: refused, uncalibrated (tilt)`.

### Moving the holo

`r2.holo.Motion` starts any of the holo's motions, with every parameter the console's
[`holo`](holo.md#motions) takes: center, move, nudge, twitch, wag, nod, scan, circle, stop, and
off, which stops, lets both servos go limp and turns the light off. A parameter left at 0 takes
the motion's default, as on the console.

For a direct move, a controller streams setpoints instead: `r2.holo.Axes` messages with both
axes in hundredths of a percent of their travel each side of the centre (−10000 to 10000). Each
setpoint goes to the servos at once, with no easing, and stops any motion running first; the
latest wins. This is what a joystick or a ROS joint command needs. The holo publishes where it
has put the axes on the same scale, ten times a second while it moves and once a second at rest.

A subject belongs to the whole bus, so two holos listening for setpoints on the same subject
move together. Give a second holo its own setpoint subject by its register,
`uavcan.sub.setpoint.id`.

### The light is the LED ring

A dome panel's own holos have a single-colour light: off, on, pulse, flicker and blink, at a
level, each for good or for a time, after which the light goes back to what it was doing; and
`leia`, the message from Leia, a flicker at full with the holo turned to the centre. On this
board `r2.holo.Light` does all of that on the LED ring: `on` lights it steady, and the others are
ring modes of the same names. The ring keeps its colour unless the request gives one.

The ring gains pulse and blink, and timed modes, for this; the console's `leds` and the HTTP API
gain them too. The console's `holo led` and `holo leia` belong to the holo engine shared with the
panel, and still do nothing on this board.

### Knowing what it is doing

`r2.holo.Status` is one message with everything: the holo's motion (ready or why not, what runs,
for how long, why the last one stopped early, the last motion asked for), the ring (including a
timed mode and what it goes back to), the screen (what shows, a clip's progress, the backlight)
and the scene running to its end. It goes out within a quarter of a second of a change, and once
a second whatever changes.

`r2.holo.Event` carries the board's happenings, the same ones as its
[JSON events](../reference/http-api.md#events): a clip or a scene starting or ending, with its
path or name, and a touch on the screen while touch reporting is on. Each is numbered with the
board's sequence number. There is no catching up on Cyphal: a controller that sees a number
skipped has missed something, and reads the next `Status` for how things are now.

### Settings as registers

The node's settings are Cyphal registers, which any Cyphal tool lists, reads and writes:

| Register | The setting | Default |
|---|---|---|
| `uavcan.node.id` | The node-ID, 0–127; 65535 for plug-and-play (`cyphal id`) | 65535 |
| `r2.node.name` | The name `GetInfo` reports (`cyphal name`) | `r2.dome.holo.front` |
| `uavcan.can.bitrate` | The bitrate (`cyphal bitrate`) | 1000000 |
| `uavcan.pub.status.id`, … `uavcan.srv.ring.id`, … | Each port's number; 65535 turns the port off | The defaults above |

They are the same saved settings the `cyphal` commands change, and the console reads and writes
the registers by name too:

```
cyphal register
cyphal register uavcan.sub.setpoint.id
cyphal register uavcan.sub.setpoint.id 2313
```

```text
uavcan.sub.setpoint.id       2313                   natural16[1], mutable, persistent; 2303 until the node's next start
```

A change takes effect when the node next starts (`cyphal restart`). A value the board can't take
is refused with the reason, and the register keeps its value:

```text
cyphal: uavcan.sub.setpoint.id not written: a subject-ID is 0 to 8191, or 65535 to turn the port off
```

A number another port already has is refused as well, with the port that has it: the subjects,
published or subscribed, are one set and the services another, and the node's own standard ports
count ([the rule](../reference/cyphal.md#port-numbers-in-use)). To swap two ports' numbers, move one
to a free number, or to 65535, first.

Over Cyphal the response says the same by carrying the old value. Until the firmware has a port,
its number is only kept: the holo's ports aren't built yet, so none of them is used, whatever its
register holds. Later the holo's calibration, its servos' drive policy and touch reporting become
registers as well.

The port numbers are saved and go into a [backup](backup.md) with the rest of the node's
settings. A second holo restored from the same backup needs its own node-ID and its own setpoint
subject.

### Restart, factory reset, identify

`uavcan.node.ExecuteCommand`, the standard command service:

- **restart** restarts the board, a second after it answers;
- **factory reset** puts every register and the board's settings back to their defaults and
  restarts, keeping the scenes, the files, the servos' calibration, the network settings and the
  host link's mode (so the node comes back on the bus, asking for a node-ID by plug-and-play);
- **identify** flickers the ring white and shows the crosshair for 5 seconds, to tell which board
  on the bus is which, then puts both back. It runs as a short scene called `identify`, so the web
  app shows it, and it ends a scene that was running.

Every other command, power off and emergency stop among them, is refused as a bad command. The
board's log says which node asked.

### From a computer

[Yakut](https://github.com/OpenCyphal/yakut) speaks all of this, given the standard types and
`r2_cyphal_dsdl` on its `CYPHAL_PATH`: the folders that hold `uavcan` and `r2`. With the board at
node-ID 52, it would read a register, start a wag, and restart the board:

```sh
export CYPHAL_PATH="$HOME/public_regulated_data_types:$HOME/r2_cyphal_dsdl"
yakut register-access 52 uavcan.srv.motion.id
yakut call 52 motion '{motion: {kind: 4, count: 3}}'
yakut execute-command 52 restart
```

(The call to `motion` waits for the holo's services; the register and the restart work now.)

On macOS a gs_usb adapter (a CANable, candleLight) needs a workaround first: python-can, which
Yakut uses, asks the system to detach a kernel driver, and macOS refuses without root. This
repository's `tools/yakut_can.py` runs Yakut with it, and with the adapter opened and closed only
while the board is quiet, which a candleLight needs; it sets the transport and `CYPHAL_PATH` from
the repository's own copies of the types, and wants the board on a fixed node-ID:

```sh
tools/yakut_can.py register-list 52
tools/yakut_can.py register-access 52 uavcan.srv.motion.id
tools/yakut_can.py register-access 52 uavcan.pub.status.id 2310
```

It refuses `execute-command`, and a `call` to `uavcan.node.ExecuteCommand`: on the bench a
candleLight now and then stopped passing frames to the computer during a Yakut session, every time
a command was sent and sometimes otherwise, and only the adapter's own bootloader brought it back.
The repository's hardware tests send the commands instead. It also refuses what would leave the
board never quiet when the adapter has to close (a write of 65535, plug-and-play, to
`uavcan.node.id`: use `cyphal id auto` on the console), `register-batch`, and a `--bitrate` other
than the board's.

### What stays off Cyphal

Uploading clips and images, the network and the web app's settings, passwords, the configuration
backup and firmware updates stay on Wi-Fi and the USB console. So do saving and editing scenes:
Cyphal applies a saved one. The [reference](../reference/cyphal.md#not-on-cyphal) has the whole
list.

`transfers` counts what was sent, whole (heartbeats, port lists, responses and plug-and-play
requests) and what was received (requests and plug-and-play messages); `frames` counts
what the controller sent and had acknowledged, and what it received. `dropped` counts transfers
the board gave up on because the bus didn't take them in time (a second, a heartbeat's period), or
that it couldn't queue or put together at all.
It rises while the board is alone on the bus, about once a second as each heartbeat expires
(a little less while it waits for a node-ID: each request expires in turn, and the next isn't
queued until it has), and shouldn't otherwise. The frame the controller was retrying then goes out when another node turns up, a
little late.

The next two lines are for when something seems wrong: how long the node has run, frames it
handed to the controller that were never sent (bus-off, or a restart of the node), frames that
arrived faster than they could be read, and the memory it uses. Its libcanard heap is 32 KB of
PSRAM, reserved at its first start; a port list waiting to go takes about 1.8 KB of it. The
internal RAM it takes is its task's 3 KB stack while it runs, the worker's 6 KB stack (the task
that writes registers and carries out commands, so that the node's own never waits) from its first
start on, and the CAN driver's queues (about 8 KB, kept once the controller has run, the same as
for `can`).

`services` counts the register and command requests, the ones refused, and those answered busy
because the worker already held four; `worker` shows how many it holds now.

## Testing the wiring: raw frames

The `can` command works below Cyphal: plain CAN frames, for checking a transceiver and a bus. It
uses the same pins and the same controller, so it runs only while the Cyphal node is off
(`link mode off`), and the node won't start while it's running. While the node holds the
controller, `can` alone still reports the bus (its first line ends `for the Cyphal node`), and
every `can` sub-command refuses:

```text
can: the controller is in use by the Cyphal node: `can` only reports while it is
```

| Command | Does |
|---|---|
| `can` | Whether it's running, the bus's state and error counters, and frames sent and received |
| `can start [-b <bitrate>] [--listen\|--selftest]` | Start the controller, at 1 Mbit/s unless told otherwise. `--listen` receives without sending or acknowledging. `--selftest` receives its own frames and needs no acknowledgement |
| `can stop` | Stop, and release the pins as floating inputs. `dump` and `echo` go off, so the next `can start` is quiet |
| `can send <id>#<hex>` | Send one frame. An ID of up to three hex digits is an 11-bit one; eight digits, a 29-bit one. Up to 8 data bytes |
| `can dump [on\|off]` | Print every frame received, until turned off (`on` if neither is given) |
| `can echo [on\|off]` | Send every frame received straight back, with 1 added to its ID |
| `can recover` | Bring the controller back from bus-off |

**With no second node**, a self-test shows that the controller, the two pins and the transceiver
work:

```
can start --selftest
can dump on
can send 123#DEADBEEF
```

The frame comes back:

```text
can: 123 [4] DE AD BE EF
```

`can stop` ends the self-test. A board that can't see its own bits on the bus fails it: the
frame never comes back, and the controller goes bus-off at once (`can` reads `bus-off`, `bus-off 1
times`, `waiting to send 1`). The transceiver's S pin high (silent), or its VIO, RXD or TXD not
connected, does that.

**With an adapter on the bus**, `can start` and `can dump on` show what it sends, and
`can send 1ABCDEF0#0102` should arrive there. A frame that is sent but never acknowledged leaves
`tx errors` climbing: [a first test](../connect/host-can.md#a-first-test) lists what to check.

`can` alone reports:

```text
can: running at 1 Mbit/s on GPIO21 (TX), GPIO33 (RX), normal
  bus: error-active, tx errors 0, rx errors 0, bus errors 0, bus-off 0 times
  frames: sent 4, received 1000; send failed 0, receive dropped 0 (queue held 5 of 256 at most), waiting to send 0
  dump off, echo off; internal heap 107259 bytes free, 69388 lowest
```

`bus errors` counts every error the controller has seen on the bus since `can start`, not
counting lost arbitration: thousands a second while nobody acknowledges, since each retry of the
frame is one. `send failed` counts frames that never made it: the one on the wire when the
controller went bus-off, and any still waiting at `can stop`. While the controller is bus-off,
`can send` refuses (``can: bus-off: `can recover` first``); frames already waiting behind the bus-off
go out after `can recover`, which brings the controller back once the bus has been idle for 128
periods of 11 bits. Unlike the node, `can` never recovers by itself.
`receive dropped` counts frames that arrived faster than the board could take them, which `can
dump` causes on a busy bus: printing is slower than CAN. The queue between the controller and
the board's reader holds 256 frames, enough to ride out a flash write on a bus full of 8-byte
frames; `queue held` says how full it has been since `can start`. With `--listen`, the bus
always reads `error-passive` with `rx errors 128`: that keeps the controller from ever sending
an error flag.
While `can` runs, `gpio` refuses its two pins.

`tools/can_check.py`, in this repository, is the computer's side, through a gs_usb adapter
(candleLight, CANable): `listen`, `send`, `flood`, and `echo`, which sends numbered frames to a
board running `can echo on` and checks that every one comes back, in order. An adapter keeps
retrying a frame nobody acknowledged, even after it is closed and opened again; `can start` on
the board acknowledges it and clears it.
