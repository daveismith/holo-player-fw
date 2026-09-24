// Tests for the board pages' JavaScript (manual/javascripts/board/): the parsers against output
// captured from a real board (tools/fixtures/board/), XMODEM both ways, and the console and file
// transfers against a simulated board behind a fake Web Serial port.
//
//   node --test tools/test_board.mjs
//
// `make docs-check` runs this when node is installed. Node's built-in test runner: no packages.

import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import { BoardError, BoardPort, ByteQueue } from "../manual/javascripts/board/serial.js";
import { Console, quoteArg, stripEcho } from "../manual/javascripts/board/console.js";
import { Files } from "../manual/javascripts/board/files.js";
import {
  hintVariants, parseDf, parseHelp, parseLs, parseOta, parseSha256, parseVersion, placeholders,
} from "../manual/javascripts/board/parse.js";
import { COMMANDS, commandLine, danger, entries, groupOf, isCommand, refusal, words } from "../manual/javascripts/board/commands.js";
import { ACK, CAN, crc16, EOT, frame, NAK, receive, send, SOH, STX, SUB } from "../manual/javascripts/board/xmodem.js";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const fixture = (name) => readFileSync(join(ROOT, "tools", "fixtures", "board", name));
const text = (name) => fixture(name).toString("utf8");
const bytes = (s) => new TextEncoder().encode(s);
const sha = (data) => createHash("sha256").update(data).digest("hex");
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// --- parsers, against a real board's output ----------------------------------------------------

test("fs ls: files and directories, sorted, directories first", () => {
  assert.deepEqual(parseLs(text("fs_ls.txt")), [
    { name: "clips", dir: true, size: null },
    { name: "calibration.png", dir: false, size: 3260 },
    { name: "history.txt", dir: false, size: 180 },
  ]);
  assert.deepEqual(parseLs(text("fs_ls_clips.txt")), [{ name: "demo.jpg", dir: false, size: 483254 }]);
  assert.deepEqual(parseLs(text("fs_ls_nope.txt")), []);
  assert.deepEqual(parseLs("        12  name with  two spaces.txt\n1 file, 12 bytes\n"),
    [{ name: "name with  two spaces.txt", dir: false, size: 12 }]);
});

test("fs df, fs sha256", () => {
  assert.deepEqual(parseDf(text("fs_df.txt")), { mount: "/data", usedKB: 496, totalKB: 11264, freeKB: 10768 });
  assert.equal(parseSha256(text("fs_sha256.txt")), "63f14b9f5a45e54d4fdb0739670c6c97f8139b1c6abb5debf0ad2b99c90a4f36");
  assert.equal(parseDf("nonsense"), null);
});

test("ota status", () => {
  const [a, b] = parseOta(text("ota_status.txt"));
  assert.equal(a.label, "ota_0");
  assert.equal(a.offset, 0x10000);
  assert.equal(a.sizeKB, 2304);
  assert.ok(a.running && a.boots && !a.empty);
  assert.equal(a.project, "holo-player-fw");
  assert.equal(a.version, "5c48843");
  assert.equal(a.note, "confirmed");
  assert.equal(b.label, "ota_1");
  assert.ok(b.empty && !b.running);
});

test("version", () => {
  assert.deepEqual(parseVersion(text("version.txt")), {
    project: "holo-player-fw", version: "5c48843", built: "Sep 23 2026 23:16:59", idf: "v6.1", chip: "ESP32-S3",
  });
});

test("help: every command, with its hint and a description", () => {
  const commands = parseHelp(text("help.txt"));
  const names = commands.map((c) => c.name);
  assert.ok(commands.length > 50, `${commands.length} commands`);
  for (const name of names) assert.ok(COMMANDS.has(name), `${name} is in GROUPS`);
  const video = commands.find((c) => c.name === "video");
  assert.equal(video.hint, "play <file> [loop] [frame] | stop | status | info <file> | verify <file> [step]");
  assert.match(video.description, /^Play a Motion-JPEG QuickTime file on the panel/);
  const free = commands.find((c) => c.name === "free");
  assert.equal(free.hint, "");
  assert.equal(free.description, "Get the current size of free heap memory");
  const ping = commands.find((c) => c.name === "ping");
  assert.match(ping.glossary, /--count=<n>/);
  assert.doesNotMatch(ping.description, /--count/);
});

test("hints into ways to call a command", () => {
  const variants = (hint) => hintVariants(hint).map((v) => [v.label, v.template]);
  assert.deepEqual(variants("play <file> [loop] [frame] | stop | status | info <file> | verify <file> [step]"), [
    ["play", "play <file>"], ["stop", "stop"], ["status", "status"], ["info", "info <file>"], ["verify", "verify <file>"]]);
  assert.deepEqual(variants("[colour <c> | off | wipe [<c>] [loop] | rainbow [loop] | bright <1-100>]"), [
    ["colour", "colour <c>"], ["off", "off"], ["wipe", "wipe"], ["rainbow", "rainbow"], ["bright", "bright <1-100>"]]);
  assert.deepEqual(variants("[on|off|status]"), [["on", "on"], ["off", "off"], ["status", "status"]]);
  assert.deepEqual(variants("get <n> | set <n> 0|1 | release <n>"), [["get", "get <n>"], ["set", "set <n>"], ["release", "release <n>"]]);
  assert.deepEqual(variants("[bl <0-100> | bench [frames]]"), [["bl", "bl <0-100>"], ["bench", "bench"]]);
  assert.deepEqual(variants("ls|df|stat|mkdir|rmdir|rm|mv|cat|hexdump|sha256|put|get|bench ...").map(([l]) => l),
    ["ls", "df", "stat", "mkdir", "rmdir", "rm", "mv", "cat", "hexdump", "sha256", "put", "get", "bench"]);
  assert.deepEqual(variants("[-r <hz>] [-n <count>] | id"), [["id", "id"]]);
  assert.deepEqual(variants("[-W <t>] [-i <t>] [-s <n>] [-c <n>] [-Q <n>] [-T <n>] <host>"), []);
  assert.deepEqual(variants(""), []);
  // Every hint the firmware has parses without throwing, and no template has an optional part.
  for (const command of parseHelp(text("help.txt"))) {
    for (const variant of hintVariants(command.hint)) assert.doesNotMatch(variant.template, /\[/, command.name);
  }
});

test("entries: a command split into its sub-commands", () => {
  const help = parseHelp(text("help.txt"));
  const of = (name) => entries(name, help.find((c) => c.name === name).hint).map((e) => [e.label, e.hint, e.line]);
  assert.deepEqual(of("video"), [
    ["video play", "<file> [loop] [frame]", "video play <file>"], ["video stop", "", "video stop"],
    ["video status", "", "video status"], ["video info", "<file>", "video info <file>"],
    ["video verify", "<file> [step]", "video verify <file>"]]);
  // Runnable bare, so listed bare too: `leds` reports the strip.
  assert.deepEqual(of("leds").slice(0, 2), [["leds", "", "leds"], ["leds colour", "<c>", "leds colour <c>"]]);
  // fs's arguments come from its usage text; put and get are the file manager's.
  const fs = of("fs");
  assert.deepEqual(fs.find(([label]) => label === "fs mv"), ["fs mv", "<from> <to>", "fs mv <from> <to>"]);
  assert.ok(!fs.some(([label]) => label === "fs put" || label === "fs get"));
  assert.deepEqual(of("ota"), [["ota", "", "ota"]]);
  assert.deepEqual(of("free"), [["free", "", "free"]]);
  assert.deepEqual(of("ping")[0].slice(0, 1), ["ping"]);
  // Every command gives at least one entry, and no entry's line has an optional part.
  for (const command of help) {
    const list = entries(command.name, command.hint);
    assert.ok(list.length, command.name);
    for (const entry of list) assert.doesNotMatch(entry.line, /\[/, entry.label);
  }
});

test("placeholders", () => {
  assert.deepEqual(placeholders("video play <file> loop").map((p) => [p.text, p.start]), [["<file>", 11]]);
  assert.deepEqual(placeholders("leds colour #ff8000"), []);
});

test("commands: grouping, lines from the docs, what needs a confirmation", () => {
  assert.equal(groupOf("leds"), "LEDs");
  assert.equal(groupOf("servo_move"), "Holoprojector");
  assert.equal(groupOf("screen"), "Display");
  assert.equal(groupOf("nonesuch"), "Other");
  assert.equal(commandLine("holo help       # every verb and its options"), "holo help");
  assert.equal(commandLine("leds colour #ff8000"), "leds colour #ff8000");
  assert.ok(isCommand("leds rainbow loop"));
  assert.ok(!isCommand("image: showing /data/r2.png (240x240 PNG)"));
  assert.ok(!isCommand("ota_0    0x010000  2304 KB  running"));
  assert.ok(!isCommand("fs_xfer.py run \"fs ls\""));
  assert.deepEqual(words('fs mv "a b.mov" c\\ d'), ["fs", "mv", "a b.mov", "c\\", "d"]);
  assert.equal(refusal("fs put -f x 12"), "transfer");
  assert.equal(refusal("ota put 1000"), "ota");
  assert.equal(refusal("ota"), null);
  assert.match(danger("fs rm old.mov"), /deletes old\.mov/);
  assert.equal(danger("fs ls"), null);
  assert.equal(danger("gpio get 4"), null);
  assert.ok(danger("gpio set 4 1"));
  assert.ok(danger("restart"));
  assert.equal(danger("leds rainbow loop"), null);
});

test("quoting arguments for esp_console", () => {
  assert.equal(quoteArg("leia.mov"), "leia.mov");
  assert.equal(quoteArg("my clip.mov"), '"my clip.mov"');
  assert.equal(quoteArg('say "hi"'), '"say \\"hi\\""');
});

// --- XMODEM ------------------------------------------------------------------------------------

test("crc16 is CRC-16/XMODEM, as Python's binascii.crc_hqx", () => {
  assert.equal(crc16(bytes("123456789")), 0x31c3);
  assert.equal(crc16(new Uint8Array(0)), 0);
  assert.equal(crc16(new Uint8Array(1024).fill(0x41)), 0x0179);
  assert.equal(crc16(Uint8Array.from({ length: 256 }, (_, i) => i)), 0x7e55);
});

test("frames: 1K and 128-byte blocks, padding, numbering", () => {
  const big = frame(1, new Uint8Array(1024).fill(0x41));
  assert.equal(big.length, 1029);
  assert.deepEqual([...big.subarray(0, 3)], [STX, 1, 0xfe]);
  assert.deepEqual([...big.subarray(1027)], [0x01, 0x79]);
  const last = frame(256, bytes("abc"));
  assert.equal(last.length, 133);
  assert.deepEqual([...last.subarray(0, 3)], [SOH, 0, 0xff]);
  assert.equal(last[6], SUB);
  assert.equal(frame(3, new Uint8Array(200), true).length, 133);   // forced small, first 128 of it
});

// Two ends of a line, each with its own queue.
function line() {
  const a = new ByteQueue();
  const b = new ByteQueue();
  return {
    host: { queue: a, port: { write: async (data) => b.push(data.slice()) } },
    device: { queue: b, port: { write: async (data) => a.push(data.slice()) } },
  };
}

for (const size of [0, 1, 128, 129, 1024, 1025, 300 * 1024 + 7]) {
  test(`XMODEM round trip, ${size} bytes`, async () => {
    const data = Uint8Array.from({ length: size }, (_, i) => (i * 7 + (i >> 8)) & 0xff);
    const { host, device } = line();
    const progress = [];
    const [sent, got] = await Promise.all([
      send(host.queue, host.port, data, { progress: (done) => progress.push(done) }),
      receive(device.queue, device.port, size),
    ]);
    assert.equal(sent.retries, 0);
    assert.equal(sha(got), sha(data));
    if (size) assert.equal(progress.at(-1), size);
  });
}

test("XMODEM send falls back to 128-byte blocks when 1K blocks keep failing", async () => {
  // A receiver that NAKs every 1K block, as the board does through Apple's driver.
  const data = Uint8Array.from({ length: 5000 }, (_, i) => i & 0xff);
  const { host, device } = line();
  const receiver = (async () => {
    const out = [];
    await device.port.write(new Uint8Array([0x43]));
    for (;;) {
      const header = await device.queue.readByte(5000);
      if (header === EOT) {
        await device.port.write(new Uint8Array([ACK]));
        return new Uint8Array(out.flat());
      }
      const n = header === STX ? 1024 : 128;
      const body = await device.queue.read(n + 4, 5000);
      if (header === STX) {
        await device.port.write(new Uint8Array([NAK]));
        continue;
      }
      out.push([...body.subarray(2, 2 + n)]);
      await device.port.write(new Uint8Array([ACK]));
    }
  })();
  const sent = await send(host.queue, host.port, data);
  const got = await receiver;
  assert.ok(sent.small);
  assert.equal(sent.retries, 2);
  assert.equal(sha(got.subarray(0, data.length)), sha(data));
});

test("XMODEM send stops when the other end cancels", async () => {
  const { host, device } = line();
  const sending = send(host.queue, host.port, new Uint8Array(4000));
  await device.port.write(new Uint8Array([0x43]));
  await device.queue.read(1029, 2000);
  await device.port.write(new Uint8Array([CAN]));
  await assert.rejects(sending, /cancelled at block 1/);
});

// --- a board behind a fake Web Serial port -----------------------------------------------------

class FakeSerialPort {
  constructor(board) {
    this.board = board;
    this.signals = [];
    this.opened = false;
  }

  getInfo() {
    return { usbVendorId: 0x1a86, usbProductId: 0x55d3 };
  }

  async open({ baudRate }) {
    if (this.board.busy) throw Object.assign(new Error("Failed to open serial port."), { name: "NetworkError" });
    this.baudRate = baudRate;
    this.opened = true;
    this.readable = new ReadableStream({ start: (controller) => { this.controller = controller; } });
    this.writable = new WritableStream({ write: (chunk) => this.board.receive(chunk) });
    this.board.attach((data) => this.controller.enqueue(data));
  }

  async setSignals(signals) {
    this.signals.push(signals);
  }

  async close() {
    this.opened = false;
    try { this.controller.close(); } catch { /* already */ }
  }
}

// Enough of the firmware's console for these tests: linenoise in dumb or smart mode, a few
// commands, and fs put / fs get over XMODEM. Output goes out in small, uneven pieces, as a UART
// delivers it.
class FakeBoard {
  constructor({ smart = false, nak1k = false } = {}) {
    this.smart = smart;
    this.nak1k = nak1k;
    this.files = new Map([["calibration.png", bytes("PNG".repeat(100))]]);
    this.line = "";
    this.mode = "console";
    this.rx = new ByteQueue();
    this.answered = 0;
    this.out = Promise.resolve();
  }

  attach(emit) {
    this.emit = emit;
  }

  // Bytes to the host, in pieces of 1 to 7 bytes.
  say(data) {
    const all = typeof data === "string" ? bytes(data) : data;
    this.out = this.out.then(async () => {
      for (let i = 0, n = 1; i < all.length; i += n, n = (n % 7) + 1) {
        this.emit(all.slice(i, i + n));
        await sleep(0);
      }
    });
    return this.out;
  }

  prompt() {
    if (!this.smart) return this.say("holo> ");
    this.awaitingCursor = true;
    return this.say("\x1b[6n\0");
  }

  receive(chunk) {
    if (this.mode !== "console") {
      this.rx.push(chunk);
      return;
    }
    for (const c of new TextDecoder().decode(chunk)) {
      if (this.awaitingCursor) {
        this.cursorReply = (this.cursorReply ?? "") + c;
        if (c === "R") {
          assert.equal(this.cursorReply, "\x1b[24;80R");
          this.cursorReply = "";
          this.awaitingCursor = false;
          this.answered++;
          this.say("holo> ");
        }
        continue;
      }
      if (c === "\r") {
        const cmd = this.line;
        this.line = "";
        this.say(this.smart ? `\r\x1b[0Kholo> ${cmd}\r\x1b[${6 + cmd.length}C\r\n` : "\r\n");
        this.run(cmd);
      } else {
        this.line += c;
        this.say(this.smart ? `\r\x1b[0Kholo> ${this.line}` : c);
      }
    }
  }

  fail(message) {
    this.say(`${message}\r\nCommand returned non-zero error code: 0x1 (ERROR)\r\n`);
    return this.prompt();
  }

  run(cmd) {
    const [name, sub, ...args] = words(cmd);
    if (!name) return this.prompt();
    if (name === "version") {
      this.say(text("version.txt").replace(/\n/g, "\r\r\n"));
    } else if (name === "fs" && sub === "ls") {
      for (const [file, data] of [...this.files].sort()) this.say(`${String(data.length).padStart(10)}  ${file}\r\n`);
      this.say(`${this.files.size} files, 0 bytes\r\n`);
    } else if (name === "fs" && sub === "df") {
      this.say("/data: 496 KB used of 11264 KB, 10768 KB free\r\n");
    } else if (name === "fs" && sub === "sha256") {
      if (!this.files.has(args[0])) return this.fail(`fs: sha256 /data/${args[0]}: No such file or directory`);
      this.say(`${sha(this.files.get(args[0]))}  /data/${args[0]}\r\n3260 bytes in 0.00 s (5351 KB/s)\r\n`);
    } else if (name === "fs" && sub === "rm") {
      if (!this.files.delete(args[0])) return this.fail(`fs: rm /data/${args[0]}: No such file or directory`);
    } else if (name === "fs" && sub === "put") {
      const force = args[0] === "-f";
      const [path, size] = force ? args.slice(1) : args;
      if (this.files.has(path) && !force) return this.fail(`fs: put: /data/${path} exists (-f to replace it)`);
      this.say(`xmodem: ready to receive ${size} bytes at 115200 baud: /data/${path}\r\n`)
        .then(() => this.xmodemIn(path, Number(size)));
      return;
    } else if (name === "fs" && sub === "get") {
      const path = args.at(-1);
      if (!this.files.has(path)) return this.fail(`fs: get /data/${path}: No such file or directory`);
      this.say(`xmodem: ready to send ${this.files.get(path).length} bytes at 115200 baud: /data/${path}\r\n`)
        .then(() => this.xmodemOut(path));
      return;
    } else if (name === "log") {
      this.say("I (1234) wifi: something happened\r\n");
    } else if (name === "imu") {
      // Streams until a key is pressed.
      this.mode = "stream";
      this.rx.clear();
      (async () => {
        while (!(await this.rx.read(1, 5)).length) await this.say("ax 0.01 ay 0.00 az 1.00\r\n");
        this.mode = "console";
        this.prompt();
      })();
      return;
    } else {
      this.say("Unrecognized command\r\n");
    }
    return this.prompt();
  }

  async xmodemIn(path, size) {
    this.mode = "xmodem";
    const port = { write: (data) => { this.emit(data.slice()); return Promise.resolve(); } };
    const rx = this.rx;
    // The board's receiver, NAKing 1K blocks when asked to.
    const out = [];
    await port.write(new Uint8Array([0x43]));
    for (;;) {
      const header = await rx.readByte(5000);
      if (header === EOT) {
        await port.write(new Uint8Array([ACK]));
        break;
      }
      const n = header === STX ? 1024 : 128;
      const body = await rx.read(n + 4, 5000);
      if (header === STX && this.nak1k) {
        await port.write(new Uint8Array([NAK]));
        continue;
      }
      out.push(...body.subarray(2, 2 + n));
      await port.write(new Uint8Array([ACK]));
    }
    this.mode = "console";
    const data = new Uint8Array(out.slice(0, size));
    this.files.set(path, data);
    this.say(`received ${size} bytes in 1.0 s (10.0 KB/s, 0 retries)\r\nsha256 ${sha(data)}  /data/${path}\r\n`);
    this.prompt();
  }

  async xmodemOut(path) {
    this.mode = "xmodem";
    const port = { write: (data) => { this.emit(data.slice()); return Promise.resolve(); } };
    await send(this.rx, port, this.files.get(path));
    this.mode = "console";
    this.say(`sent ${this.files.get(path).length} bytes in 1.0 s (10.0 KB/s, 0 retries)\r\n`);
    this.prompt();
  }
}

async function connect(options) {
  const board = new FakeBoard(options);
  const serial = new FakeSerialPort(board);
  const port = new BoardPort(serial);
  await port.open();
  const console = new Console(port);
  let seen = "";
  console.onText = (t) => { seen += t; };
  await console.start();
  return { board, serial, port, console, seen: () => seen };
}

test("opening drops RTS before DTR, so the board is not reset", async () => {
  const { serial, port } = await connect();
  assert.deepEqual(serial.signals, [{ requestToSend: false }, { dataTerminalReady: false }]);
  await port.close();
  assert.equal(serial.opened, false);
});

test("a port another program holds is reported as busy", async () => {
  const board = new FakeBoard();
  board.busy = true;
  const port = new BoardPort(new FakeSerialPort(board));
  await assert.rejects(port.open(), (error) => error instanceof BoardError && error.kind === "busy");
});

for (const smart of [false, true]) {
  test(`commands, ${smart ? "smart" : "dumb"} linenoise`, async () => {
    const { board, console, port, seen } = await connect({ smart });
    const version = await console.command("version", { timeout: 5000 });
    assert.equal(version.ok, true);
    assert.equal(parseVersion(version.text).version, "5c48843");
    const ls = await console.command("fs ls", { timeout: 5000 });
    assert.deepEqual(parseLs(ls.text), [{ name: "calibration.png", dir: false, size: 300 }]);
    const bad = await console.command("fs rm nope", { timeout: 5000 });
    assert.equal(bad.ok, false);
    assert.equal(bad.text, "fs: rm /data/nope: No such file or directory");
    const log = await console.command("log", { timeout: 5000 });
    assert.equal(log.text, "I (1234) wifi: something happened");
    assert.match(seen(), /holo> /);
    assert.doesNotMatch(seen(), /\x1b|\0/);
    if (smart) assert.ok(board.answered >= 4, `answered ${board.answered} cursor queries`);
    await port.close();
  });
}

test("stripEcho on real bytes, both modes", () => {
  const clean = (raw) => raw.toString("latin1").replace(/\x1b\[[0-9;?]*[A-Za-z]/g, "").replace(/\0/g, "");
  const dumb = clean(fixture("fs_ls.dumb.raw"));
  const smart = clean(fixture("fs_ls.smart.raw"));
  assert.deepEqual(parseLs(stripEcho(dumb, "fs ls")).map((e) => e.name), ["clips", "calibration.png", "history.txt"]);
  assert.ok(stripEcho(smart, "fs ls").startsWith("      3260  calibration.png"));
  assert.ok(stripEcho(smart, "fs ls").endsWith("1 directory"));
  assert.match(stripEcho(clean(fixture("version.dumb.raw")), "version"), /^App:holo-player-fw 5c48843\n\tbuilt:/);
});

test("files: upload, refuse to overwrite, replace, download, delete", async () => {
  const { board, console, port } = await connect();
  const files = new Files(console);
  const data = Uint8Array.from({ length: 5000 }, (_, i) => (i * 13) & 0xff);
  const progress = [];
  const put = await files.put("clip.mov", data, { progress: (done, total) => progress.push([done, total]) });
  assert.equal(put.sha256, sha(data));
  assert.equal(put.readBack, sha(data));
  assert.deepEqual(progress.at(-1), [5000, 5000]);
  assert.equal(sha(board.files.get("clip.mov")), sha(data));
  await assert.rejects(files.put("clip.mov", data), (error) => error.kind === "exists");
  await files.put("clip.mov", data.subarray(0, 100), { force: true });
  assert.equal(board.files.get("clip.mov").length, 100);
  const got = await files.get("calibration.png");
  assert.equal(new TextDecoder().decode(got.data), "PNG".repeat(100));
  await files.rm("clip.mov");
  assert.ok(!board.files.has("clip.mov"));
  await assert.rejects(files.rm("clip.mov"), /No such file/);
  // Still in step afterwards.
  assert.equal((await console.command("fs df", { timeout: 5000 })).ok, true);
  await port.close();
});

test("files: upload through a line that spoils 1K blocks", async () => {
  const { board, console, port } = await connect({ nak1k: true });
  const data = Uint8Array.from({ length: 3000 }, (_, i) => i & 0xff);
  const put = await new Files(console).put("x.bin", data);
  assert.ok(put.small);
  assert.equal(sha(board.files.get("x.bin")), sha(data));
  await port.close();
});

test("a command that streams until a key: Stop sends one", async () => {
  const { console, port, seen } = await connect();
  const running = console.command("imu");
  await sleep(50);
  assert.match(seen(), /ax 0\.01/);
  await console.sendKey();
  const result = await running;
  assert.match(result.text, /^ax 0\.01/);
  assert.equal((await console.command("fs df", { timeout: 5000 })).ok, true);
  await port.close();
});

test("an abandoned command leaves the console usable", async () => {
  const { console, port } = await connect();
  const controller = new AbortController();
  const running = console.command("imu", { signal: controller.signal });
  await sleep(30);
  controller.abort();
  await assert.rejects(running, (error) => error.kind === "aborted");
  // The next command resynchronises first: its Enter is the key that ends the stream.
  const df = await console.command("fs df", { timeout: 5000 });
  assert.deepEqual(parseDf(df.text), { mount: "/data", usedKB: 496, totalKB: 11264, freeKB: 10768 });
  await port.close();
});

test("unknown commands fail", async () => {
  const { console, port } = await connect();
  const result = await console.command("nonesuch", { timeout: 5000 });
  assert.deepEqual(result, { ok: false, text: "" });
  await port.close();
});
