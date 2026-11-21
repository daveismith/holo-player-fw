# Cyphal interface

!!! note "Partly in the firmware"
    This page describes the holo's Cyphal interface, part of it ahead of the firmware. Built so far:
    the node itself (its node-ID, its heartbeat, `uavcan.node.GetInfo`: [the board on
    Cyphal](../use/cyphal.md)), its [registers](#registers) (the node's and the ports'), its
    [commands](#commands), `uavcan.node.port.List`, and the holo's [Motion](#motion) and
    [Light](#light) services, [Axes](#axes) published and subscribed, and [Status](#status) with its
    holo and ring parts. Not yet: `Event`, `Scene`, `Screen`, `Ring`, `ListScenes`, `MediaInfo`, the
    file services, the holo's registers, and Status's screen and scene parts (which read as nothing
    showing and no scene). What isn't built may still change before it is.

This is the specification of what the board does on a [Cyphal](https://opencyphal.org)/CAN bus:
every port, every type and its fields, every register and every command. How to use it is on
[the board on Cyphal](../use/cyphal.md#the-holo-on-cyphal).

Node management uses Cyphal's standard types, from
[public_regulated_data_types](https://github.com/OpenCyphal/public_regulated_data_types). The holo's
own types are `r2.holo.*`, in a repository of their own,
[r2_cyphal_dsdl](https://github.com/daveismith/r2_cyphal_dsdl), which a controller (the dome
panel, an app on Linux) generates its code from too. They are at version **0.1**, unstable:
they become 1.0 when the dome panel is first written against them, and from then on a type that
has shipped is never changed, only given a new version.

## The node's ports

| Function | Type | Port | Sent or served |
|---|---|---|---|
| Heartbeat | `uavcan.node.Heartbeat.1.0` | subject 7509 | Once a second. Its health is advisory while the holo can't move ([below](#health)) |
| Node information | `uavcan.node.GetInfo.1.0` | service 430 | Its name, version and unique ID |
| Plug-and-play | `uavcan.pnp.NodeIDAllocationData.1.0` | subject 8166 | Only while it asks for a node-ID |
| The ports it uses | `uavcan.node.port.List.1.0` | subject 7510 | Every 10 seconds, the first as the node goes online. [Below](#the-port-list) |
| Registers | `uavcan.register.Access.1.0`, `uavcan.register.List.1.0` | services 384, 385 | [Below](#registers) |
| Restart, factory reset, identify | `uavcan.node.ExecuteCommand.1.3` | service 435 | [Below](#commands) |
| The clips and images | `uavcan.file.List.0.2`, `uavcan.file.GetInfo.0.2` | services 406, 405 | The file names and sizes on the storage volume |
| What the holo is doing | `r2.holo.Status.0.1` | subject 2300 | Within 250 ms of a change, and once a second |
| What happened | `r2.holo.Event.0.1` | subject 2301 | As it happens |
| Where the axes are | `r2.holo.Axes.0.1` | subject 2302 | Ten times a second while the holo moves, once a second at rest |
| Where the axes go | `r2.holo.Axes.0.1` | subject 2303, subscribed | A stream of setpoints |
| Motions | `r2.holo.Motion.0.1` | service 100 | |
| The light | `r2.holo.Light.0.1` | service 101 | |
| Scenes | `r2.holo.Scene.0.1` | service 102 | |
| The screen | `r2.holo.Screen.0.1` | service 103 | |
| The LED ring | `r2.holo.Ring.0.1` | service 104 | |
| The scene list | `r2.holo.ListScenes.0.1` | service 105 | |
| A file's details | `r2.holo.MediaInfo.0.1` | service 106 | Optional: it may be left out |

The standard ports' IDs are fixed by the standard. The `r2.holo` ports' are defaults, in
Cyphal's unregulated ranges, which [registers](#registers) change. `r2_cyphal_dsdl`'s
`ports/holo.csv` holds the same defaults for a program to read.

| Port name | Kind | Default ID | Priority |
|---|---|---|---|
| `status` | Published | subject 2300 | low |
| `event` | Published | subject 2301 | nominal |
| `axes` | Published | subject 2302 | high |
| `setpoint` | Subscribed | subject 2303 | fast |
| `motion` | Served | service 100 | nominal |
| `light` | Served | service 101 | nominal |
| `scene` | Served | service 102 | nominal |
| `screen` | Served | service 103 | nominal |
| `ring` | Served | service 104 | nominal |
| `list_scenes` | Served | service 105 | nominal |
| `media_info` | Served | service 106 | nominal |

The priority is the one the board sends at; a client calls a service at the same. Services are
addressed to the board's node-ID, so two holos on one bus serve them on the same IDs without
trouble. A subject is shared by the whole bus: two holos can publish `status` on 2300 (a
receiver tells them apart by node-ID), but two holos subscribed to `setpoint` on 2303 move
together, so each gets a subject of its own.

### The port list

`uavcan.node.port.List` names the ports the node really has while it runs, at the optional
priority, as the type asks. A port's number changes only when the node starts (the registers apply
from then), so the list does too: it goes out as the node goes online, and every 10 seconds after.
With the registers at their defaults it holds:

| | |
|---|---|
| Publishers | 7509 (the heartbeat), 7510 (the port list), 2300 (`status`), 2302 (`axes`) |
| Subscribers | 2303 (`setpoint`) |
| Clients | None |
| Servers | 100 (`motion`), 101 (`light`), 384 and 385 (registers), 430 (`GetInfo`), 435 (`ExecuteCommand`) |

An `r2.holo` port is in it once the firmware serves, publishes or subscribes to it, on the number
its register held as the node started; a port turned off (65535) is never in it, nor is one the
firmware doesn't have yet (`event`, `scene`, `screen`, `ring`, `list_scenes`, `media_info`), whatever
its register holds. It is 158 bytes, 23 frames.

### Health

The heartbeat's health is **advisory** while the holo can't move, for the reasons
[Status](#the-holo) gives in `ready` and `why`, and **nominal** otherwise. The node's mode is always
operational. On this board the holo can't move only when one of its servos didn't attach at the
start (not fitted), or the holo engine didn't start (Status then reads `not fitted` for no axis in
particular): an axis without saved endpoints moves over its default travel, so it doesn't make the
health advisory (Status's `calibrated` still says whether its endpoints are saved). A board with no
holo wired to it at all can't tell: its servos are outputs that read nothing back, so it reports
the holo ready, and nominal.

## Conventions

- **Units are in the names**: `_ms` is milliseconds, `_us` microseconds. A percent is 0–100
  unless the field says otherwise.
- **Zero means the default.** A request field left at 0 takes its default wherever it has one, so
  a request that is all zeros asks for every default. Where 0 is a value in its own right (a
  move's duration, the light's level, the backlight, the ring's `off`), the field is optional
  instead: an array of at most one (`[<=1]`), empty for the default.
- **Times in `Status` are spans from the moment it was sent**, `*_age_ms` back and
  `*_remaining_ms` forward, never stamps, so the receiver's clock needn't be the board's.
  **`NEVER`** (0xFFFFFFFF) is "no such time".
- **Paths** are from the root of the storage volume (`/clips/intro.mov`), as
  `uavcan.file.Path.2.0`: up to 255 bytes in the type, at most 159 on this board.
- **Names** of scenes are 1–32 of `A-Z a-z 0-9`, space, `_`, `.` and `-`.
- **Sizes.** Classic CAN carries 7 bytes of a transfer a frame. A transfer of up to 7 bytes is one
  frame; a longer one adds a 2-byte CRC. [Sizes and bus load](#sizes-and-bus-load) has each
  type's.

## Messages

### Status

`r2.holo.Status.0.1`, published on `status`: everything the holo is doing, in one message,
so that each is a consistent snapshot. It goes out within 250 ms of a change, never more than
four times a second, and once a second whatever changes. A change is a change to any field but
a time or a clip's progress (`*_age_ms`, `*_remaining_ms`, `shown`, `passes`), which the
once-a-second message keeps current. The board builds it from the same state `GET /api/v1/holo`
and `GET /api/v1/leds` report. Once another Status may go (250 ms after the last), it looks at the
holo for a change every 50 ms; the ring tells it of its own changes, as it tells the HTTP API's
event stream. Until the screen and scenes are
on Cyphal, `screen` reads as nothing showing (every field 0 or empty) and `scene` as none running:
86 bytes, 13 frames.

| Field | Type | What it is |
|---|---|---|
| `holo` | `state.Holo` | The holo's motion, [below](#the-holo) |
| `ring` | `state.Ring` | The LED ring, which is also the holo's light, [below](#the-ring) |
| `screen` | `state.Screen` | The screen, [below](#the-screen) |
| `scene` | `state.Scene` | The scene running to its end, [below](#the-scene) |
| `event_seq` | uint16 | The latest [Event](#event)'s `seq`; 0 before the first. A receiver that sees it move past the last event it got has missed one |
| `reporting_touch` | bool | Touch events are on (the register `r2.touch.reporting`, or `touch on`) |

#### The holo

`r2.holo.state.Holo.0.1`: what `GET /api/v1/holo` and the console's `holo` report, and what the
dome panel shows for a holo of its own.

| Field | Type | What it is |
|---|---|---|
| `ready` | bool | The holo could move now |
| `why` | [Reason](#reason) | Why it can't, and the axis at fault. NONE when ready |
| `motion` | uint8 | What runs now: 0 hold, 1 move (a center, move or nudge on its way), 2 twitch, 3 wag, 4 nod, 5 scan, 6 circle. The numbers of the host link's status block |
| `motion_age_ms` | uint32 | Since the running motion began; NEVER while holding |
| `motion_remaining_ms` | uint32 | Until a timed twitch or scan ends; NEVER with no limit, or while holding. A timed scan doesn't end by its time limit today ([Motion](#motion)'s `time_limit_ms`): this counts down to 0 and stays there while the scan goes on |
| `stopped` | [Reason](#reason) | Why the last motion ended early: an axis refused it partway, with the axis. NONE when it ended by itself, by `stop`, or by another motion |
| `stopped_motion` | uint8 | Which motion that was, numbered as `motion`; 0 when `stopped` is NONE |
| `stopped_age_ms` | uint32 | How long ago it stopped; NEVER when `stopped` is NONE |
| `horizontal`, `vertical` | `state.Axis` | Each axis: `fitted` (its servo is there), `calibrated` (its endpoints are saved), `inverted` (closed is above open), `placed` (something has moved it since the board started, so [Axes](#axes) knows where it is). The engine places both axes together, so `placed` is the same for both |
| `last` | [MotionSpec](#motion) | The last motion asked for, by anybody (Cyphal, the console, the host link, the HTTP API, a scene), as it was asked. A `stop` before any |

The axes' positions are in [Axes](#axes), not here.

#### The ring

`r2.holo.state.Ring.0.1`: what `GET /api/v1/leds` reports (its `timed` and `leia` too), with the
holo light's timed modes.

| Field | Type | What it is |
|---|---|---|
| `now` | `state.RingSetting` | What it is doing now |
| `remaining_ms` | uint32 | A timed mode (a [Light](#light) with a time limit, or a leia): until it goes back to `after`. NEVER when not timed |
| `after` | `state.RingSetting` | What a timed mode goes back to. Meaningful only while `remaining_ms` isn't NEVER |
| `leia` | bool | The timed flicker is a leia's, whoever asked for it |

`r2.holo.state.RingSetting.0.1`:

| Field | Type | What it is |
|---|---|---|
| `mode` | uint8 | 0 off, 1 solid, 2 wipe, 3 rainbow, 4 flicker, 5 pulse, 6 blink. The first five are the host link's `LEDS` numbers |
| `colour` | `Colour` | `r`, `g`, `b`, 0–255. For every mode but off and rainbow |
| `brightness` | uint8 | Percent, 0–100: how bright the ring is in this setting, its brightness (`leds bright`, `GET /api/v1/leds`'s `brightness`) times the setting's [level](#light) (its `level`), rounded |
| `period_ms` | uint16 | Pulse, blink: 200–60000. 0 for the other modes |
| `loop` | bool | Wipe, rainbow: again and again, rather than once |

#### The screen

`r2.holo.state.Screen.0.1`: what `GET /api/v1/screen` reports.

| Field | Type | What it is |
|---|---|---|
| `showing` | uint8 | 0 nothing, 1 colour, 2 the calibration crosshair, 3 image, 4 clip (or animated GIF). The host link's status block numbers |
| `backlight` | uint8 | Percent, 0–100 |
| `colour` | `Colour` | When showing a colour: which. Black otherwise |
| `frames` | uint16 | A clip: frames in one pass, from its header |
| `shown` | uint32 | A clip: frames shown since it started, every pass |
| `passes` | uint16 | A clip: times through so far (the JSON API's `loops`) |
| `plays` | uint16 | A clip: times through in all, 1–1000; 0 for forever |
| `fps` | float16 | A clip: frames a second, from its header |
| `path` | `uavcan.file.Path.2.0` | An image or a clip: the file. Empty otherwise |

The clip's fields are 0 unless a clip shows.

#### The scene

`r2.holo.state.Scene.0.1`: the scene running to its end, the JSON API's `active` (`GET
/api/v1/scenes`). Only a scene whose `then` is `restore` or `off` runs to an end; a `stay` scene
is applied and done, and leaves nothing running.

| Field | Type | What it is |
|---|---|---|
| `running` | bool | A scene is running to its end. Without one the fields below are 0 or empty |
| `then` | uint8 | What follows its end: 1 restore, 2 off |
| `until_clip_ends` | bool | It ends when its clip has played |
| `remaining_ms` | uint32 | Until its `duration_s` is up; NEVER when it has none |
| `slot` | uint8 | Its [slot](host-protocol.md#scene-slots), 1–255; 0 when it has none |
| `name` | uint8[<=32] | Its name |

### Event

`r2.holo.Event.0.1`, published on `event`: one of the board's happenings, the same ones its
[JSON events](http-api.md#events) and the host link's event records carry. Each is published
once, as it happens, and there is no catching up: a receiver that misses one sees the gap in
`seq` (or in Status's `event_seq`) and reads [Status](#status) for what is going on now.

| Field | Type | What it is |
|---|---|---|
| `seq` | uint16 | The board's number for this happening: 1–65535, then 1 again. One count for every happening on the board, shared with the web app and the host link |
| `what` | union | One of the five below |

| `what` | Fields | The JSON event |
|---|---|---|
| `clip_started` | `plays` uint16 (times through in all, 1–1000; 0 forever), `frames` uint16 (in one pass), `fps` float16, `path` | `clip_started` |
| `clip_ended` | `finished` bool (played to its end; false when something replaced or stopped it), `path` | `clip_ended` |
| `scene_started` | `then` uint8 (0 stay, 1 restore, 2 off), `until_clip_ends` bool, `duration_ms` uint32 (0 for none), `slot` uint8 (0 for none), `name` | `scene_started` |
| `scene_ended` | `then` uint8, `by` uint8 (0 its clip played, 1 its time was up, 2 it was ended, 3 replaced), `slot` uint8, `name` | `scene_ended` |
| `screen_touch` | `action` uint8 (0 down, 1 up), `x`, `y` uint8 (0–239, from the top left). Only while touch reporting is on | `touch` |

The paths and names are carried whole, as the JSON events carry them, so a receiver needn't ask
which clip ended. The order is the JSON events' order: a scene's clip starts just before its
`scene_started`, and its `scene_ended` comes before what its `then` does.

### Axes

`r2.holo.Axes.0.1`: both axes, in hundredths of a percent of the calibrated travel each side of
the centre. One frame.

| Field | Type | What it is |
|---|---|---|
| `x` | int16 | Horizontal: -10000 (closed, full left) to 10000 (open, full right); 0 the middle |
| `y` | int16 | Vertical: -10000 (full down) to 10000 (full up) |

`NONE`, -32768, is "no value" in either.

- **Published on `axes`**: where the board last put each axis. A servo reports nothing back, so
  this is the position commanded, not one measured. `NONE` for an axis nothing has moved since
  the board started.
- **Subscribed on `setpoint`**: a direct move. Each setpoint goes to the axes at once, with no
  easing, and stops the motion running first; the latest wins. `NONE` leaves that axis where it
  is. A value beyond ±10000 is held to it. While the holo can't move, a setpoint moves nothing
  and nothing answers it: [Status](#status) says why.

  The board does what the dome panel does for a ROS joint command: the motion running, if any, is
  stopped (Status then shows `stop` as the last motion asked for), and each axis given goes
  straight to its place through the servo layer, the call the holo engine's own moves end in. A
  setpoint received while the board is still carrying out the one before replaces it. The first
  setpoint after the board starts, before anything has placed the axes, goes through the engine as
  a `move` with no duration instead, so that the engine knows from then on where they are: an axis
  given as `NONE` in that one goes to the centre, where the engine takes an axis nothing has placed
  to be. A `move` is in whole percent, so once the engine has made it (within 20 ms) the same
  setpoint goes again the direct way, to its exact place; Status shows that `move` as the last
  motion asked for.

The resolution is finer than the holo's own, so nothing is lost either way: the engine places an
axis in steps of 0.2 % of the travel each side (20 hundredths of a percent), and the pulse goes out
in whole microseconds, which over this board's default travel is coarser still (0.27 % on pan,
0.31 % on tilt). A position published is the pulse sent, read back against the endpoints, so it can
be up to half a percent short of the setpoint that put the axis there.

## Services

Every service answers with an [Error](#error) as `result`: `code` 0 when it was done. A service
does what the same request over the HTTP API or the host link does, through the same code, with
the same refusals. A request that opens a file or waits (showing a file, applying a scene) is
answered when it is done, a few hundred milliseconds at most; while the board is still working
through the requests already sent to it, another is answered `busy`.

### Motion

`r2.holo.Motion.0.1`, on `motion`: start a motion of the holo, replacing the one running, as
`POST /api/v1/holo/motion` and the console's `holo` do. The request is one field, `motion`, an
`r2.holo.MotionSpec.0.1`; three frames.

| Field | Type | What it is |
|---|---|---|
| `kind` | uint4 | 0 center, 1 move, 2 nudge, 3 twitch, 4 wag, 5 nod, 6 scan, 7 circle, 8 stop, 9 off. `off` stops, lets both axes go limp, and turns the light off: the release. On this board the light is the LED ring, which `off` turns off (the HTTP API's and the host link's `off` leave the ring alone) |
| `range` | uint7 | Percent of the travel each side of the centre, 1–100. 0: the motion's default, twitch 60, scan 70, wag 50, nod 40, circle 50 |
| `count` | uint5 | Wag, nod: cycles; circle: turns. 1–20. 0: wag 3, nod 2, circle 1 |
| `x` | int16 | Move: −100 to 100, percent of the way from the centre to an endpoint, + right. Nudge: −200 to 200, relative. Needed by move and nudge |
| `y` | int16 | As `x`; + is up |
| `period_ms` | uint16 | A cycle of wag, nod, a turn of circle, a sweep of scan there and back: 200–60000. 0: wag 700, nod 800, circle 2400, scan 6000 |
| `interval_min_ms`, `interval_max_ms` | uint20 | Twitch: the pause between glances, chosen between the two: each 100–600000, min no more than max. Both 0: 1000 to 5000. Both or neither |
| `time_limit_ms` | uint22 | Twitch, scan: stop, and hold, after this long: 100–3600000. 0: no limit. A timed scan doesn't stop yet, over Cyphal, the console or the HTTP API alike: a fault in the shared holo engine, which looks at the time only between a motion's segments, and a scan has no gap between them |
| `duration_ms` | uint16[<=1] | Center, move, nudge: how long to take, 0–10000; 0 is at once. Empty: by the distance, 250 ms and 500 ms more for each half of the travel |

A field the motion doesn't use is ignored, whatever it holds. The odd widths are the ranges', to
keep the request to three frames.

The board checks and starts the motion with the code `POST /api/v1/holo/motion` runs once it has
read its JSON, as the host link's requests do: the fields the motion uses, the times in seconds as
the route's `interval_s` and `time_s` are, and nothing else. So a motion is checked and refused as
one over HTTP is, in the same words, which name the JSON API's fields:

| `result.code` | When |
|---|---|
| bad request | A field out of range: `` `range` is 1..100``, `` `x` and `y` for move are -100..100``, `` `interval_s` is [min, max], each 0.1..600, min first`` (one of the twitch's interval fields alone, or min over max), `` `time_s` is 0.1..3600``. A `kind` that is none: `` `motion` is center, move, nudge, twitch, wag, nod, scan, circle,`` (the JSON API's words, cut, as every message is, to 64 bytes) |
| not ready | The holo can't move: `result.reason` says why (not fitted, uncalibrated) and which axis, and `result.message` says it in words: `holo: refused, uncalibrated (tilt)` |
| busy | The board is still working through four earlier requests: send it again |

Nothing moves when a motion is refused. `stop` and `off` are never refused for the holo's state.
This board moves an axis without saved endpoints over its default travel
([the holo](../use/holo.md#calibrating-the-travel)), so it never refuses one as uncalibrated: only an
axis whose servo didn't attach is refused, as not fitted.

### Light

`r2.holo.Light.0.1`, on `light`: the holo's light, with the modes a dome panel's own holos have.
On this board the light is the LED ring: `on` is the ring's solid, the others are the ring's modes
of the same names, and Status's `ring` shows the result.

| Field | Type | What it is |
|---|---|---|
| `mode` | uint8 | 0 off, 1 on, 2 pulse, 3 flicker, 4 blink, 5 leia |
| `period_ms` | uint16 | Pulse, blink: 200–60000. 0: pulse 2000, blink 1000 |
| `time_limit_ms` | uint22 | 100–3600000: a timed mode, after which the ring goes back to what it was doing (mode, colour and level; Light never changes the ring's brightness). 0: for good; for leia, 30 s |
| `level` | uint8[<=1] | Percent, 0–100: the share of the ring's brightness this mode lights at (0 lights nothing). Empty: 100. Ignored for off and leia |
| `colour` | `Colour`[<=1] | The ring's colour for this mode. Empty: the colour it has now (white if it has never had one) |

The level is a share of the ring's brightness (`leds bright`, the board's settings), not a
brightness of its own: so the light at full is the ring at the brightness the user set, which
keeps a ring powered from the board within its limit ([the LED page](../use/leds.md#brightness))
whatever a controller asks for. Status's `ring` shows the product, the brightness the ring lights at.

The board runs the code `PATCH /api/v1/leds` runs, with the same checks and words: a mode with its
colour if given, the level (100 if not given), a pulse's or a blink's period, and `time_s` for a time
limit (the [LED page](../use/leds.md#the-holos-light) has the same from the console).

| `result.code` | When |
|---|---|
| bad request | `` `level` is 0..100``, `` `period_ms` is 200..60000``, `` `time_s` is 0.1..3600`` (a time limit under 100 ms or over an hour), or a mode that is none: `` `mode` is off, on, pulse, flicker, blink or leia`` |
| busy | The board is still working through four earlier requests |

**Leia** is the message from Leia: the light flickers at full for the time limit and then goes
back to what it was doing, and the holo turns to the centre if it can. The light half always
runs. The response's `centred` (bool) says whether the centre did too; when it didn't,
`result.code` is still 0 and `result.reason` and `result.message` give the centre's refusal. The
flicker is at level 100 in the colour given, or the ring's own; the centre is the motion route's
`center`, at its default pace, so Status shows `center` as the last motion asked for. A time limit out
of range refuses the whole request, before either half runs.

Another light or ring command during a timed mode replaces it, and the wait ends.

### Scene

`r2.holo.Scene.0.1`, on `scene`: apply a saved scene, or end the one running to its end, as
`POST /api/v1/scenes/apply` and `/scenes/end` and the host link's `SCENE` and `END` do.

| Field | Type | What it is |
|---|---|---|
| `action` | uint8 | 0 apply, 1 end |
| `slot` | uint8 | Apply: the scene's slot, 1–255; 0 to apply it by `name` |
| `name` | uint8[<=32] | Apply with slot 0: the scene's name |

| `result.code` | When |
|---|---|
| not found | No scene with that slot or name; for end, no scene running to its end |
| not playable | Its screen part can't be shown, its file missing included |
| not ready | Its holo motion can't run |

A part that fails stops the parts after it: the screen, then the LEDs, then the holo.

### Screen

`r2.holo.Screen.0.1`, on `screen`: what the screen shows, and its backlight, as
`POST /api/v1/screen/show`, `DELETE /api/v1/screen` and `PATCH /api/v1/screen` do.

| Field | Type | What it is |
|---|---|---|
| `show` | uint8 | 0 leave what shows, 1 a file, 2 a colour, 3 the calibration crosshair, 4 nothing (a clip stops; the panel sleeps with its backlight off) |
| `loop` | bool | A file that is a clip or an animation: play it forever. Not with `loops` |
| `loops` | uint16 | A clip or an animation: play it this many times, 1–1000. 0: a clip once, an animated GIF as the file says |
| `whole_frame` | bool | A file: decode each frame whole, rather than streamed in blocks |
| `colour` | `Colour` | A colour: which |
| `backlight` | uint8[<=1] | Percent, 0–100, until the board next starts. Empty: as it is. With `show` 0, this is all the request does |
| `path` | `uavcan.file.Path.2.0` | A file: a QuickTime Motion-JPEG clip (`.mov`), a PNG, a baseline JPEG or a GIF |

The file is checked before the response, so a refusal means the screen didn't change.

| `result.code` | When |
|---|---|
| bad request | A path the board refuses (`..`, too long), `loop` with `loops`, a field out of range |
| not found | No such file |
| not playable | A file it can't show: the codec, the size, not a clip or an image. The message says which |

### Ring

`r2.holo.Ring.0.1`, on `ring`: the LED ring, as `PATCH /api/v1/leds` and the host link's `LEDS`
do. Send only what changes: a new mode starts it from its beginning, a new colour or loop alone
restarts the mode running with it, and the brightness applies at once, until the board next
starts. A request with nothing in it changes nothing.

| Field | Type | What it is |
|---|---|---|
| `mode` | uint8[<=1] | 0 off, 1 solid, 2 wipe, 3 rainbow, 4 flicker, 5 pulse, 6 blink. Empty: the mode running |
| `colour` | `Colour`[<=1] | For every mode but off and rainbow. Empty: the colour it has (white if it has never had one) |
| `loop` | bool[<=1] | Wipe, rainbow: again and again, or once. Empty: once with a new mode; without one, as it is |
| `brightness` | uint8 | Percent, 1–100. 0: as it is |
| `period_ms` | uint16 | Pulse, blink: 200–60000. 0: pulse 2000, blink 1000 |

### ListScenes

`r2.holo.ListScenes.0.1`, on `list_scenes`: the saved scenes, one a call. Ask for `index` 0, 1,
2… until the name comes back empty, as `uavcan.register.List` lists registers. The order is the
order they were first saved in (`GET /api/v1/scenes`).

| Field | Type | What it is |
|---|---|---|
| `index` | uint8 | The request: 0-based |
| `count` | uint8 | Scenes saved, 0–16. A save or a delete between calls shifts the list, and this shows it |
| `slot` | uint8 | The scene's slot; 0 when it has none |
| `name` | uint8[<=32] | Its name; empty once `index` reaches `count` |

### MediaInfo

`r2.holo.MediaInfo.0.1`, on `media_info`: a clip's or an image's details from its header,
without showing it, as `GET /api/v1/media/info` gives them. **Optional**: for a controller that
needs more than the names and sizes `uavcan.file.List` and `uavcan.file.GetInfo` give.

| Field | Type | What it is |
|---|---|---|
| `path` | `uavcan.file.Path.2.0` | The request |
| `kind` | uint8 | 0 not a clip or an image, 1 clip, 2 image, 3 animation |
| `format` | uint8 | 0 none, 1 `mov`, 2 PNG, 3 JPEG, 4 GIF |
| `width`, `height` | uint16 | Pixels |
| `frames` | uint16 | A clip or an animation: in one pass; 1 for an image |
| `fps` | float16 | A clip or an animation; 0 for an image |
| `duration_ms` | uint32 | A clip or an animation: one pass; 0 for an image |

`result.code` is 0 when the board can show it. A file it can't show is not playable, with why in
the message, and still described when it is a kind the board knows. (The HTTP API answers that
case with 200 and `playable: false`.) No such file is not found.

## Error

`r2.holo.Error.0.1`, the `result` of every service. `code` is the host link's
[status](host-protocol.md#replies), with the same numbers. A busy answer's message is
`busy: the board is still working through earlier requests`:

| `code` | Name | The JSON errors it covers |
|---|---|---|
| 0 | ok | — |
| 1 | bad request | `bad_request`, `bad_path` |
| 2 | not found | `not_found`, `unknown_scene`, `not_running` |
| 3 | not playable | `not_playable` |
| 4 | not ready | `not_ready`: the holo can't move |
| 5 | busy | `busy`, and the board's queue of Cyphal requests full: try again |
| 6 | failed | Anything else |

| Field | Type | What it is |
|---|---|---|
| `code` | uint8 | Above |
| `reason` | [Reason](#reason) | With not ready: why, and the axis. With ok: only for a leia whose centre was refused. NONE otherwise |
| `message` | uint8[<=64] | The JSON error's message, word for word, cut to 64 bytes. Empty with ok, except as `reason` says |

A response that is ok is 3 bytes, one frame (4 for Light).

### Reason

`r2.holo.Reason.0.1`, one byte: the holo engine's reasons, with the words the console and the JSON
API use.

| `why` | Means |
|---|---|
| 0 | None |
| 1 | `not fitted`: the axis's servo isn't there |
| 2 | `not armed` (no arm switch on this board) |
| 3 | `uncalibrated`: the axis has no saved endpoints and the holo needs them |
| 4 | `unknown axis` |
| 5 | `no light` (never on this board, whose ring is its light) |
| 15 | Another reason: the message says |

`axis`: 0 none in particular, 1 horizontal (`pan`), 2 vertical (`tilt`), 3 the light's channel
(a panel's holo only).

## Registers

Read and written with `uavcan.register.Access` and listed with `uavcan.register.List`, by any
Cyphal tool. The console's `cyphal register` reads and writes the same table, with the same
checks. Every mutable register is saved on the board, in NVS, as soon as it is written, and is part
of a [backup](../use/backup.md); none holds a credential. `List` gives them in the order of the
tables below: the node's three, then each port's `.id` and `.type`.

A write the board can't take leaves the register as it was, and the response, which always carries
the register's value after the write, shows it: a value out of range or not a name, one of another
type, a port number another port has ([below](#port-numbers-in-use)), or a `.type` register. A
number of another type is taken when it converts exactly (a `natural8`, an `integer64` or a
`real32` 52 all write 52 to `uavcan.node.id`), and the name takes `unstructured` bytes as well as a
string. A register that doesn't exist reads empty, and a write to
one does nothing. The board's log says what was written and by which node, or why it wasn't. While
the board is still writing earlier requests (four at once), another write is answered with the
register as it is, unwritten: send it again.

**The node:**

| Register | Type | Values | Default | Flags | Takes effect |
|---|---|---|---|---|---|
| `uavcan.node.id` | natural16[1] | 0–127, or 65535 for plug-and-play. Anything else is refused | 65535 | mutable, persistent | When the node next starts |
| `r2.node.name` | string | The name `GetInfo` reports: 1–50 characters, [as `cyphal name` takes it](../use/cyphal.md#the-name) | `r2.dome.holo.front` | mutable, persistent | When the node next starts |
| `uavcan.can.bitrate` | natural32[2] | The first value is the bitrate: 125000, 250000, 500000 or 1000000. The second (CAN FD's data phase) is ignored, and reads back as the first | [1000000, 1000000] | mutable, persistent | When the node next starts |

These are the settings `cyphal id`, `cyphal name` and `cyphal bitrate` change: either way
changes both. The name isn't `uavcan.node.description`: that standard register is a free-text
note that doesn't change what `GetInfo` reports, and this one does.

**The ports**, one pair a port name (`status`, `event`, `axes`, `setpoint`, `motion`, `light`,
`scene`, `screen`, `ring`, `list_scenes`, `media_info`):

| Register | Type | Values | Default | Flags | Takes effect |
|---|---|---|---|---|---|
| `uavcan.pub.<name>.id`, `uavcan.sub.<name>.id`, `uavcan.srv.<name>.id` | natural16[1] | A subject-ID 0–8191 or a service-ID 0–511; 65535 turns the port off | The [default](#the-nodes-ports) | mutable, persistent | When the node next starts |
| `uavcan.pub.<name>.type`, and so on | string | The type, `r2.holo.Status.0.1` | — | immutable, persistent | — |

So `uavcan.pub.status.id` is 2300 until written, and `uavcan.srv.motion.type` reads
`r2.holo.Motion.0.1`. The `.type` registers let a tool such as Yakut call a service by its port
name. The standard suggests 65535 as every port's default; the board starts with working
defaults instead, so it can be used without configuring anything. The special registers for a
register's minimum, maximum and default (`<name><`, `<name>>`, `<name>=`) aren't provided.
Until the firmware has a port, its number is kept and checked but not used: it isn't in the
[port list](#the-port-list), and nothing goes on it.

<a id="port-numbers-in-use"></a>**A number belongs to one port.** Subject-IDs are one set,
published and subscribed alike (a subscription on a subject the board publishes would take other
nodes' messages of that type for its own), and service-IDs another. Within each, a number is one
of the node's own fixed ports' (subjects 7509, 7510 and 8166, services 384, 385, 430 and 435) or
one `r2.holo` port's, never two: a write of a number another port has is refused with the port that
has it ("subject 2301 is uavcan.pub.event.id's"), and the register keeps its value. A subject and a
service may share a number, and any number of ports may be off (65535). So two ports swap through a
free number, or through off. A saved number that breaks the rule anyway, which only a configuration
from elsewhere can bring (a backup from another firmware), turns the later port in the table off as
the board starts, with a warning in its log; its register then reads 65535.

**The holo**, from a later phase:

| Register | Type | Values | Default | Flags | Takes effect |
|---|---|---|---|---|---|
| `r2.holo.horizontal.endpoints_us`, `r2.holo.vertical.endpoints_us` | natural16[2] | Closed, then open, in µs, each within the servo's absolute range (1000–2000 on this board) and not equal; closed above open turns the axis round. [0, 0] goes back to the firmware's default range | The default range: [1150, 1900], [1300, 1950] | mutable, persistent | At once: the next motion uses them |
| `r2.holo.horizontal.release`, `r2.holo.vertical.release` | bit[3] | The drive policy, for the closed end, the open end and between (the ends are 2 % of the travel wide): true lets the servo go once it has settled, false keeps driving it | [false, false, false] | mutable, persistent | At the servo's next frame |
| `r2.holo.horizontal.settle_ms`, `r2.holo.vertical.settle_ms` | natural16[1] | How long a command is driven before a release takes effect: 0–65535, under 100 raised to 100 | 0 | mutable, persistent | At once |
| `r2.touch.reporting` | bit[1] | Touch events on or off | false (true in a build with `BOARD_TOUCH_AUTOSTART`) | mutable, persistent | At once, and at every start |

The endpoints are the ones `holo endpoints` sets, and the drive policy the one the web app and
`PUT /api/v1/servos/policy` set: the same saved values. Status's `horizontal` and `vertical` say
whether each axis is calibrated. `r2.touch.reporting` is saved, unlike `touch on` on the
console, so that a controller sets it once; `touch on` and `touch off` change it for the moment
without saving.

## Commands

`uavcan.node.ExecuteCommand.1.3`, on service 435:

| Command | Number | Does |
|---|---|---|
| Restart | 65535 | Restarts the board a second after the response (by when it has gone). The node comes back with the registers as they now are |
| Factory reset | 65532 | Every register back to its default (the node-ID, name and bitrate, every port; touch reporting, once it is a register), and the board's [settings](http-api.md#settings) to theirs (the boot scene, the backlight, the LEDs' brightness and count), as `cyphal reset` and `DELETE /api/v1/settings` do; then a restart, a second after the response. Kept: the scenes, the files, the servos' calibration and drive policy, the network and the web app's settings, and the host link's mode and pins, so the node is still on the bus afterwards, asking for a node-ID by plug-and-play |
| Identify | 65529 | The ring blinks white, once a second, and the screen shows the crosshair for 5 seconds, then both go back to what they were doing |
| Store persistent states | 65530 | Nothing to do: every register is saved when it is written. Answers success |
| Anything else | | Refused: bad command. Power off, begin software update and emergency stop included |

A response's `status` is success when the command was taken, before a restart; failure when the
board is still working through four earlier requests (send it again); internal error if a
factory reset couldn't save, in which case nothing is reset (what was saved before is put back) and
the board doesn't restart. `output` is always empty.

Identify is a five-second scene, `identify`, applied as a scene of the board's own would be:
the web app, the HTTP API's `GET /api/v1/scenes` and the events show it running, and it ends a
scene that is running to its end. At its end the screen, the ring and the holo go back to what
they were doing before it, as with any scene whose `then` is `restore` (a twitch or a scan starts
again; a clip on its way to its end doesn't), or, if it ended a scene that restores, to what that
scene would have restored.

## Sizes and bus load

Each type's size, from its definition ("least" with every variable-length field empty). A frame
is about 130 bits on the wire, so 130 µs at 1 Mbit/s.

| Type | Least: bytes, frames | Worst case: bytes, frames |
|---|---|---|
| `Status` | 86, 13 | 375, 54 |
| `Event` | 5, 1 | 265, 39 |
| `Axes` | 4, 1 | 4, 1 |
| `Motion` request | 17, 3 | 19, 3 |
| `Light` request | 8, 2 | 12, 2 |
| `Scene` request | 3, 1 | 35, 6 |
| `Screen` request | 9, 2 | 265, 39 |
| `Ring` request | 6, 1 | 11, 2 |
| `ListScenes` request, response | 1, 1; 6, 1 | 1, 1; 102, 15 |
| `MediaInfo` request, response | 1, 1; 17, 3 | 256, 37; 81, 12 |
| Any other response | 3, 1 | 67, 10 (a refusal with a 64-byte message) |

The worst cases with a path count 255 bytes; this board's paths are at most 159. In practice a
`Status` with a 20-byte path and a 10-byte scene name is 116 bytes, 17 frames; an event with a
20-byte path, 4 or 5.

What the board sends, at 1 Mbit/s:

| Traffic | Frames a second | Share of the bus |
|---|---|---|
| Heartbeat, once a second | 1 | 0.013 % |
| Port list, 23 frames every 10 s (more as the holo's ports join it) | 2.3 | 0.03 % |
| Status once a second, 17 frames (13 until the screen and scenes are on Cyphal) | 17 | 0.22 % |
| Status four times a second (while things change) | 68 | 0.88 % |
| Axes while moving, ten a second | 10 | 0.13 % |
| Axes at rest | 1 | 0.013 % |
| All of it, while moving and changing | about 81 | about 1.1 % |

A controller streaming setpoints at 50 a second adds 50 frames, 0.65 %. The board hands its
frames to the CAN controller one at a time, highest priority first, so a setpoint's echo in
`axes` or a service's response waits behind at most one frame of a long `Status`, not the whole of
it.

## Not on Cyphal

These stay on Wi-Fi, the USB console, or the host link:

- Uploading clips and images, and deleting or moving files.
- The network, the web app's settings, and any password or token.
- The configuration backup (`nvs_export`, `nvs_import`).
- Firmware updates: over Wi-Fi, as now.
- Saving, changing and deleting scenes, and applying a scene given whole: the web app and the
  HTTP API do those. Cyphal applies a saved one.
- The board's settings (the boot scene and the levels it starts with) other than by a factory
  reset, and the host link's own settings.
- Raw servo moves, for finding endpoints: the web app's calibration, `servo_move` and
  `holo endpoints` do that. The endpoints themselves are registers.
- Catching up on missed events: the HTTP API's and the host link's "events after N" has no
  Cyphal counterpart; Status is the catch-up.
- Diagnostics on the bus (`uavcan.diagnostic.Record`), time synchronisation, the IMU and the
  battery.
- The console's `holo led` and `holo leia`, which still do nothing on this board: the
  [Light](#light) service is what drives the ring as the holo's light.
