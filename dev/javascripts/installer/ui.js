// The browser installer on the Install page (manual/install/flashing.md).
//
// Loaded on every page through extra_javascript, and does nothing unless the page has a
// #holo-installer element. The flashing code (flash.js and the vendored esptool-js) is only
// imported on the Install page, and only when this copy of the site has firmware to install.
//
// The firmware comes from firmware/manifest.json at the root of this version of the site, which
// release.yml stages from the release's own images (tools/web_install_manifest.py). Builds with
// no firmware -- `dev`, pull-request previews, a local `make docs-serve` -- say so and point at
// the latest release's documentation instead.

import { assess, parsePartitionTable } from "./inspect.js";
import { md5Hex } from "./md5.js";

const BAUDS = [460800, 921600, 230400, 115200];
const BOOT_LOG_MS = 6000;

// --- DOM helpers: everything from the board or the manifest goes in as text, never HTML. ------

function h(tag, attrs = {}, ...children) {
  const el = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (value === false || value === null || value === undefined) continue;
    if (key === "class") el.className = value;
    else if (key.startsWith("on")) el.addEventListener(key.slice(2), value);
    else el.setAttribute(key, value === true ? "" : value);
  }
  for (const child of children.flat()) {
    if (child !== null && child !== undefined && child !== false) el.append(child);
  }
  return el;
}

const code = (text) => h("code", {}, text);

function siteRoot() {
  try {
    const base = JSON.parse(document.getElementById("__config").textContent).base;
    return new URL(base.endsWith("/") ? base : `${base}/`, location.href);
  } catch {
    return new URL("./", location.href);
  }
}

// --- the static notices, for when this copy of the page cannot install ----------------------

function notice(el, kind, manifest) {
  const root = siteRoot();
  const latest = new URL("../latest/install/flashing/", root);
  const messages = {
    "no-firmware": [
      h("strong", {}, "This copy of the documentation has no firmware to install. "),
      "Only a release's documentation includes firmware. Use the ",
      h("a", { href: latest.href }, "installer in the latest release's documentation"),
      ", or one of the other tabs.",
    ],
    unsupported: [
      h("strong", {}, "This browser can't talk to serial ports. "),
      "The installer needs Web Serial, which only desktop Chrome, Edge and Opera have. ",
      "Use one of those, or install with esptool (the next tab).",
    ],
    insecure: [
      h("strong", {}, "The browser only allows serial ports on secure pages. "),
      "Open this page over HTTPS, or from ", code("localhost"),
      ". For the offline copy, that means running ", code("serve.py"), ".",
    ],
  };
  el.replaceChildren(h("div", { class: "hi-notice" }, h("p", {}, messages[kind])));
  if (manifest) el.append(downloads(manifest));
}

// Links to the same images on the GitHub Release, for esptool. Following a link is not a
// cross-origin read, so these work where fetch() does not.
function downloads(manifest) {
  if (!manifest.release) return h("span");
  return h("p", { class: "hi-downloads" },
    "The images for ", code(manifest.version), " are also attached to its ",
    h("a", { href: manifest.release }, "GitHub Release"), ".");
}

// --- the installer ----------------------------------------------------------------------------

// Opened from disk, none of this runs: browsers won't load module scripts from file://. The page's
// own text in #holo-installer covers that case, pointing at serve.py.
async function start(el) {
  el.classList.remove("hi-notice");
  const manifestUrl = new URL("firmware/manifest.json", siteRoot());
  let manifest;
  try {
    const response = await fetch(manifestUrl, { cache: "no-cache" });
    if (!response.ok) throw new Error(response.status);
    manifest = await response.json();
  } catch {
    return notice(el, "no-firmware");
  }
  if (!("serial" in navigator)) return notice(el, "unsupported", manifest);
  if (!isSecureContext) return notice(el, "insecure", manifest);

  new Installer(el, manifest, manifestUrl);
}

class Installer {
  constructor(el, manifest, manifestUrl) {
    this.manifest = manifest;
    this.manifestUrl = manifestUrl;
    this.board = null;
    this.parts = null;
    this.assessment = null;
    // Loaded now rather than on the click: requestPort() must run soon after the user's gesture,
    // and waiting for 300 KB of flasher first could use up that allowance.
    this.flasher = import("./flash.js");
    this.flasher.catch(() => {});

    this.status = h("p", { class: "hi-status", role: "status", "aria-live": "polite" });
    this.baud = h("select", { id: "hi-baud" },
      BAUDS.map((baud, i) => h("option", { value: baud, selected: i === 0 }, `${baud}${i === 0 ? " (default)" : ""}`)));
    this.connectButton = h("button", { class: "md-button md-button--primary", type: "button", onclick: () => this.connect() },
      "Connect to the board");
    this.boardPanel = h("div", { class: "hi-board", hidden: true });
    this.progress = h("progress", { max: 1, value: 0 });
    this.progressLabel = h("span", { class: "hi-progress-label" });
    this.progressPanel = h("div", { class: "hi-progress", hidden: true }, this.progressLabel, this.progress);
    this.bootLog = h("pre", { class: "hi-bootlog", hidden: true });
    this.logText = h("pre", {});
    this.log = h("details", { class: "abstract hi-log" }, h("summary", {}, "Log"), this.logText);

    el.replaceChildren(h("div", { class: "hi" },
      h("p", { class: "hi-head" },
        h("strong", {}, "Holo Player ", manifest.version),
        manifest.release ? [" · ", h("a", { href: manifest.release }, "release notes")] : null),
      h("p", { class: "hi-connect" }, this.connectButton),
      this.status,
      this.boardPanel,
      this.progressPanel,
      this.bootLog,
      // The theme draws <details> as a collapsible note, which suits both of these.
      h("details", { class: "note hi-advanced" }, h("summary", {}, "Advanced"),
        h("p", {}, h("label", { for: "hi-baud" }, "Flash baud rate "), this.baud),
        h("p", {}, "Lower it if writing fails part way. Reading the board and its boot log always use 115200.")),
      this.log));
  }

  say(...content) {
    this.status.replaceChildren(...content);
    this.status.classList.remove("hi-error");
  }

  fail(...content) {
    this.say(...content);
    this.status.classList.add("hi-error");
  }

  appendLog(text) {
    this.logText.textContent += text.endsWith("\n") ? text : `${text}\n`;
    this.logText.scrollTop = this.logText.scrollHeight;
  }

  async connect() {
    const { Board, InstallError, loadFirmware } = await this.flasher;
    let port;
    try {
      port = await Board.choosePort();
    } catch (error) {
      if (error instanceof InstallError && error.kind === "cancelled") return;
      return this.explain(error);
    }

    this.connectButton.disabled = true;
    this.boardPanel.hidden = true;
    this.bootLog.hidden = true;
    this.progressPanel.hidden = true;
    const firmware = loadFirmware(this.manifest, this.manifestUrl);
    firmware.catch(() => {});   // reported below, after the board is closed
    try {
      this.say("Connecting…");
      this.board = new Board((line) => this.appendLog(line));
      const { flashSize } = await this.board.connect(port, Number(this.baud.value));
      if (flashSize && flashSize !== this.manifest.flash_size) {
        throw new InstallError("flash", flashSize);
      }
      this.say("Reading what is on the board…");
      const found = await this.board.inspect();
      this.parts = await firmware;
      const release = parsePartitionTable(this.parts.find((part) => part.role === "partition-table").data, md5Hex);
      this.assessment = assess(found, release, this.manifest.version);
      this.say("Connected. Choose what to do.");
      this.showBoard();
    } catch (error) {
      await this.board?.close();
      this.board = null;
      this.connectButton.disabled = false;
      this.explain(error);
    }
  }

  showBoard() {
    const a = this.assessment;
    const version = this.manifest.version;

    const found = [];
    if (a.blank) found.push(h("li", {}, "The flash is blank."));
    for (const app of a.apps) {
      if (!app.desc) continue;
      const name = app.desc.project === "holo-player-fw" ? "Holo Player" : app.desc.project;
      found.push(h("li", {}, h("strong", {}, name, " ", app.desc.version), ` in ${app.label}`,
        app.boot ? " (the one that boots)" : "", ` · built ${app.desc.date}`));
    }
    if (!a.blank && !found.length) found.push(h("li", {}, "Something that isn't an ESP-IDF application."));

    const blockedWhy = {
      blank: "Nothing to keep: the board is blank.",
      other: "The board isn't running Holo Player, so there is nothing of its to keep.",
      layout: "This release divides the flash up differently from the firmware on the board, so its settings and clips can't be kept.",
    }[a.updateBlocked];

    const updateNote = a.updateBlocked ? blockedWhy
      : a.downgrade ? `${a.holo.version} → ${version}. That is older than what is installed; settings saved by a newer version may not be understood.`
      : a.same ? `Reinstalls ${version}.`
      : `${a.holo.version} → ${version}.`;

    const eraseLabel = a.holo ? "Complete overwrite" : "Fresh install";
    const eraseNote = a.holo
      ? "Erases the whole flash, including every clip and setting, then installs. About 30 seconds longer."
      : a.foreign
        ? "Erases the whole flash, including the other firmware and anything it stored, then installs."
        : "Erases the whole flash, then installs.";

    this.confirm = h("input", { type: "checkbox", id: "hi-confirm", onchange: () => this.updateButton() });
    this.confirmRow = h("p", { class: "hi-confirm" }, this.confirm,
      h("label", { for: "hi-confirm" }, " I understand this deletes everything on the board."));

    const radio = (value, label, note, disabled) => h("label", { class: `hi-mode${disabled ? " hi-disabled" : ""}` },
      h("input", { type: "radio", name: "hi-mode", value, disabled, checked: value === a.defaultMode, onchange: () => this.updateButton() }),
      h("span", {}, h("strong", {}, label), h("br"), note));

    this.installButton = h("button", { class: "md-button md-button--primary", type: "button", onclick: () => this.install() }, "Install");
    const disconnect = h("button", { class: "md-button", type: "button", onclick: () => this.disconnect() }, "Disconnect");

    this.boardPanel.replaceChildren(
      h("p", {}, h("strong", {}, "On the board")),
      h("ul", {}, found),
      h("fieldset", { class: "hi-modes" }, h("legend", {}, `Install ${version}`),
        radio("update", "Update: keep settings and clips", updateNote, Boolean(a.updateBlocked)),
        radio("erase", eraseLabel, eraseNote, false)),
      this.confirmRow,
      h("p", { class: "hi-actions" }, this.installButton, " ", disconnect));
    this.boardPanel.hidden = false;
    this.updateButton();
  }

  mode() {
    return this.boardPanel.querySelector("input[name=hi-mode]:checked")?.value ?? "erase";
  }

  updateButton() {
    const erase = this.mode() === "erase";
    const needsConfirm = erase && this.assessment.confirmErase;
    this.confirmRow.hidden = !needsConfirm;
    this.installButton.disabled = needsConfirm && !this.confirm.checked;
  }

  async install() {
    const erase = this.mode() === "erase";
    const labels = {
      erase: "Erasing the flash…",
      bootloader: "Writing the bootloader…",
      "partition-table": "Writing the partition table…",
      otadata: "Writing the boot selection…",
      app: `Writing Holo Player ${this.manifest.version}…`,
      done: "Written and verified.",
    };
    for (const input of this.boardPanel.querySelectorAll("input, button")) input.disabled = true;
    this.progressPanel.hidden = false;
    this.say("Installing. Keep the board connected and this tab open.");
    try {
      await this.board.install(this.parts, erase, (phase, fraction) => {
        this.progressLabel.textContent = `${labels[phase] ?? phase} `;
        if (fraction === null) this.progress.removeAttribute("value");
        else this.progress.value = fraction;
      });
      this.say("Restarting the board…");
      this.bootLog.textContent = "";
      this.bootLog.hidden = false;
      await this.board.resetAndListen(BOOT_LOG_MS, (text) => {
        this.bootLog.textContent += text.replace(/\x1b\[[0-9;]*m/g, "");   // ESP-IDF's log colours
        this.bootLog.scrollTop = this.bootLog.scrollHeight;
      });
      this.board = null;
      this.finished();
    } catch (error) {
      await this.board?.close();
      this.board = null;
      this.explain(error, true);
    }
    this.boardPanel.hidden = true;
    this.connectButton.disabled = false;
  }

  finished() {
    const version = this.manifest.version;
    const running = /App version:\s*([^\s\x1b]+)/.exec(this.bootLog.textContent)?.[1];
    if (running === version) {
      this.say(h("strong", {}, `Installed. The board is running ${version}. `),
        "The port is free again: open a terminal on it at 115200 and type ", code("version"), ".");
    } else {
      this.say(h("strong", {}, "Installed and verified. "),
        running ? `The board reports ${running}, not ${version}. ` : "The board's boot messages weren't seen. ",
        "Open a terminal on the port at 115200 and type ", code("version"), " to check.");
    }
  }

  async disconnect() {
    await this.board?.close();
    this.board = null;
    this.boardPanel.hidden = true;
    this.connectButton.disabled = false;
    this.say("Disconnected.");
  }

  explain(error, writing = false) {
    const kind = error?.kind;
    const detail = String(error?.message ?? error);
    this.appendLog(`error: ${detail}`);
    const messages = {
      busy: ["The port is in use. Close ", code("idf.py monitor"), ", any serial terminal, or another tab using it, then connect again."],
      sync: ["The board didn't answer. Hold ", h("strong", {}, "BOOT"), ", tap ", h("strong", {}, "RESET"),
        ", let go of BOOT, then connect again. If that doesn't help, try another USB-C cable: some only carry power."],
      chip: [`That is an ${detail}, not an ESP32-S3. This firmware is for the Waveshare ESP32-S3-Touch-LCD-1.28.`],
      flash: [`The board's flash reports ${detail}. The Waveshare ESP32-S3-Touch-LCD-1.28 has ${this.manifest.flash_size}; this firmware won't fit another size.`],
      download: [`The firmware couldn't be downloaded (${detail}). Nothing was written.`],
      checksum: [`The downloaded ${detail} doesn't match its checksum. Nothing was written.`],
    };
    const lost = /device has been lost|disconnected|NetworkError/i.test(detail);
    if (messages[kind]) return this.fail(...messages[kind]);
    if (writing) {
      return this.fail(lost ? "The board was disconnected while writing. " : "Writing failed. ",
        "The board may not start until an install completes, so connect and install again, with a lower baud rate under Advanced if it fails again. The log below has details.");
    }
    this.fail(lost ? "The board was disconnected. " : "Something went wrong. ", "The log below has details.");
    this.log.open = true;
  }
}

const el = document.getElementById("holo-installer");
if (el) start(el);
