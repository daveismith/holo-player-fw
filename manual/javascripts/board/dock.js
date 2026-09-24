// The board's connection in the header, on every page: whether this page is talking to the board,
// and Connect / Disconnect without going to the Board page.

import { h, pageUrl } from "../common/dom.js";
import { explain } from "./runner.js";
import { session } from "./session.js";

export function mountDock() {
  const header = document.querySelector(".md-header__inner");
  if (!header) return;

  const label = h("span", { class: "hb-dock-label" });
  const button = h("button", {
    type: "button", class: "hb-dock-button", "aria-haspopup": "true", "aria-expanded": "false",
    onclick: () => toggle(),
  }, h("span", { class: "hb-dot", "aria-hidden": "true" }), label);
  const text = h("p", { class: "hb-dock-text" });
  const error = h("p", { class: "hb-note hb-error", hidden: true });
  const connect = h("button", { type: "button", class: "md-button md-button--primary", onclick: () => session.connect() }, "Connect");
  const disconnect = h("button", { type: "button", class: "md-button", onclick: () => session.disconnect() }, "Disconnect");
  const page = h("a", { class: "hb-dock-link", href: pageUrl("use/board").href }, "Files, commands and console →");
  const panel = h("div", { class: "hb-dock-panel", hidden: true, role: "dialog", "aria-label": "Board connection" },
    text, error, h("p", { class: "hb-dock-buttons" }, connect, disconnect), h("p", {}, page));
  const dock = h("div", { class: "hb-dock" }, button, panel);

  const palette = header.querySelector("[data-md-component=palette]");
  if (palette) header.insertBefore(dock, palette);
  else header.append(dock);

  function toggle(open = panel.hidden) {
    panel.hidden = !open;
    button.setAttribute("aria-expanded", String(open));
  }

  document.addEventListener("click", (event) => { if (!dock.contains(event.target)) toggle(false); });
  document.addEventListener("keydown", (event) => { if (event.key === "Escape") toggle(false); });

  function update() {
    const state = session.state;
    dock.dataset.state = state;
    label.textContent = state === "connected" ? `Board ${session.board?.version ?? ""}`.trim()
      : state === "connecting" ? "Connecting…" : "Board";
    button.title = state === "connected" ? "Connected to the board" : "Not connected to a board";
    text.textContent = state === "connected"
      ? `Connected to Holo Player ${session.board?.version ?? ""}${session.board?.slot ? `, running from ${session.board.slot}` : ""}. The ▶ buttons beside commands on these pages run them on it.`
      : state === "connecting" ? "Connecting…"
      : "Connect a Holo Player board over USB to run the commands on these pages with their ▶ buttons.";
    connect.hidden = state === "connected";
    connect.disabled = state === "connecting";
    disconnect.hidden = state !== "connected";
    const failed = state === "idle" ? session.error : null;
    error.hidden = !failed;
    if (failed) error.replaceChildren(...explain(failed));
    page.hidden = Boolean(document.getElementById("holo-board"));
  }
  session.addEventListener("state", update);
  update();
}
