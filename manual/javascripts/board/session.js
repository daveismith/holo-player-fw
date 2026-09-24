// One connection to the board for the whole page: the Board page, the header's dock, the Run
// buttons and the file manager all share it. Events, on the exported `session`:
//
//   state   the connection changed: session.state is "idle", "connecting" or "connected"
//   text    { detail: text } everything the board printed, as the console shows it
//   busy    { detail: command or null } a command started or finished (not the pages' own queries)
//
// Moving to another page drops the port. When the user was connected, the next page reopens it
// without asking -- a port once granted to the site is in navigator.serial.getPorts() -- so the
// connection seems to follow them round the documentation. sessionStorage remembers that for this
// tab only; an explicit Disconnect forgets it.

import { BoardError, BoardPort } from "./serial.js";
import { Console } from "./console.js";
import { Files } from "./files.js";
import { parseHelp, parseOta, parseVersion } from "./parse.js";

const REMEMBER = "holo-board-connected";

function remember(on) {
  try {
    if (on) sessionStorage.setItem(REMEMBER, "1");
    else sessionStorage.removeItem(REMEMBER);
  } catch {
    // storage blocked: the connection just won't follow page changes
  }
}

function remembered() {
  try {
    return sessionStorage.getItem(REMEMBER) === "1";
  } catch {
    return false;
  }
}

class Session extends EventTarget {
  constructor() {
    super();
    this.state = "idle";
    this.port = null;
    this.console = null;
    this.files = null;
    this.board = null;         // { project, version, chip, slot } once connected
    this.help = null;          // the board's own `help`, parsed, once asked for
    this.error = null;         // the last connection error, for the pages to explain
    this.running = null;
    this.disabled = false;     // the installer has the port on this page
  }

  #set(state) {
    this.state = state;
    this.dispatchEvent(new Event("state"));
  }

  get connected() {
    return this.state === "connected";
  }

  // Connect. With `ask`, the browser's port chooser opens when there is no granted port to reuse,
  // which needs a click; without it, only a port granted before is tried, silently.
  async connect({ ask = true, choose = false } = {}) {
    if (this.disabled || this.state !== "idle") return this.connected;
    this.error = null;
    let port = null;
    try {
      if (!choose) port = await BoardPort.reuse();
      if (!port && ask) port = await BoardPort.choose();
    } catch (error) {
      if (error.kind !== "cancelled") this.error = error;
      this.#set("idle");
      return false;
    }
    if (!port) return false;

    this.#set("connecting");
    try {
      await port.open();
      this.port = port;
      this.console = new Console(port);
      this.console.onText = (text) => this.dispatchEvent(new CustomEvent("text", { detail: text }));
      this.console.onCommand = (line) => this.#busy(line);
      port.onLost = () => this.#lost();
      await this.console.start();
      this.files = new Files(this.console);
      this.board = await this.#identify();
      remember(true);
      this.#set("connected");
      return true;
    } catch (error) {
      this.error = error;
      await this.#close();
      this.#set("idle");
      return false;
    }
  }

  async #identify() {
    const version = parseVersion((await this.console.command("version", { timeout: 5000, quiet: true })).text);
    let slot = null;
    try {
      const ota = await this.console.command("ota", { timeout: 5000, quiet: true });
      slot = parseOta(ota.text).find((s) => s.running)?.label ?? null;
    } catch {
      // an older build without `ota`: nothing to show
    }
    if (version.project && version.project !== "holo-player-fw") {
      throw new BoardError("other", `${version.project} ${version.version}`);
    }
    return { ...version, slot };
  }

  async #close() {
    const port = this.port;
    this.port = null;
    this.console = null;
    this.files = null;
    this.board = null;
    this.help = null;
    this.running = null;
    await port?.close();
  }

  #lost() {
    this.error = new BoardError("lost");
    this.#close();
    this.#set("idle");
  }

  // `forget`: the user asked, so the next page shouldn't reconnect by itself.
  async disconnect({ forget = true } = {}) {
    if (forget) remember(false);
    await this.#close();
    this.#set("idle");
  }

  // Run a line and return { ok, text }. `onText` also receives the board's output while it runs,
  // for a page that shows it next to the button that ran it.
  async run(line, { signal, onText } = {}) {
    if (!this.connected) throw new BoardError("not-connected");
    const listener = (event) => onText?.(event.detail);
    this.addEventListener("text", listener);
    try {
      return await this.console.command(line, { signal });
    } finally {
      this.removeEventListener("text", listener);
    }
  }

  #busy(line) {
    this.running = line;
    this.dispatchEvent(new CustomEvent("busy", { detail: line }));
  }

  async sendKey() {
    await this.console?.sendKey();
  }

  // The board's own list of commands, asked for once a connection.
  async commands() {
    if (!this.help) this.help = parseHelp((await this.console.command("help", { timeout: 10000, quiet: true })).text);
    return this.help;
  }

  // Connect silently if this tab was connected on the page before.
  async resume() {
    if (remembered() && BoardPort.supported()) {
      if (!(await this.connect({ ask: false }))) remember(false);
    }
  }
}

export const session = new Session();

// Let go of the port as the page goes, so the next one can open it.
addEventListener("pagehide", () => {
  session.disconnect({ forget: false });
});
