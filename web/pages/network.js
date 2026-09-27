// Network: the Wi-Fi network the board is on, the ones it knows, what is in range, and its own
// access point. Several of these can cut this page off from the board, and say so first.

import { h, chip, notice, facts, field, confirmAsk, askText, toggle } from "../ui.js";

const ICON = "M1 9l2 2c4.97-4.97 13.03-4.97 18 0l2-2C16.93 2.93 7.08 2.93 1 9zm8 8l3 3 3-3c-1.65-1.66-4.34-1.66-6 0zm-4-4l2 2c2.76-2.76 7.24-2.76 10 0l2-2C15.14 9.14 8.87 9.14 5 13z";

function bars(rssi) {
  return rssi >= -55 ? "▂▄▆█" : rssi >= -67 ? "▂▄▆" : rssi >= -75 ? "▂▄" : "▂";
}

export default {
  id: "network",
  title: "Network",
  icon: ICON,
  feature: "network",
  primary: false,

  mount(el, ctx) {
    const alert = h("div");
    const linkBox = h("section", { class: "card" });
    const knownBox = h("section", { class: "card" });
    const apBox = h("section", { class: "card" });
    const scanBox = h("section", { class: "card" });
    el.append(h("h1", {}, "Network"), alert, h("div", { class: "grid" }, linkBox, apBox), h("div", { class: "grid", style: "margin-top:16px" }, knownBox, scanBox));
    const fail = (e) => alert.replaceChildren(notice("bad", h("p", {}, e.message)));
    const say = (text, kind = "ok") => alert.replaceChildren(notice(kind, h("p", {}, text)));
    const viaAp = () => ctx.info.via === "ap";
    let net = null;

    async function load() {
      net = await ctx.api.get("/network");
      render();
    }

    async function join(ssid, passphrase) {
      const warn = viaAp() ? "" : ` This page reaches the board over ${net.sta.ssid ?? "its network"}, and may lose it: reopen it on ${ssid}, at http://${ctx.info.hostname}.local/.`;
      if (!await confirmAsk(`Join ${ssid}?${warn}`, { ok: "Join" })) return;
      try {
        await ctx.api.post("/network/join", passphrase ? { ssid, passphrase } : { ssid });
        say(`Joining ${ssid}…`);
        setTimeout(() => load().catch(() => {}), 8000);
      } catch (e) { fail(e); }
    }

    function render() {
      const sta = net.sta;
      linkBox.replaceChildren(h("h2", {}, "Wi-Fi", sta.connected ? chip("connected", "ok") : chip(sta.enabled ? "not connected" : "off", "warn")),
        facts([
          sta.connected ? ["Network", sta.ssid] : null,
          sta.connected ? ["Address", h("span", { class: "mono" }, sta.ip)] : null,
          sta.connected ? ["Signal", `${bars(sta.rssi)} ${sta.rssi} dBm, channel ${sta.channel}`] : null,
          ["Name", h("span", { class: "mono" }, `${net.hostname}.local`)],
        ]),
        h("div", { class: "actions" }, toggle("Join networks", sta.enabled, async (on) => {
          if (!on && !await confirmAsk(viaAp() ? "Stop joining networks? The board stays on its access point."
            : "Stop joining networks? This page reaches the board over Wi-Fi, and will lose it: turn it back on from the console (wifi on) or the board's access point.",
          { ok: "Turn it off", danger: true })) { render(); return; }
          ctx.api.patch("/network", { sta_enabled: on }).then(() => { say(on ? "Joining networks again." : "Wi-Fi is off."); setTimeout(() => load().catch(() => {}), 5000); }).catch(fail);
        })));

      knownBox.replaceChildren(h("h2", {}, "Saved networks"),
        net.known.length ? h("ul", { class: "rows" }, net.known.map((k) => h("li", { class: "row" },
          h("span", { class: "grow" }, h("span", { class: "name" }, k.ssid), h("span", { class: "sub" }, k.last ? "rejoined at start" : "")),
          sta.connected && sta.ssid === k.ssid ? chip("on it", "ok") : h("button", { class: "button small", onclick: () => join(k.ssid) }, "Join"),
          h("button", { class: "button small danger", onclick: async () => {
            const current = sta.connected && sta.ssid === k.ssid;
            if (!await confirmAsk(current && !viaAp() ? `Forget ${k.ssid}? The board is on it, and leaves it: this page will lose the board.` : `Forget ${k.ssid}?`,
              { ok: "Forget", danger: true })) return;
            ctx.api.del(`/network/known?ssid=${encodeURIComponent(k.ssid)}`).then(load).catch(fail);
          } }, "Forget")))) : h("p", { class: "muted" }, "None."),
        h("div", { class: "actions" }, h("button", { class: "button", onclick: () => add() }, "Add a network")),
        h("p", { class: "muted hint" }, "The board keeps 16; saving another forgets the one saved longest ago."));

      const ap = net.ap;
      apBox.replaceChildren(h("h2", {}, "Its own access point", ap.on ? chip("on", "ok") : chip("off")),
        facts([
          ["Name", ap.ssid],
          ap.on ? ["Address", h("span", { class: "mono" }, ap.ip)] : null,
          ap.on ? ["Connected", String(ap.clients)] : null,
          ap.off_in_s ? ["Turns off", `in ${Math.ceil(ap.off_in_s / 60)} min`] : null,
        ]),
        h("p", { class: "muted hint" }, "For when there is no network to join: phones and laptops join the board itself. "
          + "It is off at every start, unless the board can't join a network then: it comes on for 5 minutes, as it does when BOOT is pressed. "
          + "Turned on here, it stays on."),
        h("div", { class: "actions" },
          ap.off_in_s ? h("button", { class: "button primary", onclick: () => {
            ctx.api.patch("/network/ap", { on: true }).then(load).catch(fail);
          } }, "Keep it on") : null,
          h("button", { class: `button${ap.on ? "" : " primary"}`, onclick: async () => {
            if (ap.on && viaAp() && !await confirmAsk("Turn the access point off? This page reaches the board through it, and will lose it.", { ok: "Turn it off", danger: true })) return;
            ctx.api.patch("/network/ap", { on: !ap.on }).then(() => setTimeout(() => load().catch(() => {}), 1500)).catch(fail);
          } }, ap.on ? "Turn it off" : "Turn it on"),
          h("button", { class: "button", onclick: () => apCredentials() }, "Name and passphrase")));
    }

    async function add() {
      const ssid = await askText("Add a network", "Network name (SSID)", "", { ok: "Next", maxlength: 32 });
      if (!ssid) return;
      const pass = await askText(`Passphrase for ${ssid}`, "Passphrase (leave empty for an open network)", "", { ok: "Save", type: "password", maxlength: 63 });
      if (pass === null) return;
      try {
        await ctx.api.put(`/network/known?ssid=${encodeURIComponent(ssid)}`, pass ? { passphrase: pass } : {});
        say(`Saved ${ssid}: the board joins it at start when it is in range, or now with Join.`);
        await load();
      } catch (e) { fail(e); }
    }

    async function apCredentials() {
      const ssid = await askText("The access point's name", "Name (SSID)", net.ap.ssid, { ok: "Next", maxlength: 32 });
      if (ssid === null) return;
      const pass = await askText("The access point's passphrase", "New passphrase, 8 to 63 characters (leave empty to keep it)", "", { ok: "Save", type: "password", maxlength: 63 });
      if (pass === null) return;
      if (pass && pass.length < 8) return fail(new Error("A passphrase is at least 8 characters."));
      if (net.ap.on && !await confirmAsk("The access point restarts with these, and whoever is on it must join again.", { ok: "Save" })) return;
      const body = {};
      if (ssid && ssid !== net.ap.ssid) body.ssid = ssid;
      if (pass) body.passphrase = pass;
      if (!Object.keys(body).length) return;
      ctx.api.patch("/network/ap", body).then(() => { say("Saved."); setTimeout(() => load().catch(() => {}), 1500); }).catch(fail);
    }

    // --- in range -----------------------------------------------------------------------------------
    const scanList = h("div");
    const scanBtn = h("button", { class: "button", onclick: () => scan() }, "Look");
    scanBox.append(h("h2", {}, "In range"), h("p", { class: "muted hint" }, "What the board can hear, strongest first. Looking takes a few seconds."),
      h("div", { class: "actions", style: "margin-top:0" }, scanBtn), scanList);

    async function scan() {
      scanBtn.disabled = true;
      scanList.replaceChildren(h("p", { class: "muted" }, "Looking…"));
      try {
        const { networks } = await ctx.api.get("/network/scan");
        scanList.replaceChildren(networks.length ? h("ul", { class: "rows", style: "margin-top:12px" }, networks.map((n) => h("li", { class: "row" },
          h("span", { class: "grow" }, h("span", { class: "name" }, n.ssid), h("span", { class: "sub" }, `${bars(n.rssi)} ${n.rssi} dBm · ch ${n.channel} · ${n.auth}`)),
          n.known ? chip("saved") : null,
          net?.sta.connected && net.sta.ssid === n.ssid ? chip("on it", "ok") : h("button", { class: "button small", onclick: async () => {
            let pass;
            if (!n.known && n.auth !== "open") {
              pass = await askText(`Passphrase for ${n.ssid}`, "Passphrase", "", { ok: "Join", type: "password", minlength: 8, maxlength: 63 });
              if (!pass) return;
            }
            join(n.ssid, pass);
          } }, "Join")))) : h("p", { class: "muted" }, "Nothing in range."));
      } catch (e) {
        scanList.replaceChildren();
        fail(e);
      } finally {
        scanBtn.disabled = false;
      }
    }

    load().catch(fail);
    return null;
  },
};
