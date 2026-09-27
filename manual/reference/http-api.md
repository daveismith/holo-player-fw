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

- **Reading is always open**: `GET` needs nothing.
- **Changes are guarded against other web pages.** A `PUT`, `POST` or `DELETE` must name the board
  in its `Host` header: an IP address, `holo-xxxx` or `holo-xxxx.local`. A `POST` must also say
  `Content-Type: application/json`, or `application/octet-stream` for an image. `curl` and scripts
  do this naturally. A web page on another site can't, because a browser won't send those requests
  across sites without the board's consent, and the board never gives it. So a page you happen to
  visit can't update or restart the board, even with no password set.
- **The password.** Once one is set with `web password <password>` on the console, changes also
  need it, sent either way:

  ```sh
  curl -H "Authorization: Bearer <password>" ...
  curl -u any:<password> ...            # HTTP Basic, any user name
  ```

  Without it, the reply is `401` with `WWW-Authenticate: Bearer`. `GET /api/v1/info` reports
  `"auth": true` when a password is set.

The API is plain HTTP. On a network you don't trust, set a password and prefer the board's own
access point, which is WPA2.

## The board

`GET /api/v1/info` identifies the board:

- `firmware`: project, version, build date, ESP-IDF, and the image's SHA-256;
- `hostname`, `uptime_s` and `heap_free`;
- `sta` and `ap`: the station link (SSID, address, signal) and the access point;
- `via`: `ap` when the request came in on the board's access point;
- `features`: what the web app offers on this firmware (`ota` today).

```sh
curl http://holo-2db0.local/api/v1/info
```

`POST /api/v1/restart` restarts the board. The optional `{"delay_ms": 500}` gives the reply time to
leave first.

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

`PUT /api/v1/ota/image` with the image as the body. `POST` is accepted too, as
`application/octet-stream`. The reply comes once the image is staged, or has failed:

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
| 400 | `bad_request`, `bad_json`, `bad_sha256` | The request is malformed |
| 401 | `auth_required` | A password is set, and this request lacks it |
| 403 | `forbidden_host` | The `Host` header doesn't name the board |
| 404 | `not_found`, `no_such_slot`, `no_firmware` | No such endpoint, slot, or firmware on that channel |
| 409 | `busy`, `nothing_staged`, `cannot_discard`, `cancelled` | Another update is running, or the session isn't in the state asked of it |
| 411 | `length_required` | An upload without a `Content-Length` |
| 413 | `too_large` | The image won't fit the slot |
| 415 | `content_type` | A `POST` that isn't `application/json` (or `application/octet-stream`) |
| 422 | `invalid_image`, `sha256_mismatch` | Not a usable image. The running firmware still boots |
| 503 | `offline`, `unreachable` | The board can't reach the internet, or the release site |

## Every endpoint

| Method | Path | Does |
|---|---|---|
| GET | `/api/v1/info` | The board, its firmware, its network, its features |
| POST | `/api/v1/restart` | Restart |
| GET | `/api/v1/openapi.json` | The OpenAPI description of this firmware's API |
| GET | `/api/v1/ota` | The slots, the session, and whether the board can pull |
| GET | `/api/v1/ota/image` | The session |
| PUT, POST | `/api/v1/ota/image` | Upload an image |
| DELETE | `/api/v1/ota/image` | Cancel, discard, or clear |
| POST | `/api/v1/ota/activate` | Make an image the boot image, and restart |
| POST | `/api/v1/ota/pull` | Download an image from a URL or release channel |
| GET | `/api/v1/ota/check` | What a pull would fetch, and whether it is newer |

`v1` changes only when an existing client would break; new endpoints and new fields arrive without
it changing.
