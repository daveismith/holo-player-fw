// Update: the two firmware slots, the update in progress (whoever started it), the latest
// release, and uploading an image. Every step is one call to the API, so what this page does a
// script can do too: see /api/v1/openapi.json.

import { h, chip, facts, notice, formatBytes, code } from "../ui.js";
import { parseAppDesc, compareVersions } from "../lib/inspect.js";

const ICON = "M17 1.01L7 1c-1.1 0-2 .9-2 2v18c0 1.1.9 2 2 2h10c1.1 0 2-.9 2-2V3c0-1.1-.9-1.99-2-1.99zM17 19H7V5h10v14zm-1-6h-3V8h-2v5H8l4 4 4-4z";

// Where releases are published: the documentation site (components/webui Kconfig,
// WEBUI_RELEASES_URL, is the board's copy). Used only when this browser fetches a release itself.
const RELEASES = "https://davidiansmith.ca/holo-player-fw/";

// The first bytes of an image: header, first segment header, app descriptor.
const HEAD = 288;

const STATE_CHIPS = {
  confirmed: ["confirmed", "ok"],
  trial: ["on trial", "warn"],
  new: ["new, not yet run", ""],
  invalid: ["invalid", "bad"],
  rolled_back: ["rolled back", "bad"],
};

const ACTIVE_STATES = new Set(["receiving", "downloading", "verifying"]);

// --- the restart, and waiting for the board to come back ---------------------------------------

function overlay(...content) {
  const el = h("div", { class: "overlay", role: "alertdialog", "aria-live": "polite" }, h("div", { class: "card" }, ...content));
  document.body.append(el);
  return el;
}

async function waitForRestart(ctx, expect) {
  const viaAp = ctx.info?.via === "ap";
  const text = h("p", {}, expect ? `Restarting into ${expect}. This page reconnects by itself.` : "Restarting. This page reconnects by itself.");
  const extra = viaAp ? h("p", { class: "muted" }, "The board's access point is off after a restart, unless the board can't join a network: "
    + "then it comes back for 5 minutes. Otherwise press BOOT on the board, or run ", code("wifi ap on"), " on the console, then rejoin it.") : null;
  const box = overlay(h("div", { class: "spinner" }), h("h2", {}, "Restarting"), text, extra);
  const started = Date.now();
  await new Promise((r) => setTimeout(r, 3000));
  while (Date.now() - started < 120000) {
    try {
      const info = await ctx.api.get("/info");
      // Up again once its uptime is shorter than the time since we asked it to restart
      if (info.uptime_s * 1000 < Date.now() - started + 2000) {
        const ota = await ctx.api.get("/ota");
        const running = ota.slots.find((s) => s.running);
        const version = info.firmware.version;
        box.remove();
        await ctx.refreshInfo();
        return { version, slot: running?.label, state: running?.state, expected: !expect || version === expect };
      }
    } catch { /* still restarting */ }
    await new Promise((r) => setTimeout(r, 2000));
  }
  box.remove();
  return { lost: true };
}

function restartResult(r) {
  if (r.lost) {
    return notice("bad", h("p", {}, "The board has not come back after two minutes. It may have a different address now: ",
      code("wifi"), " on its console says which."));
  }
  if (!r.expected) {
    return notice("bad", h("p", {}, `It came back running ${r.version} from ${r.slot}: the new image did not start, and the board went back to the one before.`));
  }
  return notice("ok", h("p", {}, `Now running ${r.version} from ${r.slot}. `,
    r.state === "trial" ? "It is on trial, and confirms itself once it is up." : ""));
}

// --- the page ----------------------------------------------------------------------------------

export default {
  id: "update",
  title: "Update",
  icon: ICON,
  primary: false,
  feature: "ota",

  mount(el, ctx) {
    const alert = h("div");
    const result = h("div");
    const sessionBox = h("div");
    const slotsBox = h("div", { class: "grid" });
    const releaseBox = h("div");
    const uploadBox = h("div");
    el.append(h("h1", {}, "Update"), alert, result, sessionBox,
      h("h2", { style: "margin-top:8px" }, "Firmware slots"), slotsBox,
      h("div", { class: "grid", style: "margin-top:16px" }, releaseBox, uploadBox),
      h("p", { class: "muted", style: "margin-top:24px" }, "Every step here is an HTTP call a script can make: ",
        h("a", { href: "/api/v1/openapi.json" }, "the API description"), "."));

    let ota = null;
    let local = null;           // an upload from this page: { loaded, total }
    let busy = false;           // a request from this page is in flight
    let lostTouch = false;      // the alert is about polling, and goes once the board answers

    const fail = (e) => alert.replaceChildren(notice("bad", h("p", {}, e.message)));
    const current = () => ctx.info.firmware.version;
    const running = () => ota?.slots.find((s) => s.running);

    async function act(fn) {
      if (busy) return;
      busy = true;
      alert.replaceChildren();
      try { await fn(); } catch (e) { fail(e); } finally { busy = false; refresh().catch(fail); }
    }

    async function restartInto(expect, call) {
      await act(async () => {
        await call();
        result.replaceChildren(restartResult(await waitForRestart(ctx, expect)));
      });
    }

    // --- the session ---------------------------------------------------------------------------

    function sessionCard(s) {
      const app = s.app ? `${s.app.version}` : "the image";
      switch (s.state) {
        case "receiving":
        case "downloading": {
          const loaded = local ? local.loaded : s.bytes;
          const total = local ? local.total : s.total;
          return h("section", { class: "card" },
            h("h2", {}, s.state === "downloading" ? "Downloading" : "Receiving", chip(s.slot)),
            s.url ? h("p", { class: "mono muted" }, s.url) : null,
            h("progress", { max: total || 1, value: total ? loaded : 0 }),
            h("div", { class: "progress-line" },
              h("span", {}, total ? `${formatBytes(loaded)} of ${formatBytes(total)}` : formatBytes(loaded)),
              h("span", {}, s.app ? `${s.app.project} ${s.app.version}` : "")),
            h("div", { class: "actions" }, h("button", { class: "button danger", onclick: () => ctx.api.del("/ota/image").catch(fail) }, "Cancel")));
        }
        case "verifying":
          return h("section", { class: "card" }, h("h2", {}, "Checking the image", chip(s.slot)),
            h("p", {}, "The board is checking the whole image before it can be used."));
        case "staged":
          return h("section", { class: "card" },
            h("h2", {}, `${app} is ready`, chip(s.slot), chip("checked", "ok")),
            h("p", {}, `${s.app?.project ?? ""} ${s.app?.version ?? ""}, built ${s.app?.date ?? ""} ${s.app?.time ?? ""}. `,
              `${running()?.label} keeps running until the board restarts into it.`),
            h("p", { class: "mono muted" }, `sha256 ${s.sha256}`),
            h("div", { class: "actions" },
              h("button", { class: "button primary", onclick: () => restartInto(s.app?.version, () => ctx.api.post("/ota/activate", { reboot: true })) }, "Restart into it"),
              h("button", { class: "button", onclick: () => act(() => ctx.api.post("/ota/activate", { reboot: false })) }, "Use it at the next restart"),
              h("button", { class: "button danger", onclick: () => act(() => ctx.api.del("/ota/image")) }, "Discard")));
        case "active":
          return h("section", { class: "card" },
            h("h2", {}, `${app} boots next`, chip(s.slot)),
            h("p", {}, "It runs from the next restart."),
            h("div", { class: "actions" },
              h("button", { class: "button primary", onclick: () => restartInto(s.app?.version, () => ctx.api.post("/restart", {})) }, "Restart now"),
              running() && !running().boots
                ? h("button", { class: "button", onclick: () => act(() => ctx.api.post("/ota/activate", { slot: running().label, reboot: false })) }, `Keep ${current()}`)
                : null));
        case "failed":
          return notice("bad", h("p", {}, `The update failed: ${s.error}. ${running()?.label ?? "The running image"} still boots.`),
            h("p", {}, h("button", { class: "button", onclick: () => act(() => ctx.api.del("/ota/image")) }, "Dismiss")));
        default:
          return null;
      }
    }

    // --- the slots -----------------------------------------------------------------------------

    function slotCard(slot) {
      const inProgress = ACTIVE_STATES.has(ota.session.state);
      const chips = [];
      if (slot.running) chips.push(chip("running", "primary"));
      if (slot.boots && !slot.running) chips.push(chip("boots next", "warn"));
      if (slot.next && !slot.boots) chips.push(chip("next update"));
      const st = STATE_CHIPS[slot.state];
      if (st && slot.app) chips.push(chip(...st));

      const usable = slot.app && !["invalid", "rolled_back"].includes(slot.state);
      let action = null;
      if (usable && !slot.boots && !slot.running && !inProgress) {
        action = h("button", {
          class: "button",
          onclick: () => {
            if (!confirm(`Restart into ${slot.app.version} in ${slot.label}?`)) return;
            restartInto(slot.app.version, () => ctx.api.post("/ota/activate", { slot: slot.label, reboot: true }));
          },
        }, "Switch to this");
      } else if (slot.running && !slot.boots) {
        action = h("button", { class: "button", onclick: () => act(() => ctx.api.post("/ota/activate", { slot: slot.label, reboot: false })) }, "Keep this one");
      }
      return h("section", { class: `card slot${slot.running ? " running" : ""}` },
        h("h3", {}, slot.label, h("span", { class: "muted mono" }, `${formatBytes(slot.size)}`)),
        h("div", { class: "chips" }, chips),
        slot.app
          ? [h("p", { class: "version" }, slot.app.version), h("p", { class: "muted" }, `${slot.app.project}, built ${slot.app.date} ${slot.app.time}`)]
          : h("p", { class: "muted" }, "Empty"),
        action ? h("div", { class: "actions" }, action) : null);
    }

    // --- the latest release --------------------------------------------------------------------

    let check = null;           // the board's answer to /ota/check, or an Error
    let checking = false;

    async function runCheck(refresh = false) {
      checking = true;
      renderRelease();
      try {
        check = await ctx.api.get(`/ota/check?channel=latest${refresh ? "&refresh=1" : ""}`);
      } catch (e) {
        check = e;
      }
      checking = false;
      renderRelease();
    }

    function installButton(version, newer, onclick) {
      const label = newer === false && version === current() ? "Install it again" : "Download and install";
      return h("button", { class: `button${newer ? " primary" : ""}`, onclick }, label);
    }

    function pullLatest() {
      const c = check;
      if (c.newer === false && c.version !== current() && !confirm(`${c.version} is older than ${current()}. Install it anyway?`)) return;
      act(() => ctx.api.post("/ota/pull", { channel: "latest" }));
    }

    function renderRelease() {
      if (!ota?.pull.available || !ota.pull.channels) {
        releaseBox.replaceChildren();
        return;
      }
      const inProgress = ACTIVE_STATES.has(ota.session.state);
      const body = [];
      if (!ota.pull.online) {
        body.push(h("p", {}, "The board is not connected to the internet, so it can't fetch the release itself. If this device is, it can fetch it and send it on."),
          h("div", { class: "actions" }, h("button", { class: "button", disabled: inProgress, onclick: () => viaBrowser() }, "Fetch it here and send it")));
      } else if (checking || check === null) {
        body.push(h("p", { class: "muted" }, "Looking for the latest release…"));
      } else if (check instanceof Error) {
        body.push(notice("bad", h("p", {}, `Could not look: ${check.message}`)),
          h("div", { class: "actions" }, h("button", { class: "button", onclick: () => runCheck(true) }, "Try again")));
      } else {
        const c = check;
        const status = c.newer === true ? chip("newer", "ok") : c.version === c.current ? chip("installed") : c.newer === false ? chip("older", "warn") : null;
        body.push(
          h("p", { class: "version" }, c.version ?? "?", " ", status),
          facts([
            ["This board", c.current],
            ["Size", c.size ? formatBytes(c.size) : "unknown"],
            c.release_url ? ["Notes", h("a", { href: c.release_url, target: "_blank", rel: "noopener" }, "Release page")] : null,
          ]),
          h("div", { class: "actions" },
            inProgress ? null : installButton(c.version, c.newer, pullLatest),
            h("button", { class: "button", onclick: () => runCheck(true) }, "Check again")));
      }
      releaseBox.replaceChildren(h("section", { class: "card" }, h("h2", {}, "Latest release"), ...body));
    }

    // This browser fetches the release and sends it on: for a board on its own access point,
    // from a phone that still has mobile data. The board checks the manifest's SHA-256.
    async function viaBrowser() {
      await act(async () => {
        const fetchJson = async (url) => {
          const res = await fetch(url, { cache: "no-store" });
          if (!res.ok) throw new Error(`${res.status} for ${url}`);
          return res.json();
        };
        let manifest, blob;
        try {
          const versions = await fetchJson(`${RELEASES}versions.json`);
          const latest = versions.find((v) => v.aliases?.includes("latest"));
          if (!latest) throw new Error("no latest release listed");
          const base = `${RELEASES}${latest.version}/firmware/`;
          manifest = await fetchJson(`${base}manifest.json`);
          const app = manifest.parts.find((p) => p.role === "app");
          local = { loaded: 0, total: app.size };
          renderSession({ state: "downloading", slot: "this browser", url: `${base}${app.path}`, bytes: 0, total: app.size });
          const res = await fetch(`${base}${app.path}`);
          if (!res.ok) throw new Error(`${res.status} for ${app.path}`);
          const reader = res.body.getReader();
          const chunks = [];
          for (let got = 0; ;) {
            const { done, value } = await reader.read();
            if (done) break;
            chunks.push(value);
            got += value.length;
            local = { loaded: got, total: app.size };
            renderSession({ state: "downloading", slot: "this browser", url: `${base}${app.path}`, bytes: got, total: app.size });
          }
          blob = new Blob(chunks);
          manifest.app = app;
        } catch (e) {
          local = null;
          throw new Error(`This device couldn't reach the release site either (${e.message}). Download the .bin where there is internet, then upload it below.`);
        }
        await sendImage(blob, { sha256: manifest.app.sha256 }, manifest.version);
      });
    }

    // --- uploading a file ----------------------------------------------------------------------

    let picked = null;          // { file, desc, problems, force }

    async function pick(file) {
      alert.replaceChildren();
      const head = new Uint8Array(await file.slice(0, HEAD).arrayBuffer());
      const desc = parseAppDesc(head);
      const nextSlot = ota.slots.find((s) => s.next);
      const problems = [];
      const warnings = [];
      if (!desc) {
        problems.push("This is not a firmware image: an application .bin starts with 0xE9 and carries its description. (The -bootloader, -partition-table and -ota-data-initial files are not it.)");
      } else {
        if (desc.project !== ctx.info.firmware.project) {
          warnings.push(`It was built for ${desc.project}, not ${ctx.info.firmware.project}.`);
        }
        const order = compareVersions(desc.version, current());
        if (order === -1) warnings.push(`${desc.version} is older than ${current()}.`);
        if (order === 0 || desc.version === current()) warnings.push(`This board already runs ${desc.version}.`);
      }
      if (nextSlot && file.size > nextSlot.size) {
        problems.push(`At ${formatBytes(file.size)} it will not fit the ${formatBytes(nextSlot.size)} slot.`);
      }
      picked = { file, desc, problems, warnings, force: desc && desc.project !== ctx.info.firmware.project };
      uploadKey = null;
      renderUpload();
    }

    async function sendImage(blob, query, version) {
      local = { loaded: 0, total: blob.size };
      renderSession({ state: "receiving", slot: ota.slots.find((s) => s.next)?.label, bytes: 0, total: blob.size });
      try {
        await ctx.api.upload(blob, query, (loaded, total) => {
          local = { loaded, total };
          renderSession({ ...ota.session, state: "receiving", slot: ota.slots.find((s) => s.next)?.label, bytes: loaded, total });
        });
      } finally {
        local = null;
      }
      if (query.reboot) {
        result.replaceChildren(restartResult(await waitForRestart(ctx, version)));
      }
    }

    let uploadKey = null;       // what the upload card shows: re-rendered only when that changes,
                                // so an open file picker keeps the input it will report to

    function renderUpload() {
      const inProgress = ACTIVE_STATES.has(ota?.session.state) || local !== null;
      const key = `${picked ? picked.file.name + picked.file.lastModified : ""}|${inProgress}`;
      if (key === uploadKey) return;
      uploadKey = key;
      const input = h("input", { type: "file", accept: ".bin,application/octet-stream", onchange: (e) => e.target.files[0] && pick(e.target.files[0]) });
      const drop = h("label", { class: "drop" }, input,
        h("strong", {}, picked ? "Choose another file" : "Choose a firmware file"),
        h("span", { class: "muted" }, "holo-player-fw-vX.Y.Z.bin, or build/holo-player-fw.bin"));
      drop.addEventListener("dragover", (e) => { e.preventDefault(); drop.classList.add("over"); });
      drop.addEventListener("dragleave", () => drop.classList.remove("over"));
      drop.addEventListener("drop", (e) => {
        e.preventDefault();
        drop.classList.remove("over");
        if (e.dataTransfer.files[0]) pick(e.dataTransfer.files[0]);
      });

      const body = [drop];
      if (picked) {
        const { file, desc, problems, warnings } = picked;
        body.push(h("div", { style: "margin-top:12px" },
          facts([
            ["File", h("span", { class: "mono" }, file.name)],
            ["Size", formatBytes(file.size)],
            desc ? ["Firmware", `${desc.project} ${desc.version}`] : null,
            desc ? ["Built", `${desc.date} ${desc.time}`] : null,
          ])));
        for (const p of problems) body.push(notice("bad", h("p", {}, p)));
        for (const w of warnings) body.push(notice("warn", h("p", {}, w)));
        const restart = h("input", { type: "checkbox", id: "restart-after", checked: true });
        body.push(
          h("p", { style: "margin-top:12px" }, h("label", { for: "restart-after" }, restart, " Restart into it once the board has checked it")),
          h("div", { class: "actions" }, h("button", {
            class: "button primary",
            disabled: problems.length > 0 || inProgress,
            onclick: () => {
              const query = {};
              if (restart.checked) Object.assign(query, { activate: 1, reboot: 1 });
              if (picked.force) query.force = 1;
              const version = desc?.version;
              picked = null;
              uploadKey = null;
              act(() => sendImage(file, query, version));
            },
          }, "Upload")));
      }
      uploadBox.replaceChildren(h("section", { class: "card" }, h("h2", {}, "Upload a file"), ...body));
    }

    // --- refreshing ----------------------------------------------------------------------------

    function renderSession(s) {
      const card = sessionCard(s);
      sessionBox.replaceChildren(...(card ? [card] : []));
    }

    async function refresh() {
      ota = await ctx.api.get("/ota");
      if (!local) renderSession(ota.session);
      slotsBox.replaceChildren(...ota.slots.map(slotCard));
      renderRelease();
      renderUpload();
      if (check === null && !checking && ota.pull.available && ota.pull.channels && ota.pull.online) runCheck();
    }

    async function reread() {
      await refresh();
      if (lostTouch) {
        lostTouch = false;
        alert.replaceChildren();
      }
    }

    // The session's events come as its state changes, and each second while an image arrives
    return ctx.events.follow({
      on: { ota: () => reread().catch(() => {}) },
      refresh: reread,
      fallback: () => (local || ACTIVE_STATES.has(ota?.session.state) ? 1000 : 5000),
      onError: lost,
      onLost: lost,
    });

    function lost(e) {
      if (busy) return;       // restarting, or an action that reports its own error
      lostTouch = true;
      fail(new Error(`Lost touch with the board: ${e.message}`));
    }
  },
};
