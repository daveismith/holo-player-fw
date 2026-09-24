---
hide:
  - toc
---

# The board in the browser

This page talks to the board over its USB-C port, from the browser. It can put clips and images
on the board and take them off, play and show them, and run every console command, without
installing anything. It needs desktop Chrome, Edge or Opera. In any other browser, use a serial
terminal at 115200 baud and [`fs_xfer.py`](files.md#from-the-host) instead.

<div id="holo-board" class="hb-static" markdown>
**This page can't talk to the board here.** It needs JavaScript, and desktop Chrome, Edge or
Opera. Opened from the documentation zip? Browsers won't run it from a file: run
`python3 serve.py` in the unzipped folder, as the [Install](../install/flashing.md) page
describes.
</div>

## Connecting

Press **Connect to the board** and pick the board's port. On macOS it is listed twice, as
`cu.wchusbserial…` and `cu.usbmodem…`, and either works. Connecting doesn't restart the board: a
clip that is playing keeps playing.

Only one program can use the port at a time. Close `idf.py monitor`, any serial terminal, and the
installer first.

Once connected, the connection follows you around this site. The **Board** button in the header
shows it on every page, and each board command in these pages gets a ▶ that runs it there and
then. The output appears underneath. Commands that change something, such as deleting a file,
restarting the board or changing a setting, ask first.

## Files

The file manager shows `/data`, the board's clip volume (see
[Files on the board](files.md)):

- **Upload…**, or drop files on the list, to put them in the folder shown. A file that is
  already there is only replaced if you say so.
- ▶ plays a clip, and ◉ shows an image, on the board's screen.
- ↓ downloads a file. ✎ renames it and ✕ deletes it.

Every upload is checked the way `fs_xfer.py` checks it. The board hashes what it received and
the page compares that with the file's SHA-256. Then the board reads the file back from flash
and hashes it again. A download is checked against the board's own hash. An upload that fails
or is cancelled leaves nothing behind.

Files move over the console at 115200 baud: about 7 KB/s up and 10 KB/s down, so a 1.9 MB clip
takes four to five minutes to upload. Keep the tab in front while files move, because browsers slow
down tabs in the background. `fs_xfer.py` on the command line is no faster. See
[Speed](files.md#speed) for why.

## Commands

The command list comes from the board itself, so it always matches the firmware it runs. Each
command has a line to edit and run:

- The buttons above the line are the ways to call it, taken from its own syntax. Pressing one
  fills in the line and selects the first part to replace, such as `<file>`.
- **Insert a file…** puts the name of a file on the board into the line. **Colour** puts in a
  colour.
- **Stop** ends a command that runs until a key is pressed, such as `imu`. Pressing it again
  stops waiting.

`fs put`, `fs get` and `ota put` aren't run from here, because they wait for a file transfer.
Use the file manager for files, and [`fs_xfer.py ota`](ota.md) for updates.

## Console

Everything the board prints appears in the console, including the commands run from the
other panes and the board's own log messages. Type a command and press Enter to run it; ↑ and ↓
step through what you typed. **Send a key** does what pressing a key in a terminal does.
