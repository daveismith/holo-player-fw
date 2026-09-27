// DOM helpers. Everything from the board goes in as text, never as HTML.

export function h(tag, attrs = {}, ...children) {
  const el = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (value === false || value === null || value === undefined) continue;
    if (key === "class") el.className = value;
    else if (key.startsWith("on")) el.addEventListener(key.slice(2), value);
    else el.setAttribute(key, value === true ? "" : value);
  }
  for (const child of children.flat(Infinity)) {
    if (child !== null && child !== undefined && child !== false) el.append(child);
  }
  return el;
}

// An inline SVG icon from a 24x24 path.
export function icon(path) {
  const ns = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(ns, "svg");
  svg.setAttribute("viewBox", "0 0 24 24");
  svg.setAttribute("aria-hidden", "true");
  const p = document.createElementNS(ns, "path");
  p.setAttribute("d", path);
  svg.append(p);
  return svg;
}

export const code = (text) => h("code", {}, text);
export const chip = (text, kind = "") => h("span", { class: `chip ${kind}` }, text);

export function facts(rows) {
  return h("dl", { class: "facts" }, rows.filter(Boolean).flatMap(([k, v]) => [h("dt", {}, k), h("dd", {}, v)]));
}

export function notice(kind, ...content) {
  return h("div", { class: `notice ${kind}`, role: kind === "bad" ? "alert" : "status" }, h("div", {}, ...content));
}

export function formatBytes(n) {
  if (n >= 1024 * 1024) return `${(n / 1024 / 1024).toFixed(2)} MB`;
  if (n >= 1024) return `${(n / 1024).toFixed(0)} KB`;
  return `${n} bytes`;
}

export function formatDuration(s) {
  s = Math.floor(s);
  const d = Math.floor(s / 86400), hr = Math.floor((s % 86400) / 3600), m = Math.floor((s % 3600) / 60);
  if (d) return `${d} d ${hr} h`;
  if (hr) return `${hr} h ${m} min`;
  if (m) return `${m} min ${s % 60} s`;
  return `${s} s`;
}

// sessionStorage can be missing or throw (private windows, blocked storage): never rely on it.
export const store = {
  get(key) { try { return sessionStorage.getItem(key); } catch { return null; } },
  set(key, value) { try { value === null ? sessionStorage.removeItem(key) : sessionStorage.setItem(key, value); } catch { /* not kept */ } },
};

// --- controls --------------------------------------------------------------------------------

let uid = 0;
export const newId = (prefix = "f") => `${prefix}${++uid}`;

// A labelled range. `onchange(value)` fires as it moves, at most every `every` ms, and once more
// where it stops -- the board keeps up with that, and the slider feels live.
export function slider({ label, value, min = 0, max = 100, step = 1, unit = "", every = 150, onchange }) {
  const id = newId("s");
  const out = h("output", { for: id }, `${value}${unit}`);
  const input = h("input", { type: "range", id, min, max, step, value });
  let last = 0;
  let timer = null;
  const send = () => { last = Date.now(); onchange(Number(input.value)); };
  input.addEventListener("input", () => {
    out.textContent = `${input.value}${unit}`;
    clearTimeout(timer);
    if (Date.now() - last >= every) send();
    else timer = setTimeout(send, every);
  });
  const el = h("div", { class: "field slider" }, h("label", { for: id }, label, out), input);
  el.set = (v) => { if (document.activeElement !== input) { input.value = v; out.textContent = `${v}${unit}`; } };
  return el;
}

// One of a few choices, as a row of buttons. `onchange(value)` on a click.
export function segmented(options, value, onchange, label = "") {
  const el = h("div", { class: "segmented", role: "radiogroup", "aria-label": label });
  const buttons = options.map(([v, text]) => h("button", {
    type: "button", role: "radio", "aria-checked": String(v === value), onclick: () => { el.set(v); onchange(v); },
  }, text));
  el.append(...buttons);
  el.set = (v) => buttons.forEach((b, i) => b.setAttribute("aria-checked", String(options[i][0] === v)));
  return el;
}

export function field(label, input, hint) {
  if (!input.id) input.id = newId();
  return h("div", { class: "field" }, h("label", { for: input.id }, label), input, hint ? h("p", { class: "muted hint" }, hint) : null);
}

export function toggle(label, checked, onchange) {
  const input = h("input", { type: "checkbox", role: "switch", checked, onchange: () => onchange(input.checked) });
  const el = h("label", { class: "toggle" }, input, h("span", {}, label));
  el.input = input;
  return el;
}

// --- dialogs ---------------------------------------------------------------------------------

// A modal with `content` and buttons; resolves to the value of the button pressed, or null.
export function modal(title, content, buttons) {
  const form = h("form", { method: "dialog" }, h("h2", {}, title), ...[content].flat(),
    h("div", { class: "actions" }, buttons.map(([value, text, kind]) => h("button", { value, class: `button ${kind ?? ""}` }, text))));
  const dialog = h("dialog", { class: "dialog" }, form);
  document.body.append(dialog);
  return new Promise((resolve) => {
    dialog.addEventListener("close", () => { resolve(dialog.returnValue || null); dialog.remove(); }, { once: true });
    dialog.showModal();
  });
}

// Are you sure? Resolves true or false.
export async function confirmAsk(message, { ok = "OK", danger = false, title = "Are you sure?" } = {}) {
  return (await modal(title, h("p", {}, message), [["cancel", "Cancel"], ["ok", ok, danger ? "danger" : "primary"]])) === "ok";
}

// One line of text; resolves to it, or null.
export async function askText(title, label, value = "", { ok = "OK", type = "text", minlength, maxlength } = {}) {
  const input = h("input", { type, value, required: true, minlength, maxlength, autocomplete: "off" });
  const answer = await modal(title, field(label, input), [["cancel", "Cancel"], ["ok", ok, "primary"]]);
  return answer === "ok" ? input.value : null;
}

// A sheet from the bottom (a phone) or a small dialog: `content`, closed by a tap outside.
export function sheet(content) {
  const dialog = h("dialog", { class: "sheet" }, ...[content].flat());
  dialog.addEventListener("click", (e) => { if (e.target === dialog) dialog.close(); });
  dialog.addEventListener("close", () => dialog.remove(), { once: true });
  document.body.append(dialog);
  dialog.showModal();
  return dialog;
}

export function colourOf(hex) {
  return /^#[0-9a-f]{6}$/i.test(hex ?? "") ? hex : "#ffffff";
}
