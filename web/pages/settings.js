// Settings: what the board starts with, and the web app's own settings -- its name, password,
// and the sites that may call it.

import { h, notice, field, slider, confirmAsk, code } from "../ui.js";

const ICON = "M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58c.18-.14.23-.41.12-.61l-1.92-3.32c-.12-.22-.37-.29-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54c-.04-.24-.24-.41-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58c-.18.14-.23.41-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z";

export default {
  id: "settings",
  title: "Settings",
  icon: ICON,
  feature: "settings",
  primary: false,

  mount(el, ctx) {
    const alert = h("div");
    const startBox = h("section", { class: "card" });
    const webBox = h("section", { class: "card" });
    el.append(h("h1", {}, "Settings"), alert, h("div", { class: "grid" }, startBox, webBox));
    const fail = (e) => alert.replaceChildren(notice("bad", h("p", {}, e.message)));
    const say = (text, kind = "ok") => alert.replaceChildren(notice(kind, h("p", {}, text)));

    // --- at start -------------------------------------------------------------------------------
    async function loadStart() {
      const [s, scenes] = await Promise.all([ctx.api.get("/settings"),
        ctx.info.features.includes("scenes") ? ctx.api.get("/scenes").then((r) => r.scenes) : []]);
      const patch = (body) => ctx.api.patch("/settings", body).then((r) => {
        if (r.restart_required) say("Saved. The LED count changes when the board next starts.", "warn");
        else say("Saved.");
      }).catch(fail);
      const boot = h("select", { onchange: () => patch({ boot_scene: boot.value || null }) },
        h("option", { value: "" }, "Nothing: screen and LEDs off"), scenes.map((sc) => h("option", { value: sc.name }, sc.name)));
      boot.value = s.boot_scene ?? "";
      const count = h("input", { type: "number", min: 1, max: 256, value: s.leds.count });
      startBox.replaceChildren(h("h2", {}, "When the board starts"),
        field("Scene", boot, scenes.length ? null : "Save a scene on the Scenes page to start with it."),
        slider({ label: "Backlight", value: s.screen.backlight, min: 1, unit: "%", every: 400, onchange: (v) => patch({ screen: { backlight: v } }) }),
        slider({ label: "LED brightness", value: s.leds.brightness, min: 1, unit: "%", every: 400, onchange: (v) => patch({ leds: { brightness: v } }) }),
        h("p", { class: "muted hint" }, "Keep the LEDs at 30% or below when the strip is powered from the board."),
        field("LEDs on the strip", count, "Takes effect when the board next starts."),
        h("div", { class: "actions" },
          h("button", { class: "button", onclick: () => patch({ leds: { count: Math.max(1, Math.min(256, Number(count.value) || 1)) } }) }, "Save the count"),
          h("button", { class: "button danger", onclick: async () => {
            if (!await confirmAsk("Put these back to the firmware's defaults? Scenes, servo calibration and networks are kept.", { ok: "Reset" })) return;
            ctx.api.del("/settings").then(() => { say("Back to the defaults."); return loadStart(); }).catch(fail);
          } }, "Reset")));
    }

    // --- the web app -----------------------------------------------------------------------------
    async function loadWeb() {
      const w = await ctx.api.get("/web");
      const hostname = h("input", { type: "text", value: w.hostname, maxlength: 32, autocomplete: "off" });
      const pw = h("input", { type: "password", minlength: 4, maxlength: 64, autocomplete: "new-password" });
      const cors = h("textarea", { rows: 4, spellcheck: false }, w.cors.join("\n"));
      const save = (body, text) => ctx.api.patch("/web", body).then(async () => {
        if (typeof body.password === "string") ctx.api.usePassword(body.password);
        if (body.password === null) ctx.api.forgetPassword();
        say(text);
        await ctx.refreshInfo();
        await loadWeb();
      }).catch(fail);
      webBox.replaceChildren(h("h2", {}, "The web app"),
        field("Name on the network", hostname, `The board answers to ${w.hostname}.local.`),
        h("div", { class: "actions", style: "margin-top:0" },
          h("button", { class: "button", onclick: async () => {
            const name = hostname.value.trim();
            if (!await confirmAsk(`Rename the board to ${name}? This page may need to be reopened at http://${name}.local/.`, { ok: "Rename" })) return;
            save({ hostname: name }, `Renamed: ${name}.local.`);
          } }, "Rename"),
          h("button", { class: "button", onclick: () => save({ hostname: null }, "The default name is back.") }, "Default name")),
        h("h3", { style: "margin-top:16px" }, "Password"),
        h("p", { class: "muted hint" }, w.auth ? "Set: changes, downloads and these settings need it." : "None: anyone on the network can change the board."),
        field(w.auth ? "New password" : "Password", pw, "4 to 64 characters."),
        h("div", { class: "actions", style: "margin-top:0" },
          h("button", { class: "button primary", onclick: () => {
            if (pw.value.length < 4) return fail(new Error("A password is at least 4 characters."));
            save({ password: pw.value }, "Password set.");
          } }, w.auth ? "Change it" : "Set it"),
          w.auth ? h("button", { class: "button danger", onclick: async () => {
            if (await confirmAsk("Remove the password? Anyone on the network could then change the board.", { ok: "Remove", danger: true })) {
              save({ password: null }, "Password removed.");
            }
          } }, "Remove it") : null),
        h("h3", { style: "margin-top:16px" }, "Sites that may call the board"),
        h("p", { class: "muted hint" }, "One a line: ", code("https://example.com"), ", or ", code("https://*.example.com"),
          " for its subdomains. Their pages can use the board's API from a browser."),
        cors,
        h("div", { class: "actions" },
          h("button", { class: "button", onclick: () => save({ cors: cors.value.split(/\s+/).filter(Boolean) }, "Saved the list.") }, "Save the list"),
          h("button", { class: "button", onclick: () => save({ cors: null }, "The default list is back.") }, "Default list")));
    }

    loadStart().catch(fail);
    loadWeb().catch((e) => {
      webBox.replaceChildren(h("h2", {}, "The web app"), notice(e.status === 401 ? "warn" : "bad",
        h("p", {}, e.status === 401 ? "These settings need the board's password." : e.message)),
      h("div", { class: "actions" }, h("button", { class: "button", onclick: () => loadWeb().catch(fail) }, "Try again")));
    });
    return null;
  },
};
