// The Board page (manual/use/board.md): the connection, the file manager, every command the board
// has, and its console.

import { code, h } from "../common/dom.js";
import { GROUPS, groupOf } from "./commands.js";
import { FileManager } from "./filemanager.js";
import { confirmed, explain, Runner, vet } from "./runner.js";
import { session } from "./session.js";
import { Terminal } from "./terminal.js";

function describe(board) {
  const version = board.version ? ` ${board.version}` : "";
  const slot = board.slot ? `, running from ${board.slot}` : "";
  return `Holo Player${version}${slot}`;
}

class StatusBar {
  constructor() {
    this.dot = h("span", { class: "hb-dot", "aria-hidden": "true" });
    this.text = h("span", { class: "hb-status-text", role: "status", "aria-live": "polite" });
    this.connect = h("button", { type: "button", class: "md-button md-button--primary", onclick: () => session.connect() }, "Connect to the board");
    this.other = h("button", { type: "button", class: "md-button", onclick: () => session.connect({ choose: true }), hidden: true }, "Another port…");
    this.disconnect = h("button", { type: "button", class: "md-button", onclick: () => session.disconnect(), hidden: true }, "Disconnect");
    this.error = h("p", { class: "hb-note hb-error", hidden: true });
    this.el = h("div", { class: "hb-status" },
      h("p", { class: "hb-status-line" }, this.dot, this.text),
      h("p", { class: "hb-status-buttons" }, this.connect, this.other, this.disconnect),
      this.error);
    session.addEventListener("state", () => this.#update());
    session.addEventListener("busy", () => this.#update());
    this.#update();
  }

  #update() {
    const state = session.state;
    this.el.dataset.state = state;
    this.connect.hidden = state === "connected";
    this.connect.disabled = state === "connecting";
    this.other.hidden = state !== "idle";
    this.disconnect.hidden = state !== "connected";
    this.text.replaceChildren(...(state === "connected"
      ? [h("strong", {}, "Connected"), ` · ${describe(session.board)}`, ...(session.running ? [" · running ", code(session.running)] : [])]
      : [state === "connecting" ? "Connecting…" : "Not connected"]));
    const error = state === "idle" ? session.error : null;
    this.error.hidden = !error;
    if (error) this.error.replaceChildren(...explain(error));
  }
}

// Every command the board lists in `help`, grouped, each a Runner.
class Commands {
  constructor() {
    this.filter = h("input", { type: "search", class: "hb-filter", placeholder: "Filter commands…", "aria-label": "Filter commands", oninput: () => this.#applyFilter() });
    this.body = h("div", { class: "hb-groups" });
    this.el = h("div", { class: "hb-commands" }, this.filter, this.body);
    this.loaded = false;
    session.addEventListener("state", () => this.#state());
    this.#state();
  }

  async #state() {
    if (!session.connected) {
      this.loaded = false;
      this.filter.hidden = true;
      this.body.replaceChildren(h("p", { class: "hb-empty" }, "Connect to list the commands this board has."));
      return;
    }
    if (this.loaded) return;
    this.loaded = true;
    this.body.replaceChildren(h("p", { class: "hb-empty" }, "Asking the board for its commands…"));
    try {
      this.#render(await session.commands());
    } catch (error) {
      this.loaded = false;
      this.body.replaceChildren(h("p", { class: "hb-note hb-error" }, explain(error)));
    }
  }

  #render(commands) {
    const order = [...GROUPS.map(([name]) => name), "Other"];
    const groups = new Map(order.map((name) => [name, []]));
    for (const command of commands) groups.get(groupOf(command.name)).push(command);
    this.filter.hidden = false;
    this.body.replaceChildren(...[...groups].filter(([, list]) => list.length).map(([name, list]) =>
      h("details", { class: "hb-group" },
        h("summary", {}, name, h("span", { class: "hb-count" }, String(list.length))),
        list.map((command) => {
          const runner = new Runner({ name: command.name, hint: command.hint, glossary: command.glossary });
          return h("section", { class: "hb-command", "data-name": command.name, "data-text": `${command.name} ${command.hint} ${command.description}`.toLowerCase() },
            h("h3", {}, code(command.name), command.hint ? h("span", { class: "hb-hint" }, ` ${command.hint}`) : null),
            command.description ? h("p", { class: "hb-desc" }, command.description) : null,
            runner.el);
        }))));
  }

  #applyFilter() {
    const words = this.filter.value.toLowerCase().split(/\s+/).filter(Boolean);
    for (const group of this.body.querySelectorAll(".hb-group")) {
      let shown = 0;
      for (const command of group.querySelectorAll(".hb-command")) {
        const match = words.every((word) => command.dataset.text.includes(word));
        command.hidden = !match;
        shown += match;
      }
      group.hidden = !shown;
      group.open = words.length > 0 && shown > 0;
    }
  }
}

// Everything the board prints, and a line to type commands into.
class ConsolePane {
  constructor() {
    this.terminal = new Terminal({ label: "Console" });
    this.history = [];
    this.at = 0;
    this.input = h("input", {
      type: "text", class: "hb-input", spellcheck: "false", autocomplete: "off", autocapitalize: "off",
      placeholder: "Type a command, then Enter", "aria-label": "Console command",
      onkeydown: (event) => this.#key(event),
    });
    this.key = h("button", { type: "button", class: "md-button", onclick: () => session.sendKey(), title: "For a command that runs until a key is pressed, such as imu" }, "Send a key");
    this.clear = h("button", { type: "button", class: "md-button", onclick: () => this.terminal.clear() }, "Clear");
    this.note = h("p", { class: "hb-note", role: "status" });
    this.el = h("div", { class: "hb-console" },
      this.terminal.pre,
      h("div", { class: "hb-line" }, h("span", { class: "hb-prompt", "aria-hidden": "true" }, "holo>"), this.input, this.key, this.clear),
      this.note);
    session.addEventListener("text", (event) => this.terminal.write(event.detail));
    // The prompt before a command can belong to one of the page's own quiet queries, which the
    // console doesn't show: put one back, so the command reads as typed.
    session.addEventListener("busy", (event) => {
      if (event.detail && this.terminal.line === "") this.terminal.write("holo> ");
    });
    session.addEventListener("state", () => {
      this.input.disabled = !session.connected;
      if (session.state === "connected") this.terminal.write(`\n— connected: ${describe(session.board)} —\n`);
      if (session.state === "idle" && this.terminal.done) this.terminal.write("\n— disconnected —\n");
    });
    this.input.disabled = !session.connected;
  }

  #key(event) {
    if (event.key === "ArrowUp" || event.key === "ArrowDown") {
      event.preventDefault();
      this.at = Math.max(0, Math.min(this.history.length, this.at + (event.key === "ArrowUp" ? -1 : 1)));
      this.input.value = this.history[this.at] ?? "";
      return;
    }
    if (event.key !== "Enter") return;
    event.preventDefault();
    const line = this.input.value.trim();
    if (!line) return;
    this.history = [...this.history.filter((l) => l !== line), line].slice(-100);
    this.at = this.history.length;
    this.input.value = "";
    this.#run(line);
  }

  // The same checks as every Run button; the output is already in the terminal.
  async #run(line) {
    this.note.classList.remove("hb-error");
    this.note.replaceChildren();
    const problem = vet(line);
    if (problem) {
      this.note.classList.add("hb-error");
      return this.note.replaceChildren(...problem);
    }
    if (!confirmed(line)) return;
    try {
      await session.run(line);
      if (/^fs (rm|rmdir|mv|mkdir)\b/.test(line)) session.dispatchEvent(new Event("files"));
    } catch (error) {
      this.note.classList.add("hb-error");
      this.note.replaceChildren(...explain(error));
    }
  }
}

export function boardPage(el) {
  const status = new StatusBar();
  const files = new FileManager();
  const commands = new Commands();
  const consolePane = new ConsolePane();
  el.classList.remove("hb-static");
  el.replaceChildren(
    status.el,
    h("div", { class: "hb-panes" },
      h("section", { class: "hb-pane", "aria-labelledby": "hb-files-title" }, h("h2", { id: "hb-files-title" }, "Files"), files.el),
      h("section", { class: "hb-pane", "aria-labelledby": "hb-commands-title" }, h("h2", { id: "hb-commands-title" }, "Commands"), commands.el)),
    h("section", { class: "hb-pane hb-pane-console", "aria-labelledby": "hb-console-title" }, h("h2", { id: "hb-console-title" }, "Console"), consolePane.el));
}
