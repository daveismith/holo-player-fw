# The board over Wi-Fi

Once the board is on your Wi-Fi, it serves a small web app. It works on a phone or a computer, with
nothing to install. Today it has two pages:

- **Status**: the firmware it runs, how it is connected, and whether a newer release is out.
- **Update**: [install new firmware](ota.md#updating-over-wi-fi) from a file, or have the board
  fetch the latest release itself.

Settings and clip uploads will join them. Everything the app does is an
[HTTP API](../reference/http-api.md) call, so a script can do the same.

## Reaching it

### On your network

First, put the board on your Wi-Fi from its console. It remembers the network and rejoins it at every
boot:

```
wifi_save <ssid> <passphrase>
```

Then ask the board where to find it:

```
web
```

```text
web: on
  http://10.0.19.118/
  http://holo-2db0.local/
password: none: anyone on the network can update the board
```

Either address works:

- **`http://holo-xxxx.local/`**: `xxxx` is the end of the board's MAC address, so each board has its
  own name. This works from macOS, iOS, Windows and Linux, and from most Android phones. To name
  the board yourself, use `web hostname <name>`.
- **The address itself**: always works. Your router's device list shows it too, under the same name.

### With no network: the board's own access point

Out in the field, the board can be the network. On its console, run:

```
wifi ap on
```

```text
I (59133) wifi_ap: access point holo-2db0 up at 192.168.4.1
ap: on, holo-2db0, channel 11, 0 clients
pass: taxg-kerx-k4cp
page: http://192.168.4.1/
```

Then:

1. Join **`holo-xxxx`** from your phone or laptop with the passphrase shown.
2. The phone offers to sign in to the network, which opens the board's page. If it doesn't, open
   **`http://192.168.4.1/`**.
3. If the page opened in the phone's small sign-in window, open `http://192.168.4.1/` in the real
   browser before uploading files. The sign-in window can't reliably pick files.

Keep these in mind:

- **The access point is off after every restart**, including the restart that finishes an update.
  Run `wifi ap on` again to rejoin.
- **The passphrase is random.** The board makes it the first time and keeps it. `wifi ap` shows it,
  and `wifi ap --pass <passphrase>` changes it. To change the network's name, use
  `wifi ap --ssid <name>`.
- **The station keeps working.** If the board is also on your Wi-Fi, both addresses work at once.
  The access point then shares your network's channel.
- **The board has no internet on its own access point**, so it can't fetch releases itself. The
  Update page offers to fetch the release on your phone and send it on instead. That works if the
  phone still has mobile data.

## A password

With no password, anyone on the same network can update or restart the board. On a network you
share, set a password:

```
web password <password>
```

Seeing the status and the slots stays open to everyone. Changes (uploading, switching or restarting)
now ask for the password. The web app asks for it once and keeps it until you close the tab. To
remove it, run:

```
web password --clear
```

Even with no password set, a web page elsewhere can't drive the board from your browser. The board
answers only requests that name it, and only in forms a browser won't send across sites, except
for a few [trusted sites](#other-sites-and-api-tools). The
[HTTP API reference](../reference/http-api.md#protection) has the details.

## Other sites and API tools

Pages on a few trusted sites may call the board's API from your browser:
[Swagger Editor](https://editor.swagger.io), and this project's `astromech.co` and
`davidiansmith.ca`, with their subdomains. That is what lets the
[HTTP API explorer](../reference/http-api-explorer.md) try requests on a real board. Every other
site is refused. To see or change the list, run:

```
web cors
```

The [HTTP API reference](../reference/http-api.md#protection) covers adding and removing sites.
Set a password before you add a site you don't control.

## Turning it off

To stop the web server, run:

```
web off
```

The board remembers this across restarts. `web on` starts the server again.

## Working on the web app

The pages are in `web/` in the repository. They are plain JavaScript modules and CSS, with no build
step. The firmware build gzips them into the image. To try a change without rebuilding and
reflashing, serve them from disk and pass the API through to a real board:

```sh
python tools/web_dev.py holo-2db0.local
# then open http://localhost:8080/
```

A new page is one module in `web/pages/`, listed in `web/app.js`. The page appears when the board's
`/api/v1/info` lists its feature.
