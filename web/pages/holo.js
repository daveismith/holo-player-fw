// Holo: the holoprojector's motion -- a pad to point it, and its behaviours -- and the servos
// behind it: where each is, and calibrating the travel.

import { poll } from "../api.js";
import { h, chip, notice, facts, slider, segmented, confirmAsk, field } from "../ui.js";

const ICON = "M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zM12 17c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z";

const BEHAVIOURS = [
  ["twitch", "Twitch", "Random glances, until stopped"],
  ["scan", "Scan", "A slow sweep side to side, until stopped"],
  ["wag", "Wag", "Side to side, and back"],
  ["nod", "Nod", "Up and down, and back"],
  ["circle", "Circle", "Round once, and back"],
];

const WHY = {
  "uncalibrated": "Its servos' travel has not been saved yet: calibrate them below.",
  "not fitted": "A servo is missing.",
  "not armed": "It is not armed.",
};

export default {
  id: "holo",
  title: "Holo",
  icon: ICON,
  feature: "holo",
  primary: true,

  mount(el, ctx) {
    const alert = h("div");
    const stateBox = h("section", { class: "card" });
    const moveBox = h("section", { class: "card" });
    const servoBox = h("div", { class: "stack" });
    el.append(h("h1", {}, "Holoprojector"), alert, h("div", { class: "grid" }, stateBox, moveBox),
      h("h2", { style: "margin-top:24px" }, "Servos"), servoBox);

    const fail = (e) => alert.replaceChildren(notice("bad", h("p", {}, e.message)));
    const motion = (body) => ctx.api.post("/holo/motion", body).then((s) => { holo = s; renderState(); }).catch(fail);

    // --- where it is, and the pad ---------------------------------------------------------------
    let holo = null;
    const dot = h("div", { class: "dot" });
    const target = h("div", { class: "target", hidden: true });
    const pad = h("div", { class: "pad", role: "application", "aria-label": "Drag to point the holo" }, dot, target);
    const stateText = h("div");
    stateBox.append(h("h2", {}, "Point it"), stateText, pad,
      h("p", { class: "muted hint", style: "text-align:center" }, "Drag, or tap, where it should look: up is up, right is right."),
      h("div", { class: "actions", style: "justify-content:center" },
        h("button", { class: "button", onclick: () => motion({ motion: "center" }) }, "Centre"),
        h("button", { class: "button", onclick: () => motion({ motion: "stop" }) }, "Stop"),
        h("button", { class: "button", title: "Stop, and let the servos go limp", onclick: () => motion({ motion: "off" }) }, "Off")));

    const place = (el2, x, y) => { el2.style.left = `${50 + x * 42}%`; el2.style.top = `${50 - y * 42}%`; };
    place(dot, 0, 0);
    let dragging = false;
    let lastSent = 0;
    let pending = null;
    function aim(e) {
      const r = pad.getBoundingClientRect();
      let x = ((e.clientX - r.left) / r.width - 0.5) / 0.42;
      let y = -((e.clientY - r.top) / r.height - 0.5) / 0.42;
      const d = Math.hypot(x, y);
      if (d > 1) { x /= d; y /= d; }
      target.hidden = false;
      place(target, x, y);
      pending = { motion: "move", x: Math.round(x * 100), y: Math.round(y * 100), duration_ms: 150 };
      if (Date.now() - lastSent > 180) flush();
    }
    function flush() {
      if (!pending) return;
      lastSent = Date.now();
      const body = pending;
      pending = null;
      ctx.api.post("/holo/motion", body).catch(fail);
    }
    pad.addEventListener("pointerdown", (e) => { dragging = true; pad.setPointerCapture(e.pointerId); alert.replaceChildren(); aim(e); });
    pad.addEventListener("pointermove", (e) => { if (dragging) aim(e); });
    pad.addEventListener("pointerup", () => { dragging = false; flush(); setTimeout(() => { target.hidden = true; }, 600); });

    function renderState() {
      place(dot, holo.position.x, holo.position.y);
      stateText.replaceChildren(...[h("div", { class: "chips" },
        chip(holo.motion === "hold" ? "still" : holo.motion, holo.motion === "hold" ? "" : "primary"),
        holo.ready ? chip("ready", "ok") : chip(holo.why ?? "cannot move", "bad"),
        holo.stopped ? chip(`stopped: ${holo.stopped}`, "warn") : null),
        !holo.ready && WHY[holo.why] ? notice("warn", h("p", {}, WHY[holo.why])) : null].filter(Boolean));
    }

    // --- behaviours ---------------------------------------------------------------------------
    const range = slider({ label: "How far", value: 50, min: 5, max: 100, unit: "%", onchange: () => {} });
    const count = h("input", { type: "number", min: 1, max: 20, value: 3 });
    moveBox.append(h("h2", {}, "Behaviours"),
      h("div", { class: "behaviours" }, BEHAVIOURS.map(([m, label, what]) => [
        h("button", { class: "button", onclick: () => {
          const body = { motion: m, range: Number(range.querySelector("input").value) };
          if (["wag", "nod", "circle"].includes(m)) body.count = Math.max(1, Math.min(20, Number(count.value) || 1));
          motion(body);
        } }, label), h("span", { class: "muted" }, what)])),
      h("div", { style: "margin-top:16px" }, range, field("Times (wag, nod, circle)", count)));

    // --- the servos ------------------------------------------------------------------------------
    let servos = [];
    const open = new Set();      // servos whose calibration panel is open: kept across refreshes
    const cards = new Map();

    function servoCard(s) {
      const l = s.limits;
      const card = h("section", { class: "card" });
      const summary = h("div");
      const body = h("div");
      card.append(h("h2", {}, s.id, s.gpio !== undefined ? h("span", { class: "muted mono" }, `GPIO ${s.gpio}`) : null), summary, body);
      card.update = (s2) => {
        summary.replaceChildren(h("div", { class: "chips" },
          s2.driven ? chip("driven", "primary") : s2.released ? chip("released by its policy") : chip("limp"),
          s2.limits.calibrated ? chip("calibrated", "ok") : chip("default range", "warn")),
        facts([
          ["Now", s2.us ? `${s2.us} µs` : "not moved since start"],
          ["Travel", `${s2.limits.op_min_us}–${s2.limits.op_max_us} µs${s2.limits.invert ? ", turned round" : ""}`],
          ["Limits", `${s2.limits.abs_min_us}–${s2.limits.abs_max_us} µs`],
          ["Policy", `closed ${s2.policy.closed}, open ${s2.policy.open}, between ${s2.policy.mid}; settle ${s2.policy.settle_ms} ms`],
        ]));
      };
      card.update(s);

      const toggleBtn = h("button", { class: "button", onclick: () => {
        if (open.has(s.id)) open.delete(s.id); else open.add(s.id);
        renderPanel();
      } });
      body.append(h("div", { class: "actions" }, toggleBtn));
      const panel = h("div");
      body.append(panel);

      function renderPanel() {
        toggleBtn.textContent = open.has(s.id) ? "Done" : "Calibrate";
        if (!open.has(s.id)) { panel.replaceChildren(); return; }
        const cur = servos.find((x) => x.id === s.id) ?? s;
        let closed = cur.limits.invert ? cur.limits.op_max_us : cur.limits.op_min_us;
        let opened = cur.limits.invert ? cur.limits.op_min_us : cur.limits.op_max_us;
        const ends = h("p", { class: "mono" });
        const showEnds = () => { ends.textContent = `closed end ${closed} µs · open end ${opened} µs`; };
        showEnds();
        const pos = slider({ label: "Drive it to", value: cur.us || Math.round((l.abs_min_us + l.abs_max_us) / 2), min: l.abs_min_us, max: l.abs_max_us,
          step: 5, unit: " µs", every: 120, onchange: (v) => ctx.api.post("/servos/move", { id: s.id, us: v }).catch(fail) });
        const here = () => Number(pos.querySelector("input").value);
        const policy = { ...cur.policy };
        const pick = (zone) => segmented([["hold", "Hold"], ["release", "Let go"]], policy[zone], (v) => { policy[zone] = v; }, zone);
        const settle = h("input", { type: "number", min: 0, max: 65535, value: policy.settle_ms });
        panel.replaceChildren(
          notice("", h("p", {}, "Move the slider until the holo is at full ", h("strong", {}, s.id === "tilt" ? "down" : "left"),
            ", and set the closed end; then full ", h("strong", {}, s.id === "tilt" ? "up" : "right"), ", and set the open end. Moving here stops the holo's own motion.")),
          pos, ends,
          h("div", { class: "actions" },
            h("button", { class: "button", onclick: () => { closed = here(); showEnds(); } }, "Set the closed end here"),
            h("button", { class: "button", onclick: () => { opened = here(); showEnds(); } }, "Set the open end here")),
          h("div", { class: "actions" },
            h("button", { class: "button primary", onclick: () => save(ctx.api.put(`/servos/calibration?id=${encodeURIComponent(s.id)}`,
              { min_us: Math.min(closed, opened), max_us: Math.max(closed, opened), invert: closed > opened })) }, "Save the travel"),
            h("button", { class: "button danger", onclick: async () => {
              if (await confirmAsk(`Forget ${s.id}'s saved travel, and go back to the firmware's default?`, { ok: "Forget it", danger: true })) {
                save(ctx.api.del(`/servos/calibration?id=${encodeURIComponent(s.id)}`));
              }
            } }, "Forget it"),
            h("button", { class: "button", onclick: () => ctx.api.post("/servos/release", { id: s.id }).then(refresh).catch(fail) }, "Let it go limp")),
          h("h3", { style: "margin-top:16px" }, "Drive policy"),
          h("p", { class: "muted hint" }, "Whether the servo keeps being driven once it has settled. Letting go at an end stops the buzz of a servo held against a stop."),
          h("div", { class: "fields" },
            h("div", { class: "field" }, h("label", {}, "At the closed end"), pick("closed")),
            h("div", { class: "field" }, h("label", {}, "At the open end"), pick("open")),
            h("div", { class: "field" }, h("label", {}, "Between"), pick("mid")),
            field("Settle (ms)", settle)),
          h("div", { class: "actions" }, h("button", { class: "button", onclick: () => save(ctx.api.put(`/servos/policy?id=${encodeURIComponent(s.id)}`,
            { ...policy, settle_ms: Math.max(0, Math.min(65535, Number(settle.value) || 0)) })) }, "Save the policy")));
      }

      async function save(promise) {
        alert.replaceChildren();
        try {
          const updated = await promise;
          alert.replaceChildren(notice("ok", h("p", {}, `${s.id}: saved.`)));
          if (updated?.id) card.update(updated);
        } catch (e) { fail(e); }
      }
      renderPanel();
      return card;
    }

    function renderServos() {
      for (const s of servos) {
        if (!cards.has(s.id)) {
          cards.set(s.id, servoCard(s));
          servoBox.append(cards.get(s.id));
        } else {
          cards.get(s.id).update(s);
        }
      }
      if (!servos.length) servoBox.replaceChildren(h("p", { class: "muted" }, "No servos."));
    }

    async function refresh() {
      holo = await ctx.api.get("/holo");
      renderState();
      servos = (await ctx.api.get("/servos")).servos;
      renderServos();
    }

    return poll(refresh, () => (holo && holo.motion !== "hold" ? 700 : 3000), fail);
  },
};
