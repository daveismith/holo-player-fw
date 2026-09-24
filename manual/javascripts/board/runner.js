// A command line to edit and run, with its output underneath: the unit of the Board page's
// command list, and of every Run button in the documentation.

import { code, h, pageUrl } from "../common/dom.js";
import { quoteArg } from "./console.js";
import { danger, refusal } from "./commands.js";
import { hintVariants, placeholders } from "./parse.js";
import { session } from "./session.js";
import { Terminal } from "./terminal.js";

// Why something failed, in words, for a note beside the button.
export function explain(error) {
  const kind = error?.kind;
  const detail = String(error?.message ?? error ?? "");
  const messages = {
    busy: ["The port is in use. Close ", code("idf.py monitor"), ", any serial terminal, the installer, or another tab using the board, then try again."],
    "no-prompt": ["The board didn't answer at its console. Is it running Holo Player? A board in its bootloader, or a blank one, needs ",
      h("a", { href: pageUrl("install/flashing").href }, "installing"), " first. Pressing RESET on the board can help."],
    other: [`That board runs ${detail}, not Holo Player.`],
    lost: ["The board was disconnected."],
    "not-connected": ["Connect to the board first."],
    timeout: ["The board stopped answering. Try again, or disconnect and connect again."],
    fs: [detail],
    full: [detail.replace(/^fs: put: /, "The board has no room: ")],
    checksum: [`The file didn't arrive intact (${detail}). Try again.`],
    xmodem: [`The transfer failed: ${detail}.`],
    exists: [detail],
  };
  return messages[kind] ?? [`Something went wrong: ${detail}`];
}

// Every file on the board, a level of directories deep, for the file pickers. Asked for once a
// connection, and again after the file manager changes something.
let fileCache = null;
session.addEventListener("state", () => { fileCache = null; });
session.addEventListener("files", () => { fileCache = null; });

async function boardFiles() {
  if (!fileCache) {
    fileCache = (async () => {
      const top = await session.files.list();
      const all = top.filter((e) => !e.dir).map((e) => e.name);
      for (const dir of top.filter((e) => e.dir)) {
        for (const entry of await session.files.list(dir.name)) if (!entry.dir) all.push(`${dir.name}/${entry.name}`);
      }
      return all.sort();
    })();
    fileCache.catch(() => { fileCache = null; });
  }
  return fileCache;
}

// Why a line can't be run as it stands, as words for a note, or null. Transfers need the file
// manager, which speaks XMODEM; typed at the console they would leave the board waiting.
export function vet(line) {
  const refused = refusal(line);
  if (refused === "transfer") {
    return ["Files go to and from the board with the file manager on the ",
      h("a", { href: pageUrl("use/board").href }, "Board page"), ", which speaks XMODEM for you."];
  }
  if (refused === "ota") {
    return ["Updates go over serial with ", code("fs_xfer.py ota"), ": see ",
      h("a", { href: pageUrl("use/ota").href }, "Updating over serial"), "."];
  }
  const missing = placeholders(line)[0];
  if (missing) return ["Fill in ", code(missing.text), " first."];
  return null;
}

// Ask first when the line changes something on the board.
export function confirmed(line) {
  const warning = danger(line);
  return !warning || confirm(`${warning}\n\n${line}`);
}

const FILE_ARG = /<(file|path|from|to)>/;
const COLOUR_ARG = /<(c|name\|#RRGGBB[^>]*)>|\bcolour\b/;

export class Runner {
  // `name`: the command; `hint`: its hint, for the ways to call it and the helpers it gets; `line`:
  // what the input starts with.
  // `chips: false` leaves out the buttons for the ways to call it, where the page lists each way
  // as an entry of its own.
  constructor({ name, hint = "", line = name, glossary = "", compact = false, chips: showChips = true } = {}) {
    this.name = name;
    this.controller = null;

    this.input = h("input", {
      type: "text", class: "hb-input", value: line, spellcheck: "false", autocomplete: "off", autocapitalize: "off",
      "aria-label": `Command line for ${name}`,
      onkeydown: (event) => { if (event.key === "Enter") { event.preventDefault(); this.run(); } },
    });
    this.runButton = h("button", { type: "button", class: "md-button md-button--primary hb-run-button", onclick: () => this.run() }, "Run");
    this.stopButton = h("button", { type: "button", class: "md-button hb-stop", hidden: true, onclick: () => this.stop() }, "Stop");
    this.note = h("p", { class: "hb-note", role: "status", "aria-live": "polite" });
    this.terminal = new Terminal({ className: "hb-out", label: `Output of ${name}` });
    this.terminal.pre.hidden = true;

    const variants = hintVariants(hint);
    const chips = showChips && (variants.length > 1 || (variants.length === 1 && variants[0].template !== ""))
      ? h("div", { class: "hb-chips", role: "group", "aria-label": "Ways to call it" },
        variants.map((v) => h("button", {
          type: "button", class: "hb-chip", title: `${name} ${v.full}`,
          onclick: () => this.#fill(`${name} ${v.template}`),
        }, v.label)))
      : null;

    const helpers = [];
    // By the line's own syntax; where the whole command's hint is shown instead, by the commands
    // that take files at all.
    if (FILE_ARG.test(hint) || (showChips && ["video", "image", "fs"].includes(name))) {
      this.filePicker = h("select", { class: "hb-pick", "aria-label": "Insert a file from the board", onfocus: () => this.#loadFiles(), onchange: (e) => this.#insertFile(e) },
        h("option", { value: "" }, "Insert a file…"));
      helpers.push(this.filePicker);
    }
    if (COLOUR_ARG.test(hint)) {
      helpers.push(h("label", { class: "hb-colour" }, "Colour ",
        h("input", { type: "color", value: "#ff8000", onchange: (e) => this.#insert(e.target.value, /<(c|name\|[^>]*)>/) })));
    }

    this.el = h("div", { class: `hb-runner${compact ? " hb-compact" : ""}` },
      chips,
      h("div", { class: "hb-line" }, h("span", { class: "hb-prompt", "aria-hidden": "true" }, "holo>"), this.input, this.runButton, this.stopButton),
      helpers.length ? h("div", { class: "hb-helpers" }, helpers) : null,
      glossary ? h("details", { class: "hb-glossary" }, h("summary", {}, "Options"), h("pre", {}, glossary)) : null,
      this.note,
      this.terminal.pre);
  }

  say(...content) {
    this.note.replaceChildren(...content);
    this.note.classList.remove("hb-error");
  }

  fail(...content) {
    this.say(...content);
    this.note.classList.add("hb-error");
  }

  // Set the line, and select its first <placeholder> for typing over.
  #fill(line) {
    this.input.value = line;
    this.input.focus();
    const first = placeholders(line)[0];
    if (first) this.input.setSelectionRange(first.start, first.end);
    else this.input.setSelectionRange(line.length, line.length);
    this.say();
  }

  // Put `text` in place of the first placeholder matching `pattern`, else of the selection.
  #insert(text, pattern) {
    const line = this.input.value;
    const target = placeholders(line).find((p) => pattern.test(p.text));
    const [start, end] = target ? [target.start, target.end]
      : [this.input.selectionStart ?? line.length, this.input.selectionEnd ?? line.length];
    const before = line.slice(0, start);
    const pad = before && !/\s$/.test(before) ? " " : "";
    this.input.value = `${before}${pad}${text}${line.slice(end)}`;
    const caret = before.length + pad.length + text.length;
    this.input.focus();
    this.input.setSelectionRange(caret, caret);
  }

  async #loadFiles() {
    if (!session.connected || this.filePicker.options.length > 1) return;
    try {
      const files = await boardFiles();
      this.filePicker.append(...files.map((f) => h("option", { value: f }, f)));
      if (!files.length) this.filePicker.append(h("option", { value: "", disabled: true }, "(no files)"));
    } catch {
      // the console shows why
    }
  }

  #insertFile(event) {
    const file = event.target.value;
    event.target.value = "";
    if (file) this.#insert(quoteArg(file), FILE_ARG);
  }

  async run() {
    const line = this.input.value.trim();
    if (!line || this.controller) return;
    const problem = vet(line);
    if (problem) {
      const missing = placeholders(line)[0];
      if (missing) {
        this.input.focus();
        this.input.setSelectionRange(missing.start, missing.end);
      }
      return this.fail(...problem);
    }
    if (!confirmed(line)) return;

    if (!session.connected) {
      this.say("Connecting…");
      if (!(await session.connect())) return session.error ? this.fail(...explain(session.error)) : this.say();
    }

    this.controller = new AbortController();
    this.runButton.disabled = true;
    this.stopButton.hidden = false;
    this.terminal.clear();
    this.terminal.pre.hidden = false;
    this.terminal.write("holo> ");
    this.say(session.running ? `Waiting for ${session.running} to finish…` : "");
    try {
      const result = await session.run(line, { signal: this.controller.signal, onText: (t) => {
        if (this.note.textContent.startsWith("Waiting")) this.say();
        this.terminal.write(t);
      } });
      this.terminal.clear();
      this.terminal.write(`holo> ${line}\n${result.text}`);
      this.terminal.pre.classList.toggle("hb-failed", !result.ok);
      this.say(result.ok ? "" : "The board reported an error.");
      if (/^fs (rm|rmdir|mv|mkdir)\b/.test(line)) session.dispatchEvent(new Event("files"));
    } catch (error) {
      if (error.kind === "aborted") this.say("Stopped waiting. The console was reset to a fresh prompt.");
      else this.fail(...explain(error));
    } finally {
      this.controller = null;
      this.runButton.disabled = false;
      this.stopButton.hidden = true;
    }
  }

  // The first press sends a key, which ends a command that runs until one is pressed (imu); a
  // second gives up waiting.
  async stop() {
    if (!this.controller) return;
    if (this.stopButton.dataset.pressed) {
      this.controller.abort();
      return;
    }
    this.stopButton.dataset.pressed = "1";
    this.stopButton.textContent = "Stop waiting";
    await session.sendKey();
    setTimeout(() => {
      delete this.stopButton.dataset.pressed;
      this.stopButton.textContent = "Stop";
    }, 3000);
  }
}
