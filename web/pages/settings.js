// Settings: what the board starts with, the web app's own settings -- its name, password, and
// the sites that may call it -- and the host link: a controller on a wire.

import { h, notice, field, slider, confirmAsk, code, facts, chip } from "../ui.js";

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
    const linkBox = h("section", { class: "card" });
    const hasLink = ctx.info.features.includes("link");
    el.append(h("h1", {}, "Settings"), alert, h("div", { class: "grid" }, startBox, webBox, hasLink ? linkBox : null));
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

    // --- the host link -------------------------------------------------------------------------
    async function loadLink() {
      const l = await ctx.api.get("/link");
      const s = l.settings;
      const mode = h("select", {}, [["off", "Off"], ["uart", "UART"], ["rs485", "RS485"]].map(([v, t]) => h("option", { value: v }, t)));
      mode.value = s.mode;
      const protocol = h("select", {}, [["auto", "JSON lines or native frames"], ["json", "JSON lines"], ["native", "Native frames"]]
        .map(([v, t]) => h("option", { value: v }, t)));
      protocol.value = s.protocol;
      const baud = h("select", {}, [9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600].map((b) => h("option", { value: b }, String(b))));
      baud.value = String(s.baud);
      const address = h("input", { type: "number", min: 1, max: 223, value: s.address });
      const groups = h("input", { type: "text", value: s.groups.join(" "), placeholder: "none", autocomplete: "off" });
      const delay = h("input", { type: "number", min: 0, max: 50, value: s.reply_delay_ms });
      const running = l.running.mode === "off" ? "off"
        : `${l.running.mode.toUpperCase()} on GPIO${l.running.pins.a} and GPIO${l.running.pins.b}${l.running.pins.c != null ? `, GPIO${l.running.pins.c}` : ""}`;
      const c = l.counters;
      linkBox.replaceChildren(h("h2", {}, "Host link", l.restart_required ? chip("restart to apply", "warn") : null),
        h("p", { class: "muted hint" }, "A controller on a wire, on P2's spare pins: see ",
          h("a", { href: "https://davidiansmith.ca/holo-player-fw/latest/use/host-link/", target: "_blank" }, "the host link"), "."),
        facts([
          ["Now", running],
          ["Requests", `${c.requests}${l.last_request_s != null ? `, the last ${Math.round(l.last_request_s)} s ago` : ""}`],
          c.bad_lines || c.framing_errors || c.overruns ? ["Trouble", `${c.bad_lines} bad lines, ${c.framing_errors} framing errors, ${c.overruns} overruns`] : null,
          ["Events", l.events.kinds.length ? `${l.events.kinds.join(", ")} (${l.events.push ? "pushed" : "kept"})` : "none asked for"],
        ].filter(Boolean)),
        h("div", { class: "fields", style: "margin-top:12px" },
          field("Transport", mode, "From the next restart"), field("Speaks", protocol),
          field("Baud rate", baud), field("Address", address, "1-223"),
          field("Groups", groups, "RS485: up to 8 of 1-31"), field("Reply delay (ms)", delay, "RS485: 0-50")),
        h("div", { class: "actions" },
          h("button", { class: "button primary", onclick: () => {
            const body = {
              mode: mode.value, protocol: protocol.value, baud: Number(baud.value), address: Number(address.value),
              groups: groups.value.split(/[\s,]+/).filter(Boolean).map(Number), reply_delay_ms: Number(delay.value),
            };
            ctx.api.patch("/link", body).then((r) => {
              say(r.restart_required ? "Saved. The transport changes when the board next restarts." : "Saved.", r.restart_required ? "warn" : "ok");
              return loadLink();
            }).catch(fail);
          } }, "Save")));
    }

    loadStart().catch(fail);
    if (hasLink) loadLink().catch(fail);
    loadWeb().catch((e) => {
      webBox.replaceChildren(h("h2", {}, "The web app"), notice(e.status === 401 ? "warn" : "bad",
        h("p", {}, e.status === 401 ? "These settings need the board's password." : e.message)),
      h("div", { class: "actions" }, h("button", { class: "button", onclick: () => loadWeb().catch(fail) }, "Try again")));
    });
    return null;
  },
};
