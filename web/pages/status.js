// Status: what the board is running, how it is connected, and whether there is an update.

import { h, chip, facts, notice, formatBytes, formatDuration, code } from "../ui.js";

const ICON = "M11 7h2v2h-2zm0 4h2v6h-2zm1-9C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm0 18c-4.41 0-8-3.59-8-8s3.59-8 8-8 8 3.59 8 8-3.59 8-8 8z";

function signal(rssi) {
  if (rssi >= -55) return ["Excellent", "ok"];
  if (rssi >= -67) return ["Good", "ok"];
  if (rssi >= -75) return ["Fair", "warn"];
  return ["Weak", "bad"];
}

function firmwareCard(info, running) {
  const fw = info.firmware;
  const trial = running?.state === "trial";
  return h("section", { class: "card" },
    h("h2", {}, "Firmware", running ? chip(running.label) : null,
      trial ? chip("on trial", "warn") : running?.state === "confirmed" ? chip("confirmed", "ok") : null),
    h("p", { class: "slot" }, h("span", { class: "version" }, fw.version)),
    facts([
      ["Built", `${fw.date} ${fw.time}`],
      ["ESP-IDF", fw.idf],
      ["Chip", info.chip],
    ]),
    h("div", { class: "actions" }, h("a", { class: "button", href: "#/update" }, "Updates")));
}

function networkCard(info) {
  const rows = [];
  if (info.sta.connected) {
    const [label, kind] = signal(info.sta.rssi);
    rows.push(["Wi-Fi", info.sta.ssid]);
    rows.push(["Address", h("span", { class: "mono" }, info.sta.ip)]);
    rows.push(["Signal", h("span", {}, chip(label, kind), ` ${info.sta.rssi} dBm, channel ${info.sta.channel}`)]);
    if (info.sta.ipv6?.length) rows.push(["IPv6", h("span", { class: "mono" }, info.sta.ipv6.join("\n"))]);
  } else {
    rows.push(["Wi-Fi", info.sta.enabled ? "not connected" : "off"]);
  }
  rows.push(["Name", h("span", { class: "mono" }, `${info.hostname}.local`)]);
  rows.push(["Access point", info.ap.on ? `${info.ap.ssid}, ${info.ap.ip}, ${info.ap.clients} connected`
    + (info.ap.off_in_s ? `, off in ${Math.ceil(info.ap.off_in_s / 60)} min` : "") : "off"]);
  return h("section", { class: "card" }, h("h2", {}, "Network"), facts(rows),
    info.ap.on ? null : h("p", { class: "muted", style: "margin-top:12px" },
      "No network nearby? Press BOOT on the board, or run ", code("wifi ap on"), " on the console, to start its own access point."));
}

function nowCard(screen, leds, holo) {
  const showing = !screen ? null
    : screen.showing === "clip" ? `playing ${screen.path}${screen.loop ? ", looping" : ""}`
    : screen.showing === "image" ? `showing ${screen.path}`
    : screen.showing === "colour" ? `showing ${screen.colour}`
    : screen.showing === "calibration" ? "the alignment crosshair" : "off";
  return h("section", { class: "card" }, h("h2", {}, "Now"), facts([
    screen ? ["Screen", h("span", { class: "mono" }, showing)] : null,
    leds ? ["LEDs", leds.mode === "off" ? "off" : `${leds.mode}${leds.mode !== "rainbow" ? ` ${leds.colour}` : ""}${leds.loop ? ", looping" : ""}, ${leds.brightness}%`] : null,
    holo ? ["Holo", holo.ready ? (holo.motion === "hold" ? "still" : holo.motion) : `can't move: ${holo.why}`] : null,
  ]), h("div", { class: "actions" },
    screen ? h("a", { class: "button", href: "#/show" }, "Show") : null,
    holo ? h("a", { class: "button", href: "#/holo" }, "Holo") : null));
}

function systemCard(info) {
  return h("section", { class: "card" }, h("h2", {}, "System"), facts([
    ["Up for", formatDuration(info.uptime_s)],
    ["Free memory", formatBytes(info.heap_free)],
    ["Web password", info.auth ? "set" : "none"],
  ]));
}

export default {
  id: "status",
  title: "Status",
  icon: ICON,
  primary: true,
  feature: null,

  mount(el, ctx) {
    const alert = h("div");
    const banner = h("div");
    const grid = h("div", { class: "grid" });
    el.append(h("h1", {}, "Status"), alert, banner, grid);

    const has = (f) => ctx.info.features.includes(f);
    let slots = null;
    let screen = null;
    let leds = null;
    let holo = null;
    let checked = false;

    function render() {
      const info = ctx.info;
      const running = slots?.find((s) => s.running);
      grid.replaceChildren(...[screen || leds || holo ? nowCard(screen, leds, holo) : null,
        firmwareCard(info, running), networkCard(info), systemCard(info)].filter(Boolean));
    }

    async function refresh() {
      const info = await ctx.refreshInfo();
      alert.replaceChildren();
      if (has("ota")) {
        slots = (await ctx.api.get("/ota")).slots;
      }
      [screen, leds, holo] = await Promise.all([
        has("screen") ? ctx.api.get("/screen") : null,
        has("leds") ? ctx.api.get("/leds") : null,
        has("holo") ? ctx.api.get("/holo") : null,
      ]);
      render();

      // One look for a newer release per visit, when the board can reach the internet
      if (!checked && has("ota") && info.sta.connected) {
        checked = true;
        ctx.api.get("/ota/check?channel=latest").then((c) => {
          if (c.newer) {
            banner.replaceChildren(notice("ok", h("p", {}, `${c.version} is available (this is ${c.current}). `,
              h("a", { href: "#/update" }, "Update"))));
          }
        }).catch(() => { /* offline or no channel: say nothing */ });
      }
    }

    // What changes by itself arrives as it happens; the network and the slots are read again
    const on = {
      system: (s) => {
        Object.assign(ctx.info, { uptime_s: s.uptime_s, heap_free: s.heap_free });
        if (ctx.info.sta?.connected && s.rssi !== null) ctx.info.sta.rssi = s.rssi;
        render();
      },
      network: () => ctx.refreshInfo().then(render).catch(() => {}),
    };
    if (has("ota")) on.ota = () => ctx.api.get("/ota").then((o) => { slots = o.slots; render(); }).catch(() => {});
    if (has("screen")) on.screen = (s) => { screen = s; render(); };
    if (has("leds")) on.leds = (l) => { leds = l; render(); };
    if (has("holo")) on.holo = (x) => { holo = x; render(); };

    const lost = (e) => alert.replaceChildren(notice("bad", h("p", {}, `Lost touch with the board: ${e.message}`)));
    return ctx.events.follow({ on, refresh, fallback: 10000, onError: lost, onLost: lost });
  },
};
