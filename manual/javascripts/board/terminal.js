// A scrollback of what the board printed. The console strips ANSI codes, so this treats \r as a
// terminal does -- back to the start of the line, overwriting from there -- which is all
// linenoise's redraws need once the codes are gone.

import { h } from "../common/dom.js";

const KEEP = 200000;   // characters of scrollback

export class Terminal {
  constructor({ className = "hb-term", label = "Board output" } = {}) {
    this.done = "";
    this.line = "";
    this.cursor = 0;
    this.pre = h("pre", { class: className, role: "log", "aria-label": label, tabindex: "0" });
  }

  write(text) {
    for (const part of text.split(/(\r\n|\n|\r)/)) {
      if (part === "\n" || part === "\r\n") {
        this.done += `${this.line}\n`;
        this.line = "";
        this.cursor = 0;
      } else if (part === "\r") {
        this.cursor = 0;
      } else if (part) {
        this.line = this.line.slice(0, this.cursor) + part + this.line.slice(this.cursor + part.length);
        this.cursor += part.length;
      }
    }
    if (this.done.length > KEEP) this.done = this.done.slice(this.done.indexOf("\n", this.done.length - KEEP) + 1);
    const atBottom = this.pre.scrollHeight - this.pre.scrollTop - this.pre.clientHeight < 24;
    this.pre.textContent = this.done + this.line;
    if (atBottom) this.pre.scrollTop = this.pre.scrollHeight;
  }

  clear() {
    this.done = "";
    this.line = "";
    this.cursor = 0;
    this.pre.textContent = "";
  }
}
