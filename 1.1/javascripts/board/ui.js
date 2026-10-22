// The board pages, loaded on every page through extra_javascript:
//
//   * a dock in the header with the board's connection, which follows the user between pages;
//   * a ▶ beside each board command in the documentation, to run it there and then;
//   * the Board page (manual/use/board.md, #holo-board): files, every command, and the console;
//   * the file manager wherever a page has a <div class="holo-files">.
//
// All of it needs Web Serial (desktop Chrome, Edge or Opera) on a secure page. Elsewhere the
// documentation reads as it always has, and the Board page says why it can't work.

import { code, h } from "../common/dom.js";
import { BoardPort } from "./serial.js";
import { session } from "./session.js";
import { mountDock } from "./dock.js";
import { decorate } from "./inline.js";
import { boardPage } from "./page.js";
import { FileManager } from "./filemanager.js";

function notice(el) {
  const insecure = "serial" in navigator && !isSecureContext;
  el.classList.add("hb-notice");
  el.replaceChildren(h("p", {}, insecure
    ? [h("strong", {}, "The browser only allows serial ports on secure pages. "), "Open this page over HTTPS, or from ",
      code("localhost"), ". For the offline copy, that means running ", code("serve.py"), "."]
    : [h("strong", {}, "This browser can't talk to serial ports. "),
      "Talking to the board from these pages needs Web Serial, which only desktop Chrome, Edge and Opera have. ",
      "Every command also works in a serial terminal at 115200 baud, and files move with ", code("fs_xfer.py"), "."]));
}

const boardEl = document.getElementById("holo-board");
const fileEls = document.querySelectorAll(".holo-files");

if (BoardPort.supported()) {
  mountDock();
  decorate();
  if (boardEl) boardPage(boardEl);
  for (const el of fileEls) {
    el.classList.remove("hb-static");
    el.replaceChildren(new FileManager().el);
  }
  // The installer needs the port to itself: don't take it back on the Install page.
  if (!document.getElementById("holo-installer")) session.resume();
} else {
  if (boardEl) notice(boardEl);
  for (const el of fileEls) notice(el);
}
