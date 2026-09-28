# HTTP API

The board's [web app](../use/web.md) is a front end for a small JSON API, and anything it does, a
script can do too. This page explains how the API works, with `curl` recipes. The full description
is an OpenAPI 3.1 document:

- **[Browse it](http-api-explorer.md)** in Swagger UI, on this site;
- **[Download it](openapi.json)**, or fetch it from any board at `/api/v1/openapi.json`. The board's
  copy always describes the firmware serving it. Postman, Insomnia, Swagger Editor and
  `openapi-generator` import it directly.

## Reaching the API

Everything is under `/api/v1` on port 80, on whichever address reaches the board:

| Address | When |
|---|---|
| `http://holo-xxxx.local` | On your network (mDNS). `web` on the console prints the exact name |
| `http://<its address>` | On your network. `web` or `wifi` on the console prints it |
| `http://192.168.4.1` | On the board's own access point, after `wifi ap on` |

The examples below use `holo-2db0.local`. Replies are JSON. Errors are JSON too, with a status that
means something and a stable `error` code to branch on:

```json
{ "error": "busy", "message": "another update is in progress" }
```

## Protection

- **Reading is open**: `GET` needs nothing, except downloading a file and reading the web
  settings, which need the password once one is set (the console's history is a file, and can hold
  what was typed).
- **Changes are guarded against other web pages.** A `PUT`, `POST`, `PATCH` or `DELETE` must name
  the board in its `Host` header: an IP address, `holo-xxxx` or `holo-xxxx.local`. A `POST` or
  `PATCH` must also say `Content-Type: application/json`. `curl` and scripts do this naturally. A web page on another site can't, because a browser won't send those requests
  across sites without the board's consent, and the board gives it only to the trusted sites below.
  So a page you happen to visit can't update or restart the board, even with no password set.
- **The password.** Once one is set -- with `web password <password>` on the console, or
  [`PATCH /api/v1/web`](#network-and-the-web-server) -- changes also need it, sent either way:

  ```sh
  curl -H "Authorization: Bearer <password>" ...
  curl -u any:<password> ...            # HTTP Basic, any user name
  ```

  Without it, the reply is `401` with `WWW-Authenticate: Bearer`. `GET /api/v1/info` reports
  `"auth": true` when a password is set.
- **Trusted sites (CORS).** Pages from a short list of sites may call the API from a browser: the
  board answers their CORS preflights and lets them read its replies. The list starts as
  [Swagger Editor](https://editor.swagger.io), `astromech.co` and `davidiansmith.ca`, each with its
  subdomains. `web cors` on the console shows it and changes it:

  ```
  web cors add http://localhost:8080
  ```

  (or `PATCH /api/v1/web` with the whole list). `web cors remove <origin>` takes one away, `web cors none` empties the list, and `web cors reset`
  goes back to the default. An origin is `scheme://host[:port]`; `https://*.example.com` covers
  every subdomain of `example.com` but not `example.com` itself. The other checks still apply to
  these sites. **With no password set, a listed site can update and restart the board**, so set a
  password if you add one you don't control.

The API is plain HTTP. On a network you don't trust, set a password and prefer the board's own
access point, which is WPA2.

## The board

`GET /api/v1/info` identifies the board:

- `firmware`: project, version, build date, ESP-IDF, and the image's SHA-256;
- `hostname`, `uptime_s` and `heap_free`;
- `sta` and `ap`: the station link (SSID, address, signal) and the access point;
- `via`: `ap` when the request came in on the board's access point;
- `features`: what the web app offers on this firmware: `ota`, `files`, `screen`, `leds`, `holo`,
  `scenes`, `settings`, `network`, `events`.

```sh
curl http://holo-2db0.local/api/v1/info
```

`POST /api/v1/restart` restarts the board. The optional `{"delay_ms": 500}` gives the reply time to
leave first.

## Now, and from now on

Two kinds of thing are here, kept apart:

- **What the board is doing now**: `/screen`, `/leds`, `/holo`, and the servos' positions. Changing
  these changes nothing that survives a restart.
- **What is saved**: `/settings`, `/scenes`, a servo's calibration and drive policy, the known
  networks and the access point's name, and `/web`. What the board does when it starts is
  `settings.boot_scene`.

The shapes follow from that:

| Shape | For | Example |
|---|---|---|
| `GET`, then `PATCH` with only what changes; the reply is the whole new state | What it is doing now, and the settings | `PATCH /api/v1/leds` `{"brightness": 20}` |
| `POST` with a JSON body | An action | `POST /api/v1/screen/show` `{"path": "/clips/intro.mov"}` |
| `PUT` and `DELETE`, named in the query | A saved, named thing | `PUT /api/v1/scenes/scene?name=cantina` |

**Long operations** -- an update, a file upload, download or copy, a SHA-256, a Wi-Fi scan -- run
one at a time: another gets `409` `busy`. The server answers everything else meanwhile.

## Files

The board's storage volume holds the clips and images, and the console's history. A file is named by
a `path` query parameter from the volume's root, such as `/clips/intro.mov`; `/` is the root. `..`,
`.` and empty segments are refused (`400` `bad_path`), as are names over 63 bytes and paths over
159.

```sh
B=http://holo-2db0.local/api/v1
curl "$B/fs"                                            # size and free space
curl "$B/fs/list?path=/clips"                           # a directory
curl "$B/fs/entry?path=/clips/intro.mov&sha256=true"    # one file, hashed
curl -T intro.mov "$B/fs/file?path=/clips/intro.mov&parents=true"
curl -o intro.mov "$B/fs/file?path=/clips/intro.mov"
J='Content-Type: application/json'
curl -X POST -H "$J" -d '{"path":"/clips/old"}' "$B/fs/mkdir"
curl -X POST -H "$J" -d '{"from":"/clips/intro.mov","to":"/clips/old/intro.mov"}' "$B/fs/move"
curl -X POST -H "$J" -d '{"from":"/clips/old/intro.mov","to":"/clips/intro.mov"}' "$B/fs/copy"
curl -X DELETE "$B/fs/entry?path=/clips/old&recursive=true"
```

- **An upload** (`PUT /api/v1/fs/file`) is written beside its destination as `<name>.part`, and
  takes its place only once all of it has arrived. An upload that fails or is cut off changes
  nothing. It refuses to replace a file unless `overwrite=true`, and a missing directory unless
  `parents=true`. `sha256=<hex>` has the board check what arrived. A full volume is `507`
  `no_space`; the board keeps a 32 KB margin.
- **Moving, replacing or deleting the file on the screen** stops it first. A clip playing from the
  volume otherwise carries on during an upload, though it may stutter while the flash is written.

## The screen

`GET /api/v1/screen` says what the round screen shows: `nothing` (the panel asleep), a `colour`, the
`calibration` crosshair, an `image`, or a `clip` with its progress. `POST /api/v1/screen/show`
shows something else:

```sh
curl -X POST -H "$J" -d '{"path":"/clips/intro.mov","loop":true}' "$B/screen/show"     # forever
curl -X POST -H "$J" -d '{"path":"/clips/intro.mov","loops":3}' "$B/screen/show"      # three times
curl -X POST -H "$J" -d '{"path":"/stills/logo.png"}' "$B/screen/show"
curl -X POST -H "$J" -d '{"colour":"#102030"}' "$B/screen/show"
curl -X POST -H "$J" -d '{"calibration":true}' "$B/screen/show"
curl -X DELETE "$B/screen"                                    # nothing
curl -X PATCH -H "$J" -d '{"backlight":40}' "$B/screen"      # until the next start
```

A file is checked before the reply: a clip must be QuickTime Motion-JPEG no larger than 240×240,
an image a PNG, baseline JPEG or GIF. Anything else is `422` `not_playable`, saying why, and the
screen is unchanged. `GET /api/v1/media?path=/` lists every clip and image under a directory by
their names, and `GET /api/v1/media/info?path=...` reads one file's header -- size, frames, frame
rate -- without showing it.

## The LEDs

`GET /api/v1/leds` is the strip: its `mode` (`off`, `solid`, `wipe`, `rainbow`, `flicker`),
`colour`, `loop` and `brightness`. `flicker` flickers the colour like a failing hologram until
something else replaces it. `PATCH` changes any of them now:

```sh
curl -X PATCH -H "$J" -d '{"mode":"solid","colour":"orange"}' "$B/leds"
curl -X PATCH -H "$J" -d '{"mode":"rainbow","loop":true}' "$B/leds"
curl -X PATCH -H "$J" -d '{"mode":"flicker","colour":"#4da6ff"}' "$B/leds"
curl -X PATCH -H "$J" -d '{"brightness":10}' "$B/leds"
```

A colour is `#rrggbb`, `R,G,B`, or a name. Keep the brightness at 30 or below when the strip is
powered from the board.

## The holoprojector and its servos

`GET /api/v1/holo` is the holo's motion and position (`x` and `y`, -1 to 1, + right and up), and
whether it can move (`ready`, or `why` not). `POST /api/v1/holo/motion` moves it, or starts or
stops a behaviour, with the console's `holo` parameters:

```sh
curl -X POST -H "$J" -d '{"motion":"twitch","range":50,"interval_s":[2,6]}' "$B/holo/motion"
curl -X POST -H "$J" -d '{"motion":"move","x":40,"y":-20,"duration_ms":600}' "$B/holo/motion"
curl -X POST -H "$J" -d '{"motion":"wag","count":3}' "$B/holo/motion"
curl -X POST -H "$J" -d '{"motion":"stop"}' "$B/holo/motion"
```

A holo that can't move -- its servos not calibrated, say -- is `409` `not_ready`, saying why. Its
light is the LED strip: [`/api/v1/leds`](#the-leds), or a [scene](#scenes) to set both at once.

The servos underneath are at `GET /api/v1/servos`: the pulse each was last sent, whether it is being
driven, its limits and its drive policy. Calibrating one is a matter of moving it to each end of the
holo's travel, then saving that range:

```sh
curl -X POST -H "$J" -d '{"id":"pan","us":1150}' "$B/servos/move"      # closed end: full left
curl -X POST -H "$J" -d '{"id":"pan","us":1900}' "$B/servos/move"      # open end: full right
curl -X PUT  -H "$J" -d '{"min_us":1150,"max_us":1900}' "$B/servos/calibration?id=pan"
curl -X POST -H "$J" -d '{"id":"all"}' "$B/servos/release"             # limp
```

`DELETE /api/v1/servos/calibration?id=pan` forgets the range. `PUT /api/v1/servos/policy?id=pan`
saves whether a servo keeps being driven once it has settled, at each end and between them, which
can stop the buzz of a servo held against a stop.

## Scenes

A scene is a saved combination of what to show, what the LEDs do, and how the holo moves. Each
part is optional, and a part left out is left alone, so a scene with only `leds` is an LED preset.
Up to 16 are kept.

```sh
curl -X PUT -H "$J" "$B/scenes/scene?name=cantina" -d '{
  "screen": {"path": "/clips/cantina.mov", "loop": true},
  "leds":   {"mode": "solid", "colour": "#ff8000", "brightness": 30},
  "holo":   {"motion": "twitch", "range": 50}
}'
curl -X POST -H "$J" -d '{"name":"cantina"}' "$B/scenes/apply"
curl "$B/scenes"
curl -X DELETE "$B/scenes/scene?name=cantina"
```

`POST /api/v1/scenes/apply` also takes a scene that isn't saved, as `{"scene": {...}}`. It applies
the screen, then the LEDs, then the holo, and a part that fails stops the rest, with an error
naming it. The files a scene names are checked when it is applied, not when it is saved.

### When a scene ends

A scene can say what happens when it is over, with `then`:

| `then` | When the scene ends |
|---|---|
| `stay` (the default) | Nothing: everything carries on as the scene left it |
| `restore` | The screen, the LEDs and the holo go back to what they were doing before the scene. A behaviour such as a twitch starts again, and a clip that was looping plays again |
| `off` | The screen and LEDs go off, and the holo's servos go limp |

A scene ends when its clip has played (once, or its `loops`), or after `duration_s`, whichever
comes first. A scene with `restore` or `off` needs one of them: a clip that doesn't `loop` forever,
or a duration. In such a scene, a clip or animated GIF with neither `loop` nor `loops` plays once.

```sh
# Leia's message once, with the LEDs flickering blue and the holo looking straight out; then back
# to whatever the board was doing
curl -X PUT -H "$J" "$B/scenes/scene?name=message" -d '{
  "screen": {"path": "/clips/leia.mov"},
  "leds":   {"mode": "flicker", "colour": "#4da6ff"},
  "holo":   {"motion": "center"},
  "then":   "restore"
}'
# Thirty seconds of flicker and a nod, then everything off
curl -X PUT -H "$J" "$B/scenes/scene?name=bow" -d '{
  "leds": {"mode": "flicker"}, "holo": {"motion": "nod"}, "duration_s": 30, "then": "off"
}'
```

`GET /api/v1/scenes` says which scene is running to its end, under `active`. `POST
/api/v1/scenes/end` ends it now, doing its `then`. Applying another scene, or showing something else
in place of its clip, ends it without its `then`. One `restore` scene after another goes back to what
the board was doing before the first.

## Settings

`GET /api/v1/settings` is what the board starts with; `PATCH` changes it, and it is saved:

- `boot_scene`: the scene applied at every start, or null for nothing;
- `screen.backlight`: the backlight it starts at;
- `leds.brightness`, and `leds.count`, the LEDs on the strip.

```sh
curl -X PATCH -H "$J" -d '{"boot_scene":"cantina","screen":{"backlight":80}}' "$B/settings"
```

The backlight and brightness change now too. A new `leds.count` waits for a restart, and the reply
says so with `"restart_required": true`. `DELETE /api/v1/settings` goes back to the firmware's
defaults, keeping scenes, calibration and networks.

## Network and the web server

`GET /api/v1/network` is the network the board is on, the networks it knows, and its access point.
Passphrases are never read back. The board keeps 16 networks; saving another forgets the one saved
longest ago. Joining without a passphrase uses the saved one, or takes the network to be open.

```sh
curl -X PUT -H "$J" -d '{"passphrase":"correct horse battery"}' "$B/network/known?ssid=workshop"
curl -X POST -H "$J" -d '{"ssid":"workshop"}' "$B/network/join"
curl "$B/network/scan"
curl -X DELETE "$B/network/known?ssid=cafe"
curl -X PATCH -H "$J" -d '{"on":true}' "$B/network/ap"
```

Several of these can cut you off, so each replies first:

- **joining** another network leaves the one the board is on;
- **forgetting** the network the board is on leaves it too;
- **`PATCH /api/v1/network`** with `{"sta_enabled": false}` stops it joining any;
- **changing the access point's name or passphrase** restarts it, dropping its clients.

The access point is the way back in. It is off at every start, unless the board can't join a
network then: it comes on for 5 minutes, and `ap.off_in_s` counts them down. Pressing BOOT on the
board brings it back the same way, as does `wifi ap on` on the console, to stay. `{"on": true}`
keeps one that is on for a while on.

`GET /api/v1/web` and `PATCH /api/v1/web` are the web server's own settings: the `.local`
hostname, the password, and the [trusted sites](#protection). `null` goes back to the default, or
clears the password.

```sh
curl -X PATCH -H "$J" -d '{"password":"hunter22"}' "$B/web"
curl -u any:hunter22 -X PATCH -H "$J" -d '{"hostname":"holo-dome"}' "$B/web"
```

## Events

Rather than asking again and again, a client can follow `GET /api/v1/events`: a stream of
[Server-Sent Events](https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events) that
stays open, and carries each change as it happens. The web app follows it; `curl -N` shows it:

```sh
curl -N -H "Accept: text/event-stream" "$B/events?kinds=screen,scene,scene_ended"
```

```text
retry: 3000

event: scene
data: {"event":"scene","state":{"name":"message","then":"restore","until_clip_ends":true,"remaining_s":null}}

event: screen
data: {"event":"screen","state":{"powered":true,"backlight":100,"showing":"clip","path":"/clips/leia.mov",...}}

event: scene_ended
id: 42
data: {"event":"scene_ended","seq":42,"t_ms":812345,"name":"message","then":"restore","by":"clip"}
```

Two sorts of event come down it:

| Sort | Kinds | `data` |
|---|---|---|
| **A resource changed** | `screen`, `leds`, `holo`, `scene` (the one running to its end, or null), `scenes`, `settings`, `network`, `ota` (the update session), `system` (uptime, free memory, signal; every 15 s) | `{"event": kind, "state": ...}`: what the resource's `GET` returns now |
| **Something happened** | `clip_ended`, `scene_ended`, `touch` | The event's own fields, with `seq` and `t_ms` |

- **`?kinds=`** takes only those kinds; without it, every kind.
- **Changes close together are sent as one**, the latest, and the holo's position at most four
  times a second while it moves. Only the change is sent: a page counts a scene's time down, or a
  clip's frames on, itself.
- **Happenings are numbered** (`seq`, which is also the SSE `id:`) and the board keeps the last 32.
  A browser's `EventSource` reconnects by itself and sends `Last-Event-ID`, and the board replays
  what it missed; `?after=40` does the same by hand. States aren't kept: after a reconnect, read
  them again with their `GET`s.
- **Without `Accept: text/event-stream`**, `GET /api/v1/events` is the kept happenings as JSON,
  `{"seq", "lost", "events"}`, for a client that would rather poll.
- **A few streams at once**: 3 unless the firmware is built with more. One more gets `503` `busy`,
  and the web app polls instead.

The stream needs no password: like `/screen` and `/leds`, which it mirrors, it's for reading. In a
browser:

```js
const events = new EventSource("http://holo-2db0.local/api/v1/events?kinds=scene_ended,leds");
events.addEventListener("scene_ended", (m) => console.log("ended", JSON.parse(m.data)));
events.addEventListener("leds", (m) => console.log("LEDs now", JSON.parse(m.data).state));
```

## A show, scripted

Upload a clip, check it can play, save it as a scene with warm LEDs and a restless holo, and have the
board start with it:

```sh
#!/bin/sh
set -e
B=http://${1:-holo-2db0.local}/api/v1
J='Content-Type: application/json'

curl -fsS -T cantina.mov "$B/fs/file?path=/clips/cantina.mov&parents=true&overwrite=true" >/dev/null
curl -fsS "$B/media/info?path=/clips/cantina.mov"; echo
curl -fsS -X PUT -H "$J" "$B/scenes/scene?name=cantina" -d '{
  "screen": {"path": "/clips/cantina.mov", "loop": true},
  "leds":   {"mode": "solid", "colour": "#ff8000", "brightness": 25},
  "holo":   {"motion": "twitch", "range": 50}
}' >/dev/null
curl -fsS -X POST -H "$J" -d '{"name":"cantina"}' "$B/scenes/apply" >/dev/null
curl -fsS -X PATCH -H "$J" -d '{"boot_scene":"cantina"}' "$B/settings"; echo
```

`tools/api_smoke.py` in the repository exercises every endpoint this way, and checks each reply
against the OpenAPI description: `make api-smoke HOST=holo-2db0.local` (add `WRITE=1` to also
change things, under `/test-api` on the board, putting back what it touches).

## Updates

An update is a **session**, whether it comes from an upload, a pull, or the console's `ota put`.
There is one session at a time; a second is refused with `409`. It moves through these states:

```text
idle ──► receiving / downloading ──► verifying ──► staged ──► active ──► (restart) running, on trial ──► confirmed
                     │                    │           │
                     └──────► failed ◄────┘           └─► idle (discarded)
```

- **receiving / downloading**: bytes are arriving, written into the slot that isn't running. The
  first 288 bytes are checked at once: an application image, for this chip, for this project. A
  wrong file fails immediately rather than after a megabyte.
- **verifying**: all there. The board checks the image's structure and its SHA-256.
- **staged**: good, and in its slot, but not what boots. Nothing has changed yet.
- **active**: the boot image. It runs from the next restart.
- **failed**: see `error`. The running firmware is untouched, whatever went wrong.

After a restart into the new image, it runs **on trial**. It confirms itself once it is up. A
reset before then boots the previous image again.

`GET /api/v1/ota` returns everything about updates in one reply:

- `slots`: each slot's label, which one runs, which boots, which is next, its record (`trial`,
  `confirmed`, …), and the image in it;
- `session`: the session;
- `pull`: whether the board can fetch images, and whether it is online.

`GET /api/v1/ota/image` returns just the session, and is the call to poll.

### Upload

`PUT /api/v1/ota/image` with the image as the body. The reply comes once the image is staged, or
has failed:

```sh
curl -T build/holo-player-fw.bin http://holo-2db0.local/api/v1/ota/image
```

Query options:

| Option | Does |
|---|---|
| `sha256=<hex>` | Refuse the image unless it has this SHA-256 |
| `activate=1` | Make it the boot image once staged |
| `reboot=1` | With `activate`: restart into it, a moment after replying |
| `force=1` | Accept an image built for another project |

Everything in one call, checked against the file's own hash:

```sh
f=build/holo-player-fw.bin
curl -T "$f" "http://holo-2db0.local/api/v1/ota/image?sha256=$(shasum -a 256 "$f" | cut -d' ' -f1)&activate=1&reboot=1"
```

The upload streams straight into flash, so the server keeps answering throughout, and another client
can watch `GET /api/v1/ota/image` count the bytes.

### Pull

`POST /api/v1/ota/pull` has the board download the image itself, which needs its station link to
the internet. The reply (`202`) comes as soon as the download starts; poll the session to follow
it. The body names one of:

- **`channel`**: a release channel, such as `latest`, looked up on this documentation site;
- **`url`**: an image, or a release's `manifest.json`. A manifest's SHA-256 is then enforced.

```sh
curl -X POST -H "Content-Type: application/json" -d '{"channel":"latest"}' http://holo-2db0.local/api/v1/ota/pull
curl -X POST -H "Content-Type: application/json" \
     -d '{"url":"http://10.0.19.20:8000/holo-player-fw.bin","activate":true,"reboot":true}' \
     http://holo-2db0.local/api/v1/ota/pull
```

`sha256`, `activate`, `reboot` and `force` work as they do for an upload. HTTPS certificates are
checked, and a redirect from `https` to plain `http` is refused.

### Look before you pull

`GET /api/v1/ota/check?channel=latest` looks up what a pull would fetch, without downloading it:

- the release's `version`, next to `current`;
- `newer`: whether the release is a later one. It is null when either version isn't a release
  tag, such as a development build;
- `size`, `sha256`, and a link to the release.

Answers are kept for five minutes; `refresh=1` looks again. A channel that publishes no firmware,
such as `dev`, answers `404`.

```sh
curl "http://holo-2db0.local/api/v1/ota/check?channel=latest"
```

### Switch

`POST /api/v1/ota/activate` makes an image the boot image and, unless `"reboot": false`, restarts
into it a second after replying:

| Body | Boots |
|---|---|
| `{}` | The staged image |
| `{"slot": "ota_0"}` | The image already in `ota_0`: the way back to the previous firmware |
| `{"slot": "<the running slot>", "reboot": false}` | Undoes an activation that hasn't restarted yet |

The board checks the image in the slot before selecting it. A slot marked invalid or rolled back is
refused (`422`).

### Cancel or discard

`DELETE /api/v1/ota/image` does one of three things:

- stops an upload or pull in progress, which then fails as `cancelled`;
- forgets a staged image;
- clears a failed session.

An image already activated can't be discarded. Activate the running slot instead.

## A whole update, scripted

```sh
#!/bin/sh
# Update a board to the latest release, and wait until it runs it.
set -e
B=http://${1:-holo-2db0.local}/api/v1
J='Content-Type: application/json'

curl -fsS "$B/ota/check?channel=latest&refresh=1"; echo
curl -fsS -X POST -H "$J" -d '{"channel":"latest"}' "$B/ota/pull" >/dev/null
while :; do
  s=$(curl -fsS "$B/ota/image")
  case "$s" in
    *'"state":"staged"'*) break ;;
    *'"state":"failed"'*) echo "$s"; exit 1 ;;
  esac
  sleep 1
done
curl -fsS -X POST -H "$J" -d '{}' "$B/ota/activate" >/dev/null
sleep 10
until curl -fsS -m 2 "$B/info" 2>/dev/null; do sleep 2; done; echo
```

## Errors

| Status | `error` | When |
|---|---|---|
| 400 | `bad_request`, `bad_json`, `bad_sha256`, `bad_path`, `not_a_directory`, `is_a_directory` | The request is malformed, or names the wrong kind of thing |
| 401 | `auth_required` | A password is set, and this request lacks it |
| 403 | `forbidden_host` | The `Host` header doesn't name the board |
| 404 | `not_found`, `no_such_slot`, `no_firmware`, `unknown_scene` | No such endpoint, file, servo, network, slot, scene, or firmware on that channel |
| 409 | `busy`, `nothing_staged`, `cannot_discard`, `cancelled` | Another long operation is running, or the update session isn't in the state asked of it |
| 409 | `not_running` | No scene is running to its end |
| 409 | `exists`, `not_empty`, `full` | Something is in the way: a file (`overwrite=true`), a directory's contents (`recursive=true`), or the list of scenes or networks |
| 409 | `not_ready` | The holo can't move now; the message says why |
| 411 | `length_required` | An upload without a `Content-Length` |
| 413 | `too_large` | The image won't fit the slot |
| 415 | `content_type` | A `POST` or `PATCH` that isn't `application/json` |
| 422 | `invalid_image`, `sha256_mismatch` | Not a usable firmware image (the running firmware still boots), or a file that didn't arrive intact |
| 422 | `not_playable` | A file the screen can't show; the message says why |
| 503 | `offline`, `unreachable` | The board can't reach the internet, or the release site |
| 503 | `busy` | Every event stream is taken |
| 507 | `no_space` | The storage volume is too full |

## Every endpoint

| Method | Path | Does |
|---|---|---|
| GET | `/api/v1/info` | The board, its firmware, its network, its features |
| POST | `/api/v1/restart` | Restart |
| GET | `/api/v1/openapi.json` | The OpenAPI description of this firmware's API |
| GET | `/api/v1/events` | What changes, as it happens (Server-Sent Events), or the latest happenings |
| GET | `/api/v1/ota` | The slots, the session, and whether the board can pull |
| GET | `/api/v1/ota/image` | The session |
| PUT | `/api/v1/ota/image` | Upload an image |
| DELETE | `/api/v1/ota/image` | Cancel, discard, or clear |
| POST | `/api/v1/ota/activate` | Make an image the boot image, and restart |
| POST | `/api/v1/ota/pull` | Download an image from a URL or release channel |
| GET | `/api/v1/ota/check` | What a pull would fetch, and whether it is newer |
| GET | `/api/v1/fs` | The storage volume's size and free space |
| GET | `/api/v1/fs/list` | A directory |
| GET | `/api/v1/fs/entry` | A file or directory, optionally hashed |
| DELETE | `/api/v1/fs/entry` | Delete a file or directory |
| GET | `/api/v1/fs/file` | Download a file |
| PUT | `/api/v1/fs/file` | Upload a file |
| POST | `/api/v1/fs/mkdir` | Make a directory |
| POST | `/api/v1/fs/move` | Move or rename |
| POST | `/api/v1/fs/copy` | Copy a file |
| GET | `/api/v1/screen` | What the screen shows |
| PATCH | `/api/v1/screen` | The backlight, now |
| DELETE | `/api/v1/screen` | Show nothing |
| POST | `/api/v1/screen/show` | Show a clip, image, colour, or the crosshair |
| GET | `/api/v1/media` | The clips and images under a directory |
| GET | `/api/v1/media/info` | Describe a clip or image |
| GET | `/api/v1/leds` | The LED strip |
| PATCH | `/api/v1/leds` | Change the LED strip, now |
| GET | `/api/v1/holo` | The holo's motion and position |
| POST | `/api/v1/holo/motion` | Move the holo, or start or stop a behaviour |
| GET | `/api/v1/servos` | The servos: position, limits, policy |
| POST | `/api/v1/servos/move` | Drive a servo to a position |
| POST | `/api/v1/servos/release` | Stop driving servos |
| PUT | `/api/v1/servos/calibration` | Save a servo's working range |
| DELETE | `/api/v1/servos/calibration` | Forget it |
| PUT | `/api/v1/servos/policy` | Save a servo's drive policy |
| GET | `/api/v1/scenes` | The saved scenes |
| PUT | `/api/v1/scenes/scene` | Save a scene |
| DELETE | `/api/v1/scenes/scene` | Delete a scene |
| POST | `/api/v1/scenes/apply` | Apply a scene |
| POST | `/api/v1/scenes/end` | End the running scene now, doing its `then` |
| GET | `/api/v1/settings` | What the board starts with |
| PATCH | `/api/v1/settings` | Change it (saved) |
| DELETE | `/api/v1/settings` | Back to the defaults |
| GET | `/api/v1/network` | The network, the known ones, the access point |
| PATCH | `/api/v1/network` | Turn joining networks on or off |
| GET | `/api/v1/network/scan` | The networks in range |
| PUT | `/api/v1/network/known` | Save a network |
| DELETE | `/api/v1/network/known` | Forget a network |
| POST | `/api/v1/network/join` | Join a network |
| PATCH | `/api/v1/network/ap` | The access point: on or off, name, passphrase |
| GET | `/api/v1/web` | The web server's settings |
| PATCH | `/api/v1/web` | Change them (saved) |

`v1` changes only when an existing client would break; new endpoints and new fields arrive without
it changing.
