// The board's console (linenoise + esp_console) as the pages use it: run a command and get its
// output back, or hand the port to a file transfer. A port of the Console class in
// esp-console-kit's tools/fs_xfer.py, which is the reference for everything here.
//
// No DOM. Every byte the board sends is also passed on, as text, to `onText`, so a page can show
// the whole session -- echoes, prompts, log lines and all.

import { BoardError, ByteQueue } from "./serial.js";

const ESC = "\x1b";
const ANSI = /\x1b\[[0-9;?]*[A-Za-z]/g;
const PARTIAL_ESCAPE = /\x1b(\[[0-9;?]*)?$/;
// The prompt, once ANSI colours are gone: "holo> " at the end of what has arrived.
const PROMPT = /(?:^|\n)[^\n]*\S+> $/;
export const READY = /xmodem: ready to (receive|send) (\d+|\?) bytes at (\d+) baud: (\S+)/;
const FAILED = /^(Command returned non-zero error code: .*|Unrecognized command)\s*$/m;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// Drop the echoed command line and the trailing prompt.
export function stripEcho(text, cmd) {
  // Lines end \r\n, or \r\r\n (`version`); a \r within a line is linenoise redrawing it.
  let lines = text.split("\n").map((line) => {
    const trimmed = line.replace(/\r+$/, "");
    return trimmed.slice(trimmed.lastIndexOf("\r") + 1);
  });
  const echo = cmd ? lines.findIndex((line) => line.includes(cmd)) : -1;
  if (echo >= 0 && echo < 2) lines = lines.slice(echo + 1);
  if (lines.length && PROMPT.test(lines[lines.length - 1])) lines = lines.slice(0, -1);
  return lines.join("\n").replace(/^\n+|\n+$/g, "");
}

// Quote one argument for esp_console's splitter, which understands "..." with \" and \\ inside.
export function quoteArg(arg) {
  return /^[^\s"'\\]+$/.test(arg) ? arg : `"${arg.replace(/[\\"]/g, (c) => `\\${c}`)}"`;
}

// One at a time: a command, or a transfer, holds the console until it is done.
class Lock {
  constructor() {
    this.tail = Promise.resolve();
  }

  run(fn) {
    const result = this.tail.then(fn);
    this.tail = result.catch(() => {});
    return result;
  }
}

export class Console {
  constructor(port) {
    this.port = port;
    this.onText = null;        // (text) => void, ANSI removed, \r kept for the terminal to handle
    this.text = "";            // what has arrived since the last take(), ANSI removed
    this.pending = "";         // an escape sequence split across reads
    this.waiters = new Set();
    this.lock = new Lock();
    this.decoder = new TextDecoder();
    this.running = null;       // the command in progress, for the pages' "running" state
    this.queue = null;         // while a transfer holds the port
    this.dirty = true;         // not known to be at a prompt: sync before the next command
    this.quiet = false;        // a page's own query is running: keep its output out of onText
    port.sink = (bytes) => this.#feed(bytes);
  }

  #feed(bytes) {
    if (this.queue) {
      this.queue.push(bytes);
      return;
    }
    let text = this.pending + this.decoder.decode(bytes, { stream: true });
    const partial = PARTIAL_ESCAPE.exec(text);
    this.pending = partial ? partial[0] : "";
    if (partial) text = text.slice(0, partial.index);
    // linenoise asks where the cursor is at the start of every line, and swallows typed input
    // until it gets an answer; at boot it asks whether there is a terminal at all.
    if (text.includes(`${ESC}[6n`)) this.port.write(new TextEncoder().encode(`${ESC}[24;80R`)).catch(() => {});
    if (text.includes(`${ESC}[5n`)) this.port.write(new TextEncoder().encode(`${ESC}[0n`)).catch(() => {});
    const clean = text.replace(ANSI, "").replace(/\0/g, "");   // smart mode pads its queries with NULs
    if (!clean) return;
    this.text += clean;
    if (!this.quiet) this.onText?.(clean);
    for (const wake of this.waiters) wake();
  }

  atPrompt() {
    return PROMPT.test(this.text.replace(/\r/g, ""));
  }

  // At the prompt that follows a line just sent. In smart mode linenoise redraws the prompt as
  // each typed character arrives ("\rholo> f", "\rholo> fs", …), and a read can end right after
  // one of those prompts; only the Enter that ends the line produces a newline.
  #doneAfterLine() {
    const newline = this.text.indexOf("\n");
    return newline >= 0 && PROMPT.test(this.text.slice(newline).replace(/\r/g, ""));
  }

  take() {
    const out = this.text;
    this.text = "";
    return out;
  }

  // Resolve when `test()` holds, or false after `ms` (or when `signal` aborts).
  #until(test, ms, signal) {
    return new Promise((resolve) => {
      let timer = null;
      const done = (value) => {
        this.waiters.delete(check);
        clearTimeout(timer);
        signal?.removeEventListener("abort", aborted);
        resolve(value);
      };
      const check = () => { if (test()) done(true); };
      const aborted = () => done(false);
      this.waiters.add(check);
      if (Number.isFinite(ms)) timer = setTimeout(() => done(false), ms);
      signal?.addEventListener("abort", aborted);
      check();
    });
  }

  async #send(line) {
    await this.port.write(new TextEncoder().encode(`${line}\r`));
  }

  // Get to a fresh prompt: press Enter and wait for one. Throws "no-prompt" when the board does
  // not answer -- not running the application, or in its ROM bootloader. Smart linenoise redraws
  // even an empty line on Enter ("\rholo> "), so the prompt must follow a newline -- or be all
  // that arrived, from a command the Enter has just ended (`imu`).
  async #sync(ms = 3000) {
    for (let attempt = 0; attempt < 2; attempt++) {
      this.take();
      await this.#send("");
      if (await this.#until(() => this.#doneAfterLine() || /^[^\r\n]*\S+> $/.test(this.text), ms)) {
        this.take();
        this.dirty = false;
        return;
      }
    }
    throw new BoardError("no-prompt", this.take());
  }

  // Once, after opening: a board that has just been reset by the open prints its boot messages
  // first, so wait a little longer for them to finish.
  async start() {
    await this.lock.run(async () => {
      await sleep(150);
      try {
        await this.#sync(1500);
      } catch {
        await this.#sync(4000);
      }
    });
  }

  // Run a command and return { ok, text }: its output without the echo and the prompt, and
  // whether the console reported it as failed. With no `timeout` it waits for the prompt as long
  // as it takes, until `signal` aborts; then the console is resynchronised. `quiet` keeps the
  // exchange out of onText: for the pages' own questions (`help`, `fs ls`), not the user's.
  command(cmd, { timeout = Infinity, signal, quiet = false } = {}) {
    return this.lock.run(async () => {
      this.running = cmd;
      this.quiet = quiet;
      try {
        if (this.dirty) await this.#sync();
        this.take();
        await this.#send(cmd);
        const ok = await this.#until(() => this.#doneAfterLine(), timeout, signal);
        const text = stripEcho(this.take(), cmd);
        if (!ok) {
          this.dirty = true;
          if (signal?.aborted) throw new BoardError("aborted", text);
          throw new BoardError("timeout", `no prompt after ${cmd}: ${text}`);
        }
        return { ok: !FAILED.test(text), text: text.replace(FAILED, "").replace(/\n+$/, "") };
      } finally {
        this.running = null;
        this.quiet = false;
      }
    });
  }

  // For a command that runs until a key is pressed (imu), or to wake a stuck one.
  async sendKey() {
    await this.#send("");
  }

  // A file transfer: send `cmd`, and when the board says it is ready, hand the raw port to
  // `transfer(ready, queue, port)` until it returns; then back to the console, and the board's
  // report after the transfer. Returns { refused, text } when the board declined, or
  // { result, text } with the transfer's own result and the board's report.
  transfer(cmd, transfer, { readyTimeout = 10000, reportTimeout = 15000 } = {}) {
    return this.lock.run(async () => {
      this.running = cmd;
      try {
        if (this.dirty) await this.#sync();
        this.take();
        await this.#send(cmd);
        let ready = null;
        const seen = await this.#until(() => {
          ready = READY.exec(this.text);
          return (ready && this.text.indexOf("\n", ready.index) >= 0) || this.#doneAfterLine();
        }, readyTimeout);
        if (!ready) {
          const text = stripEcho(this.take(), cmd);
          if (!seen) {
            this.dirty = true;
            throw new BoardError("timeout", `no answer to ${cmd}: ${text}`);
          }
          return { refused: true, text: text.replace(FAILED, "").trim() };
        }
        this.take();
        this.queue = new ByteQueue();
        let result;
        let failure = null;
        try {
          result = await transfer(ready, this.queue, this.port);
        } catch (error) {
          failure = error;
        } finally {
          this.queue = null;
        }
        // The board finishes, prints how it went, and goes back to its prompt.
        if (!(await this.#until(() => this.atPrompt(), reportTimeout))) this.dirty = true;
        const text = stripEcho(this.take(), "").replace(FAILED, "").trim();
        if (failure) {
          failure.report = text;
          throw failure;
        }
        return { result, text };
      } finally {
        this.running = null;
      }
    });
  }

  async resync() {
    await this.lock.run(() => this.#sync());
  }
}
