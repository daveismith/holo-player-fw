# The board on Cyphal

!!! note "Not in the firmware yet"
    This page describes the Cyphal node ahead of the firmware, for review. Its commands can't be
    run yet.

With a [CAN transceiver](../connect/host-can.md) on the host link's pins, the board is a node on
a [Cyphal](https://opencyphal.org)/CAN bus. So far it does what every Cyphal node does:

- it gets a **node-ID**, by plug-and-play or from its settings;
- it sends a **heartbeat** once a second (`uavcan.node.Heartbeat`);
- it answers **`uavcan.node.GetInfo`** with its name, its version and its unique ID.

Scenes, the screen, the LEDs and the holo are not on Cyphal yet. The node is there to be found
and identified, by a dome controller or by a tool such as
[cyphal_service](https://github.com/daveismith/cyphal_service) or Yakut.

The bus is classic CAN, 1 Mbit/s unless changed. The USB console, Wi-Fi and the web app all work
alongside it. UART, RS485 and CAN share the same pins, so only one of them runs at a time.

## Turning it on

```text
link mode can
restart
```

`link mode off` (or another transport) and a restart turn it off again.

## The node-ID

A node-ID is the board's address on the bus, from 0 to 127.

**By plug-and-play**, which is how it starts out. The board asks the bus for a node-ID, and an
allocator on the bus gives it one. `cyphal_service` has an allocator, as does Yakut. The board
keeps asking, about once a second, for as long as it takes: with no allocator on the bus, the
rest of the board carries on as usual and `cyphal` shows it waiting.

The allocator remembers which board it gave which node-ID, so a board gets the same one every
time. The board itself doesn't save it. It asks again at every start, which takes a second or
two.

This is the plug-and-play of Cyphal v1 for classic CAN: `uavcan.pnp.NodeIDAllocationData.1.0`,
on subject 8166.

**Fixed**, for a bus with no allocator:

```text
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
- **Once running**, with a fixed or an allocated node-ID, hearing its own node-ID stops it at
  once. It sends nothing more, not even a heartbeat.

Either way, the board logs a warning on the console:

```text
W (5123) cyphal: node-ID 52 is in use by another node; Cyphal stopped
```

and `cyphal` shows why it stopped. The rest of the board is unaffected. The node stays stopped
until it's given another node-ID or told to start again (`cyphal restart`), or the board
restarts: it doesn't try again by itself, and a fixed node-ID never falls back to plug-and-play.
While stopped it still listens, so `cyphal` goes on counting the bus's traffic.

The board can only notice a node that is on the bus and sending. An allocator may have set a
node-ID aside for a node that isn't there at the moment: the board can't know that, and a fixed
node-ID that clashes with it runs until the other node turns up.

## What it reports

**Heartbeat**, once a second: the time since the node started, in seconds, with its health
nominal and its mode operational.

**`GetInfo`:**

| Field | Value |
|---|---|
| `name` | The node's name: `r2.dome.holo.front` unless changed |
| `unique_id` | 16 bytes that identify this board, below |
| `software_version` | The release's major and minor version; 0.0 for a build that isn't a release |
| `software_vcs_revision_id` | The commit the firmware was built from, when the build knows it |
| `hardware_version` | 1.0 |
| `protocol_version` | 1.0 |

### The name

The name says what the node is. The default, `r2.dome.holo.front`, is for the front
holoprojector of an R2 dome. A dome's other holos run the same firmware with another name:

```text
cyphal name r2.dome.holo.top
cyphal name --reset
```

A controller can then find every holo by the start of its name, `r2.dome.holo.`, and tell them
apart by the end, with no table of node-IDs. Swapping the dots for slashes gives a name that
works in ROS: `/r2/dome/holo/front`.

A name is 1 to 50 characters: lower-case letters, digits and `_`, in parts joined by `.`. A part
can't be empty or start with a digit. The new name is reported when the node next starts.

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
| `cyphal` | The node's state, its node-ID, name and unique ID, the bus's state, and the counters |
| `cyphal id <0-127>\|auto` | A fixed node-ID, or plug-and-play. From the node's next start |
| `cyphal name <name>\|--reset` | The name `GetInfo` reports. From the node's next start |
| `cyphal bitrate <125k\|250k\|500k\|1m>` | The bus's bitrate. From the node's next start |
| `cyphal restart` | Stop the node and start it again, with the settings as they now are |
| `cyphal reset` | Back to the firmware's defaults: plug-and-play, the default name, 1 Mbit/s |

The settings are saved, and they are part of a [backup](backup.md): the node-ID setting, the name
and the bitrate. A replacement board restored from a backup takes the old one's name and, if it
was fixed, its node-ID.

The pins are the host link's: `link pins` changes them.

## Status

```text
cyphal: node 52 (allocated), r2.dome.holo.front, 1 Mbit/s on GPIO21 (TX), GPIO33 (RX)
  unique id 686f6c6f706c61790000e4b0638c2db0
  bus: error-active, tx errors 0, rx errors 0, bus-off 0 times
  transfers: sent 1234, received 56; frames: sent 1290, received 61; dropped 0
```

The first line starts with the node's state:

| State | Means |
|---|---|
| `off` | The host link isn't set to CAN |
| `waiting for a node-ID` | Asking the bus for one; no allocator has answered yet |
| `listening` | The 2 seconds before a node with a fixed node-ID starts |
| `node 52 (allocated)` or `(fixed)` | Running |
| `stopped: node-ID 52 is in use by another node` | [Stopped](#a-node-id-that-is-already-taken), until told to start again |

The bus's state is CAN's own:

| State | Means |
|---|---|
| `error-active` | Normal |
| `error-passive` | Many errors lately. A board alone on the bus sits here, since nobody acknowledges its frames |
| `bus-off` | So many transmit errors that the controller has taken itself off the bus. The node brings it back by itself, and counts how often |

`dropped` counts transfers the board gave up on because the bus didn't take them in time. It
rises while the board is alone on the bus, and shouldn't otherwise.

## Testing the wiring: raw frames

The `can` command works below Cyphal: plain CAN frames, for checking a transceiver and a bus. It
uses the same pins and the same controller, so it runs only while the Cyphal node is off
(`link mode off`), and the node won't start while it's running.

| Command | Does |
|---|---|
| `can` | Whether it's running, the bus's state and error counters, and frames sent and received |
| `can start [-b <bitrate>] [--listen\|--selftest]` | Start the controller, at 1 Mbit/s unless told otherwise. `--listen` receives without sending or acknowledging. `--selftest` receives its own frames and needs no acknowledgement |
| `can stop` | Stop, and release the pins |
| `can send <id>#<hex>` | Send one frame. An ID of up to three hex digits is an 11-bit one; eight digits, a 29-bit one. Up to 8 data bytes |
| `can dump [on\|off]` | Print every frame received, until turned off |
| `can recover` | Bring the controller back from bus-off |

**With no second node**, a self-test shows that the controller, the two pins and the transceiver
work:

```text
can start --selftest
can dump on
can send 123#DEADBEEF
```

The frame comes back:

```text
can: 123 [4] DE AD BE EF
```

**With an adapter on the bus**, `can start` and `can dump on` show what it sends, and
`can send 1ABCDEF0#0102` should arrive there. A frame that is sent but never acknowledged leaves
`tx errors` climbing: [a first test](../connect/host-can.md#a-first-test) lists what to check.
