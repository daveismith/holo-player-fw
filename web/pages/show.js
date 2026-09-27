// Show: what the screen shows now, the clips and images there are to show, a colour or the
// crosshair, and the LEDs beside it -- what the board shows, in one place.

import { poll } from "../api.js";
import { h, chip, notice, formatBytes, slider, segmented, toggle, colourOf, icon } from "../ui.js";

const ICON = "M21 3H3c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h5v2h8v-2h5c1.1 0 1.99-.9 1.99-2L23 5c0-1.1-.9-2-2-2zm0 14H3V5h18v12zm-5-6l-7 4V7z";
const CLIP = "M18 4l2 4h-3l-2-4h-2l2 4h-3l-2-4H8l2 4H7L5 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V4h-4z";
const IMAGE = "M21 19V5c0-1.1-.9-2-2-2H5c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h14c1.1 0 2-.9 2-2zM8.5 13.5l2.5 3.01L14.5 12l4.5 6H5l3.5-4.5z";
const SWATCHES = ["#ffffff", "#ff0000", "#ff8000", "#ffc107", "#00ff00", "#00bcd4", "#0000ff", "#673ab7", "#ff69b4", "#000000"];
const LED_MODES = [["off", "Off"], ["solid", "Solid"], ["wipe", "Wipe"], ["rainbow", "Rainbow"]];

function describe(s) {
  switch (s.showing) {
    case "clip": return `Playing ${s.path}`;
    case "image": return `Showing ${s.path}`;
    case "colour": return `Showing ${s.colour}`;
    case "calibration": return "Showing the alignment crosshair";
    default: return "Nothing: the screen is off";
  }
}

export default {
  id: "show",
  title: "Show",
  icon: ICON,
  feature: "screen",
  primary: true,

  mount(el, ctx) {
    const alert = h("div");
    const nowBox = h("section", { class: "card" });
    const mediaBox = h("section", { class: "card" });
    const colourBox = h("section", { class: "card" });
    const ledsBox = h("section", { class: "card" });
    const hasLeds = ctx.info.features.includes("leds");
    el.append(h("h1", {}, "Show"), alert, h("div", { class: "grid" }, nowBox, colourBox, hasLeds ? ledsBox : null),
      h("div", { style: "margin-top:16px" }, mediaBox));

    const fail = (e) => alert.replaceChildren(notice("bad", h("p", {}, e.message)));
    const act = async (fn) => {
      alert.replaceChildren();
      try { await fn(); } catch (e) { fail(e); }
      refresh().catch(fail);
    };

    // --- now --------------------------------------------------------------------------------
    let screen = null;
    const backlight = slider({ label: "Backlight", value: 100, unit: "%", onchange: (v) => ctx.api.patch("/screen", { backlight: v }).catch(fail) });
    const nowText = h("p");
    const nowExtra = h("div");
    const stop = h("button", { class: "button", onclick: () => act(() => ctx.api.del("/screen")) }, "Clear the screen");
    nowBox.append(h("h2", {}, "On the screen"), nowText, nowExtra, backlight, h("div", { class: "actions" }, stop));

    function renderNow() {
      nowText.textContent = describe(screen);
      backlight.set(screen.backlight);
      stop.disabled = screen.showing === "nothing";
      const extra = [];
      if (screen.showing === "clip" && screen.clip) {
        const c = screen.clip;
        const frame = c.frames ? c.shown % c.frames : 0;
        extra.push(h("div", { class: "chips" }, chip(screen.loop ? "looping" : "once"), chip(`${c.width}×${c.height}`), c.fps ? chip(`${Math.round(c.fps)} fps`) : null,
          c.late ? chip(`${c.late} late`, "warn") : null),
          h("progress", { max: c.frames || 1, value: frame }),
          h("div", { class: "progress-line" }, h("span", {}, `frame ${frame} of ${c.frames}`), h("span", {}, c.loops ? `${c.loops} loop${c.loops > 1 ? "s" : ""}` : "")));
      }
      if (screen.showing === "colour") {
        extra.push(h("div", { class: "swatch", style: `background:${screen.colour};cursor:default`, "aria-hidden": "true" }));
      }
      nowExtra.replaceChildren(...extra);
    }

    // --- a colour, or the crosshair ---------------------------------------------------------
    const picker = h("input", { type: "color", value: "#ff8000", "aria-label": "Colour" });
    colourBox.append(h("h2", {}, "A colour"),
      h("div", { class: "swatches" }, SWATCHES.map((c) => h("button", {
        class: "swatch", style: `background:${c}`, title: c, "aria-label": `Show ${c}`,
        onclick: () => act(() => ctx.api.post("/screen/show", { colour: c })),
      }))),
      h("div", { class: "actions" }, picker,
        h("button", { class: "button", onclick: () => act(() => ctx.api.post("/screen/show", { colour: picker.value })) }, "Show this colour")),
      h("div", { class: "actions" },
        h("button", { class: "button", onclick: () => act(() => ctx.api.post("/screen/show", { calibration: true })) }, "Show the alignment crosshair")),
      h("p", { class: "muted hint", style: "margin-top:8px" }, "The crosshair's arrow points up, for lining the screen up behind the dome's lens."));

    // --- the LEDs -----------------------------------------------------------------------------
    let leds = null;
    const patchLeds = (body) => ctx.api.patch("/leds", body).then((l) => { leds = l; renderLeds(); }).catch(fail);
    const mode = segmented(LED_MODES, "off", (m) => patchLeds({ mode: m, colour: ledColour.value, loop: loopLeds.input.checked }), "LED pattern");
    const ledColour = h("input", { type: "color", value: "#ffffff", "aria-label": "LED colour",
      onchange: () => patchLeds({ colour: ledColour.value, ...(leds?.mode === "off" ? { mode: "solid" } : {}) }) });
    const loopLeds = toggle("Loop the pattern", false, (on) => leds?.mode === "wipe" || leds?.mode === "rainbow" ? patchLeds({ loop: on }) : null);
    const brightness = slider({ label: "Brightness", value: 30, min: 1, unit: "%", onchange: (v) => patchLeds({ brightness: v }) });
    const ledNote = h("p", { class: "muted hint" });
    ledsBox.append(h("h2", {}, "LEDs"), mode, h("div", { class: "actions" }, ledColour, loopLeds), brightness, ledNote);

    function renderLeds() {
      mode.set(leds.mode);
      if (document.activeElement !== ledColour) ledColour.value = colourOf(leds.colour);
      loopLeds.input.checked = leds.loop;
      brightness.set(leds.brightness);
      ledNote.textContent = `${leds.count} LEDs on GPIO${leds.gpio}. Keep the brightness at 30% or below when the strip is powered from the board.`;
    }

    // --- clips and images -----------------------------------------------------------------------
    const loopMedia = toggle("Loop clips and animations", true, () => {});
    const list = h("ul", { class: "rows" });
    mediaBox.append(h("h2", {}, "Clips and images", h("button", { class: "button small", style: "margin-left:auto", onclick: () => loadMedia().catch(fail) }, "Refresh")),
      loopMedia, list);

    async function loadMedia() {
      const { items } = await ctx.api.get("/media");
      if (!items.length) {
        list.replaceChildren(h("li", { class: "row" }, h("span", { class: "muted" }, "Nothing to show yet: upload clips and images on the ",
          h("a", { href: "#/files" }, "Files"), " page.")));
        return;
      }
      list.replaceChildren(...items.map((m) => {
        const current = screen && (screen.showing === "clip" || screen.showing === "image") && screen.path === m.path;
        return h("li", { class: `row${current ? " current" : ""}` },
          icon(m.kind === "image" ? IMAGE : CLIP),
          h("span", { class: "grow" }, h("span", { class: "name" }, m.path.split("/").pop()),
            h("span", { class: "sub" }, `${m.path} · ${formatBytes(m.size)}`)),
          h("button", { class: "button small primary", onclick: () => act(() => ctx.api.post("/screen/show",
            m.kind === "image" ? { path: m.path } : { path: m.path, loop: loopMedia.input.checked })) },
          m.kind === "image" ? "Show" : "Play"));
      }));
    }

    let lastPath;       // undefined until the first refresh, so the list loads then
    async function refresh() {
      screen = await ctx.api.get("/screen");
      renderNow();
      if (hasLeds) {
        leds = await ctx.api.get("/leds");
        renderLeds();
      }
      const path = screen.path ?? null;
      if (path !== lastPath) {         // the highlighted row follows what is showing
        lastPath = path;
        await loadMedia();
      }
    }

    return poll(refresh, () => (screen?.showing === "clip" ? 1000 : 4000), fail);
  },
};
