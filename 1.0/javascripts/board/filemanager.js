// The board's /data volume: browse it, and upload, download, rename, delete, play and show files.
// On the Board page, and embedded in "Files on the board" (a <div class="holo-files">).

import { code, h } from "../common/dom.js";
import { quoteArg } from "./console.js";
import { join, parent } from "./files.js";
import { explain } from "./runner.js";
import { session } from "./session.js";

const VIDEO = /\.(mov|mjpeg)$/i;
const DRAG_TYPE = "application/x-holo-path";   // a file or folder on the board, being moved
const IMAGE = /\.(png|jpe?g|gif)$/i;

export function formatSize(bytes) {
  if (bytes === null || bytes === undefined) return "";
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(bytes < 10240 ? 1 : 0)} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
}

function formatTime(seconds) {
  if (!Number.isFinite(seconds)) return "";
  const s = Math.max(0, Math.round(seconds));
  return s < 60 ? `${s} s` : `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;
}

function icon(entry) {
  if (entry.dir) return "📁";
  if (VIDEO.test(entry.name)) return "🎞";
  if (IMAGE.test(entry.name)) return "🖼";
  return "📄";
}

export class FileManager {
  constructor() {
    this.dir = "";
    this.entries = [];
    this.queue = [];           // uploads and downloads waiting their turn
    this.active = null;

    this.crumbs = h("nav", { class: "hb-crumbs", "aria-label": "Folder" });
    this.usage = h("span", { class: "hb-usage" });
    this.meter = h("meter", { class: "hb-meter", min: 0, max: 1, value: 0, hidden: true });
    this.list = h("div", { class: "hb-files", role: "table", "aria-label": "Files on the board" });
    this.picker = h("input", { type: "file", multiple: true, hidden: true, onchange: () => this.#picked() });
    this.note = h("p", { class: "hb-note", role: "status", "aria-live": "polite" });
    this.transfers = h("div", { class: "hb-transfers" });
    this.connectButton = h("button", { type: "button", class: "md-button md-button--primary", onclick: () => this.#connect() }, "Connect to the board");
    this.offline = h("div", { class: "hb-offline" }, h("p", {}, "Connect to see the files on the board."), this.connectButton);

    this.toolbar = h("div", { class: "hb-toolbar" },
      h("button", { type: "button", class: "md-button md-button--primary", onclick: () => this.picker.click() }, "Upload…"),
      h("button", { type: "button", class: "md-button", onclick: () => this.#mkdir() }, "New folder"),
      h("button", { type: "button", class: "md-button", onclick: () => this.refresh(), title: "Read the list again" }, "Refresh"));

    this.panel = h("div", { class: "hb-files-panel" },
      h("div", { class: "hb-files-head" }, this.crumbs, h("span", { class: "hb-usage-wrap" }, this.usage, this.meter)),
      this.list,
      h("p", { class: "hb-drop-hint" }, "Drop files on the list to upload them here, or on a folder to upload them there. Drag a file onto a folder, .., or a folder in the path above to move it."),
      this.toolbar,
      this.transfers,
      this.note);

    this.el = h("div", { class: "hb-fm" }, this.offline, this.panel, this.picker);

    // Drop files anywhere on the list to upload them into the folder shown.
    this.list.addEventListener("dragover", (event) => {
      if (!event.dataTransfer?.types.includes("Files") || event.dataTransfer.types.includes(DRAG_TYPE)) return;
      event.preventDefault();
      this.list.classList.add("hb-dragging");
    });
    this.list.addEventListener("dragleave", () => this.list.classList.remove("hb-dragging"));
    this.list.addEventListener("drop", (event) => {
      if (!event.dataTransfer?.types.includes("Files") || event.dataTransfer.types.includes(DRAG_TYPE)) return;
      event.preventDefault();
      this.list.classList.remove("hb-dragging");
      this.#upload([...event.dataTransfer.files]);
    });

    session.addEventListener("state", () => this.#state());
    session.addEventListener("files", () => { if (!this.active) this.refresh(); });
    this.#state();
  }

  say(...content) {
    this.note.replaceChildren(...content);
    this.note.classList.remove("hb-error");
  }

  fail(...content) {
    this.say(...content);
    this.note.classList.add("hb-error");
  }

  #state() {
    const connected = session.connected;
    this.offline.hidden = connected;
    this.panel.hidden = !connected;
    this.connectButton.disabled = session.state === "connecting";
    this.connectButton.textContent = session.state === "connecting" ? "Connecting…" : "Connect to the board";
    if (connected) this.refresh();
    else {
      this.dir = "";
      this.entries = [];
      if (session.error && session.state === "idle") this.offline.querySelector("p").replaceChildren(...explain(session.error));
    }
  }

  async #connect() {
    await session.connect();
  }

  async refresh() {
    if (!session.connected) return;
    try {
      const [entries, usage] = [await session.files.list(this.dir), await session.files.usage()];
      this.entries = entries;
      this.#render(usage);
    } catch (error) {
      if (this.dir && /No such file/.test(error.message)) {
        this.dir = "";
        return this.refresh();
      }
      this.fail(...explain(error));
    }
  }

  #render(usage) {
    const parts = this.dir ? this.dir.split("/") : [];
    const crumb = (label, dir) => this.#target(h("button", { type: "button", class: "hb-crumb", onclick: () => this.#open(dir) }, label), dir);
    this.crumbs.replaceChildren(crumb("/data", ""), ...parts.flatMap((part, i) => ["/", crumb(part, parts.slice(0, i + 1).join("/"))]));
    if (usage) {
      this.usage.textContent = `${formatSize(usage.usedKB * 1024)} of ${formatSize(usage.totalKB * 1024)} used`;
      this.meter.value = usage.usedKB / usage.totalKB;
      this.meter.hidden = false;
      this.free = usage.freeKB * 1024;
    }

    const rows = [];
    if (this.dir) {
      rows.push(this.#target(h("div", { class: "hb-row", role: "row", title: "Drop here to move up a folder" },
        h("span", { class: "hb-icon", "aria-hidden": "true" }, "↩"),
        h("button", { type: "button", class: "hb-name hb-link", onclick: () => this.#open(parent(this.dir)) }, ".."),
        h("span", { class: "hb-size" }), h("span", { class: "hb-actions" })), parent(this.dir)));
    }
    for (const entry of this.entries) {
      const path = join(this.dir, entry.name);
      const action = (label, title, fn, extra = "") => h("button", { type: "button", class: `hb-act ${extra}`, title, "aria-label": `${title} ${entry.name}`, onclick: fn }, label);
      const actions = [];
      if (VIDEO.test(entry.name)) actions.push(action("▶", "Play", () => this.#show(`video play ${quoteArg(path)}`)));
      if (IMAGE.test(entry.name)) actions.push(action("◉", "Show", () => this.#show(`image show ${quoteArg(path)}`)));
      if (!entry.dir) actions.push(action("↓", "Download", () => this.#download(path, entry.size)));
      actions.push(action("✎", "Rename or move", () => this.#rename(entry, path)));
      actions.push(action("✕", "Delete", () => this.#delete(entry, path), "hb-danger"));
      const row = h("div", {
        class: "hb-row", role: "row", draggable: "true",
        ondragstart: (event) => {
          event.dataTransfer.setData(DRAG_TYPE, path);
          event.dataTransfer.setData("text/plain", path);
          event.dataTransfer.effectAllowed = "move";
          row.classList.add("hb-dragged");
        },
        ondragend: () => row.classList.remove("hb-dragged"),
      },
        h("span", { class: "hb-icon", "aria-hidden": "true" }, icon(entry)),
        entry.dir
          ? h("button", { type: "button", class: "hb-name hb-link", onclick: () => this.#open(path) }, `${entry.name}/`)
          : h("span", { class: "hb-name" }, entry.name),
        h("span", { class: "hb-size" }, formatSize(entry.size)),
        h("span", { class: "hb-actions" }, actions));
      rows.push(entry.dir ? this.#target(row, path) : row);
    }
    if (!this.entries.length) rows.push(h("p", { class: "hb-empty" }, this.dir ? "This folder is empty." : "No files yet. Upload a clip or an image."));
    this.list.replaceChildren(...rows);
  }

  // Make `el` somewhere to drop: a file or folder dragged from the list moves into `dir`, and files
  // dragged from the computer upload there.
  #target(el, dir) {
    const accepts = (event) => event.dataTransfer?.types.includes(DRAG_TYPE) || event.dataTransfer?.types.includes("Files");
    el.addEventListener("dragover", (event) => {
      if (!accepts(event)) return;
      event.preventDefault();
      event.stopPropagation();
      event.dataTransfer.dropEffect = event.dataTransfer.types.includes(DRAG_TYPE) ? "move" : "copy";
      el.classList.add("hb-drop-target");
    });
    el.addEventListener("dragleave", () => el.classList.remove("hb-drop-target"));
    el.addEventListener("drop", (event) => {
      if (!accepts(event)) return;
      event.preventDefault();
      event.stopPropagation();
      el.classList.remove("hb-drop-target");
      this.list.classList.remove("hb-dragging");
      const from = event.dataTransfer.getData(DRAG_TYPE);
      if (from) this.#move(from, dir);
      else this.#upload([...event.dataTransfer.files], dir);
    });
    return el;
  }

  async #move(from, dir) {
    const name = from.split("/").pop();
    if (parent(from) === dir) return;   // dropped where it already is
    if (dir === from || dir.startsWith(`${from}/`)) return this.fail("A folder can't go inside itself.");
    const to = join(dir, name);
    if (!confirm(`Move ${from} to /data/${to}?`)) return;
    try {
      await session.files.mv(from, to);
      this.say(`Moved ${name} to /data/${dir ? `${dir}/` : ""}.`);
      session.dispatchEvent(new Event("files"));
    } catch (error) {
      this.fail(...explain(error));
    }
  }

  #open(dir) {
    this.dir = dir;
    this.say();
    this.refresh();
  }

  async #run(line) {
    const result = await session.run(line);
    if (!result.ok) throw Object.assign(new Error(result.text || `${line} failed`), { kind: "fs" });
    return result;
  }

  async #show(line) {
    try {
      const result = await this.#run(line);
      this.say(result.text.split("\n")[0] || "Done.");
    } catch (error) {
      this.fail(...explain(error));
    }
  }

  async #mkdir() {
    const name = prompt("New folder name");
    if (!name) return;
    try {
      await session.files.mkdir(join(this.dir, name.trim()));
      session.dispatchEvent(new Event("files"));
    } catch (error) {
      this.fail(...explain(error));
    }
  }

  async #rename(entry, path) {
    const name = prompt(`Rename ${entry.name} to (a path such as clips/${entry.name} moves it; drag it onto a folder to do the same)`, entry.name);
    if (!name || name === entry.name) return;
    try {
      await session.files.mv(path, name.includes("/") ? name : join(this.dir, name));
      session.dispatchEvent(new Event("files"));
    } catch (error) {
      this.fail(...explain(error));
    }
  }

  async #delete(entry, path) {
    const what = entry.dir ? `the folder ${entry.name} (it must be empty)` : entry.name;
    if (!confirm(`Delete ${what} from the board?`)) return;
    try {
      if (entry.dir) await session.files.rmdir(path);
      else await session.files.rm(path);
      session.dispatchEvent(new Event("files"));
    } catch (error) {
      this.fail(...explain(error));
    }
  }

  #picked() {
    this.#upload([...this.picker.files]);
    this.picker.value = "";
  }

  // Upload into `dir`. Only the folder shown is known well enough to ask before replacing a file;
  // elsewhere the board refuses to overwrite, and says so.
  #upload(files, dir = this.dir) {
    const existing = new Set(dir === this.dir ? this.entries.map((e) => e.name) : []);
    for (const file of files) {
      let force = false;
      if (existing.has(file.name)) {
        if (!confirm(`${file.name} is already on the board. Replace it?`)) continue;
        force = true;
      }
      this.#enqueue({ kind: "up", name: file.name, path: join(dir, file.name), file, force, size: file.size });
    }
  }

  #download(path, size) {
    this.#enqueue({ kind: "down", name: path.split("/").pop(), path, size });
  }

  // Transfers go one at a time -- they share the console -- each with a row of its own.
  #enqueue(job) {
    job.controller = new AbortController();
    job.progress = h("progress", { max: 1, value: 0 });
    job.label = h("span", { class: "hb-xfer-label" }, `${job.kind === "up" ? "↑" : "↓"} ${job.name}`);
    job.stats = h("span", { class: "hb-xfer-stats" }, "waiting");
    job.cancel = h("button", { type: "button", class: "hb-act", title: "Cancel", "aria-label": `Cancel ${job.name}`, onclick: () => this.#cancel(job) }, "✕");
    job.row = h("div", { class: "hb-xfer" }, job.label, job.progress, job.stats, job.cancel);
    this.transfers.append(job.row);
    this.queue.push(job);
    if (!this.active) this.#next();
  }

  #cancel(job) {
    job.controller.abort();
    if (job !== this.active) {
      this.queue = this.queue.filter((j) => j !== job);
      job.row.remove();
    }
  }

  async #next() {
    const job = this.queue.shift();
    this.active = job ?? null;
    this.transfers.classList.toggle("hb-busy", Boolean(job));
    if (!job) {
      session.dispatchEvent(new Event("files"));
      return;
    }
    this.say("Keep this tab in front while files move: browsers slow down background tabs.");
    const started = Date.now();
    const progress = (done, total) => {
      job.progress.value = total ? done / total : 1;
      const seconds = (Date.now() - started) / 1000;
      const rate = done / Math.max(seconds, 0.001);
      job.stats.textContent = `${Math.floor((done / Math.max(total, 1)) * 100)}% · ${formatSize(rate)}/s · ${formatTime((total - done) / rate)} left`;
    };
    job.stats.textContent = "starting";
    try {
      if (job.kind === "up") {
        if (this.free !== undefined && job.size > this.free && !job.force) {
          throw Object.assign(new Error(`${job.name} is ${formatSize(job.size)}, and the board has ${formatSize(this.free)} free.`), { kind: "fs" });
        }
        const data = new Uint8Array(await job.file.arrayBuffer());
        const result = await session.files.put(job.path, data, { force: job.force, progress, signal: job.controller.signal });
        job.stats.textContent = `done in ${formatTime(result.seconds)}, SHA-256 checked twice`;
      } else {
        const result = await session.files.get(job.path, { progress, signal: job.controller.signal });
        const url = URL.createObjectURL(new Blob([result.data]));
        h("a", { href: url, download: job.name }).click();
        setTimeout(() => URL.revokeObjectURL(url), 60000);
        job.stats.textContent = `done in ${formatTime((Date.now() - started) / 1000)}, SHA-256 checked`;
      }
      job.progress.value = 1;
      job.cancel.remove();
      this.say();
      setTimeout(() => job.row.remove(), 8000);
    } catch (error) {
      job.row.classList.add("hb-failed");
      job.cancel.remove();
      if (job.controller.signal.aborted) {
        job.stats.textContent = "cancelled";
        this.say(job.kind === "up" ? `Cancelled. Nothing was kept: the board only keeps a complete upload.` : "Cancelled.");
      } else {
        job.stats.textContent = "failed";
        this.fail(job.kind === "up" && error.kind === "exists" ? [code(job.name), " is already on the board."] : explain(error));
      }
      setTimeout(() => job.row.remove(), 15000);
    }
    this.#next();
  }
}
