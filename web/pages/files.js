// Files: the board's storage volume -- browse it, upload clips and images to it, and move,
// copy, rename, download and delete what is there.

import { h, chip, notice, formatBytes, icon, askText, confirmAsk, modal, facts, store } from "../ui.js";

const ICON = "M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z";
const FILE = "M14 2H6c-1.1 0-1.99.9-1.99 2L4 20c0 1.1.89 2 1.99 2H18c1.1 0 2-.9 2-2V8l-6-6zm2 16H8v-2h8v2zm0-4H8v-2h8v2zm-3-5V3.5L18.5 9H13z";
const SHOWABLE = /\.(mov|gif|png|jpe?g)$/i;

const join = (dir, name) => (dir === "/" ? `/${name}` : `${dir}/${name}`);
const parentOf = (p) => p.replace(/\/[^/]+$/, "") || "/";

export default {
  id: "files",
  title: "Files",
  icon: ICON,
  feature: "files",
  primary: false,

  mount(el, ctx) {
    const alert = h("div");
    const volumeBox = h("div");
    const crumbs = h("nav", { class: "crumbs", "aria-label": "Folder" });
    const list = h("ul", { class: "rows" });
    const queueBox = h("div");
    const input = h("input", { type: "file", multiple: true, onchange: () => { enqueue([...input.files]); input.value = ""; } });
    const drop = h("label", { class: "drop" }, input, h("strong", {}, "Upload files"),
      h("span", { class: "muted" }, "Choose them, or drop them here: they go into this folder"));
    el.append(h("h1", {}, "Files"), alert, volumeBox,
      h("section", { class: "card" }, crumbs,
        h("div", { class: "actions", style: "margin-top:0;margin-bottom:12px" },
          h("button", { class: "button small", onclick: () => newFolder() }, "New folder"),
          h("button", { class: "button small", onclick: () => load().catch(fail) }, "Refresh")),
        drop, queueBox, h("div", { style: "margin-top:12px" }, list)));

    const fail = (e) => alert.replaceChildren(notice("bad", h("p", {}, e.message)));
    const say = (text) => alert.replaceChildren(notice("ok", h("p", {}, text)));
    let dir = store.get("holo.files.dir") ?? "/";

    drop.addEventListener("dragover", (e) => { e.preventDefault(); drop.classList.add("over"); });
    drop.addEventListener("dragleave", () => drop.classList.remove("over"));
    drop.addEventListener("drop", (e) => { e.preventDefault(); drop.classList.remove("over"); enqueue([...e.dataTransfer.files]); });

    async function load() {
      let listing;
      try {
        listing = await ctx.api.get(`/fs/list?${new URLSearchParams({ path: dir })}`);
      } catch (e) {
        if (dir !== "/" && e.status === 404) { go("/"); return; }
        throw e;
      }
      const vol = await ctx.api.get("/fs");
      volumeBox.replaceChildren(h("section", { class: "card", style: "margin-bottom:16px" },
        h("div", { class: "progress-line", style: "margin:0" }, h("span", {}, `${formatBytes(vol.used)} used`), h("span", {}, `${formatBytes(vol.free)} free of ${formatBytes(vol.total)}`)),
        h("div", { class: "meter" }, h("div", { style: `width:${vol.total ? (100 * vol.used / vol.total).toFixed(1) : 0}%` }))));

      const parts = dir.split("/").filter(Boolean);
      crumbs.replaceChildren(h("button", { onclick: () => go("/") }, "Storage"),
        ...parts.flatMap((p, i) => [h("span", {}, "/"), h("button", { onclick: () => go(`/${parts.slice(0, i + 1).join("/")}`) }, p)]));

      const rows = listing.entries.map((e) => h("li", { class: "row" },
        icon(e.type === "dir" ? ICON : FILE),
        e.type === "dir"
          ? h("button", { class: "link grow", onclick: () => go(e.path) }, h("span", { class: "name" }, e.name), h("span", { class: "sub" }, "folder"))
          : h("span", { class: "grow" }, h("span", { class: "name" }, e.name),
            h("span", { class: "sub" }, `${formatBytes(e.size)}${e.mtime ? ` · ${new Date(e.mtime).toLocaleString()}` : ""}`)),
        e.type === "file" && SHOWABLE.test(e.name) && ctx.info.features.includes("screen")
          ? h("button", { class: "button small", onclick: () => ctx.api.post("/screen/show", { path: e.path, ...(/\.(mov|gif)$/i.test(e.name) ? { loop: true } : {}) })
            .then(() => say(`Showing ${e.path}.`)).catch(fail) }, /\.(mov|gif)$/i.test(e.name) ? "Play" : "Show")
          : null,
        h("button", { class: "button small", "aria-label": `More for ${e.name}`, onclick: () => more(e) }, "…")));
      if (dir !== "/") rows.unshift(h("li", { class: "row" }, icon(ICON), h("button", { class: "link grow", onclick: () => go(parentOf(dir)) }, h("span", { class: "name" }, ".."), h("span", { class: "sub" }, "up a folder"))));
      list.replaceChildren(...(rows.length ? rows : [h("li", { class: "row" }, h("span", { class: "muted" }, "Empty."))]));
    }

    function go(path) {
      dir = path;
      store.set("holo.files.dir", dir);
      alert.replaceChildren();
      load().catch(fail);
    }

    async function newFolder() {
      const name = await askText("New folder", "Name", "", { ok: "Make it", maxlength: 63 });
      if (!name) return;
      try {
        await ctx.api.post("/fs/mkdir", { path: join(dir, name) });
        await load();
      } catch (e) { fail(e); }
    }

    // --- one entry's actions ------------------------------------------------------------------
    async function more(e) {
      const buttons = [["cancel", "Close"], ["move", "Rename or move"]];
      if (e.type === "file") buttons.push(["copy", "Copy"], ["download", "Download"], ["hash", "SHA-256"]);
      buttons.push(["delete", "Delete", "danger"]);
      const what = await modal(e.name, facts([
        ["Path", h("span", { class: "mono" }, e.path)],
        e.type === "file" ? ["Size", formatBytes(e.size)] : null,
        e.mtime ? ["Changed", new Date(e.mtime).toLocaleString()] : null,
      ]), buttons);
      alert.replaceChildren();
      try {
        if (what === "move") {
          const to = await askText(`Rename or move ${e.name}`, "New path, from the root of the storage", e.path, { ok: "Move", maxlength: 159 });
          if (to && to !== e.path) {
            await ctx.api.post("/fs/move", { from: e.path, to });
            say(`Moved to ${to}.`);
          }
        } else if (what === "copy") {
          const to = await askText(`Copy ${e.name}`, "The copy's path", e.path.replace(/(\.[^./]+)?$/, "-copy$1"), { ok: "Copy", maxlength: 159 });
          if (to) {
            await ctx.api.post("/fs/copy", { from: e.path, to });
            say(`Copied to ${to}.`);
          }
        } else if (what === "download") {
          const blob = await ctx.api.download(e.path);
          const a = h("a", { href: URL.createObjectURL(blob), download: e.name });
          document.body.append(a);
          a.click();
          setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
        } else if (what === "hash") {
          say("Hashing…");
          const r = await ctx.api.get(`/fs/entry?${new URLSearchParams({ path: e.path, sha256: "true" })}`);
          alert.replaceChildren(notice("", h("p", {}, `SHA-256 of ${e.name}:`), h("p", { class: "mono" }, r.sha256)));
        } else if (what === "delete") {
          const recursive = e.type === "dir";
          if (await confirmAsk(recursive ? `Delete ${e.path} and everything in it?` : `Delete ${e.path}?`, { ok: "Delete", danger: true })) {
            await ctx.api.del(`/fs/entry?${new URLSearchParams({ path: e.path, ...(recursive ? { recursive: "true" } : {}) })}`);
            say(`Deleted ${e.path}.`);
          }
        }
      } catch (err) { fail(err); }
      load().catch(fail);
    }

    // --- uploads, one at a time -------------------------------------------------------------
    const queue = [];
    let uploading = false;

    function enqueue(filesToAdd) {
      for (const file of filesToAdd) queue.push({ file, dir, loaded: 0, state: "waiting" });
      renderQueue();
      pump();
    }

    function renderQueue() {
      const shown = queue.filter((q) => q.state !== "done" || Date.now() - q.doneAt < 4000);
      queueBox.replaceChildren(...shown.map((q) => h("div", { style: "margin-top:12px" },
        h("div", { class: "progress-line", style: "margin:0" }, h("span", { class: "mono" }, join(q.dir, q.file.name)),
          h("span", {}, q.state === "sending" ? `${formatBytes(q.loaded)} of ${formatBytes(q.file.size)}` : q.state === "done" ? chip("uploaded", "ok")
            : q.state === "failed" ? chip("failed", "bad") : "waiting")),
        q.state === "sending" ? h("progress", { max: q.file.size || 1, value: q.loaded }) : null,
        q.error ? h("p", { class: "error" }, q.error) : null)));
    }

    async function pump() {
      if (uploading) return;
      uploading = true;
      for (let q; (q = queue.find((x) => x.state === "waiting")); ) {
        q.state = "sending";
        const path = join(q.dir, q.file.name);
        const send = (overwrite) => ctx.api.uploadFile(path, q.file, overwrite ? { overwrite: "true" } : {}, (loaded) => { q.loaded = loaded; renderQueue(); });
        try {
          try {
            await send(false);
          } catch (e) {
            if (e.code !== "exists" || !await confirmAsk(`${path} is already on the board. Replace it?`, { ok: "Replace" })) throw e;
            q.loaded = 0;
            await send(true);
          }
          q.state = "done";
          q.doneAt = Date.now();
        } catch (e) {
          q.state = "failed";
          q.error = e.message;
        }
        renderQueue();
        if (q.dir === dir) load().catch(fail);
      }
      uploading = false;
      setTimeout(renderQueue, 4100);
    }

    load().catch(fail);
    return null;
  },
};
