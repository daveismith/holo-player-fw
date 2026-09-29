// Run buttons in the documentation itself: beside each board command in a code block, and beside
// the commands in reference tables. Each opens a small runner under what it belongs to.

import { h } from "../common/dom.js";
import { commandLine, isCommand, words } from "./commands.js";
import { placeholders } from "./parse.js";
import { Runner } from "./runner.js";

function panel(runner, onClose) {
  const close = h("button", { type: "button", class: "hb-act", title: "Close", "aria-label": "Close", onclick: onClose }, "✕");
  return h("div", { class: "hb-inline" }, h("p", { class: "hb-inline-head" }, h("span", {}, "On the board"), close), runner.el);
}

// Code blocks with no highlighting -- the board's commands are written in bare fences, and shell
// commands for the host in `sh` fences, which are highlighted -- get a ▶ on each line that starts
// with a board command.
function codeBlocks(root) {
  for (const block of root.querySelectorAll(".md-typeset .highlight")) {
    const codeEl = block.querySelector("pre > code");
    if (!codeEl || codeEl.children.length) continue;
    const lines = codeEl.textContent.replace(/\n$/, "").split("\n");
    if (!lines.some((line) => isCommand(line))) continue;

    let open = null;
    const show = (line) => {
      open?.el.remove();
      const runner = new Runner({ name: words(line)[0], line, compact: true });
      open = { el: panel(runner, () => { open.el.remove(); open = null; }) };
      block.after(open.el);
      if (placeholders(line).length) runner.input.focus();
      else runner.run();
    };

    codeEl.replaceChildren(...lines.flatMap((line, i) => {
      const parts = [];
      if (isCommand(line)) {
        const cmd = commandLine(line);
        parts.push(h("button", {
          type: "button", class: "hb-run-line", title: `Run on the board: ${cmd}`, "aria-label": `Run on the board: ${cmd}`,
          onclick: () => show(cmd),
        }));
      }
      parts.push(line);
      if (i < lines.length - 1) parts.push("\n");
      return parts;
    }));
    block.classList.add("hb-runnable");
  }
}

// Reference tables: a ▶ after a command in a row's first cell, opening a runner with the ways to
// call it (from the cell's own syntax) in a row of its own underneath.
function tables(root) {
  for (const cell of root.querySelectorAll(".md-typeset table td:first-child")) {
    const codeEl = cell.querySelector("code");
    if (!codeEl || !isCommand(codeEl.textContent)) continue;
    const syntax = codeEl.textContent.replace(/\s*\\?\|\s*/g, " | ").trim();
    const [name] = words(syntax);
    const hint = syntax.slice(name.length).trim();
    const complete = !/[<[|…]/.test(hint);
    const row = cell.parentElement;
    let open = null;
    cell.append(h("button", {
      type: "button", class: "hb-run-cell", title: `Try ${name} on the board`, "aria-label": `Try ${name} on the board`,
      onclick: () => {
        if (open) {
          open.remove();
          open = null;
          return;
        }
        const runner = new Runner({ name, hint, line: complete ? syntax : name, compact: true });
        open = h("tr", { class: "hb-inline-row" }, h("td", { colspan: String(row.children.length) }, panel(runner, () => { open.remove(); open = null; })));
        row.after(open);
        if (complete) runner.run();
        else runner.input.focus();
      },
    }));
  }
}

export function decorate(root = document) {
  codeBlocks(root);
  tables(root);
}
