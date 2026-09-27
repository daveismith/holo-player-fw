// Scenes: what the screen, the LEDs and the holo do together, saved by name and applied in one
// tap -- and the one the board starts with.

import { h, chip, notice, toggle, field, confirmAsk, colourOf } from "../ui.js";

const ICON = "M4 6H2v14c0 1.1.9 2 2 2h14v-2H4V6zm16-4H8c-1.1 0-2 .9-2 2v12c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V4c0-1.1-.9-2-2-2zm-8 12.5v-9l6 4.5-6 4.5z";
const NAME = /^[A-Za-z0-9 _.-]{1,32}$/;
const MOTIONS = ["twitch", "scan", "wag", "nod", "circle", "center", "stop", "off"];

function summary(scene) {
  const parts = [];
  const s = scene.screen;
  if (s) parts.push(s.path ? `screen: ${s.path.split("/").pop()}${s.loop ? " (loop)" : s.loops ? ` ×${s.loops}` : ""}` : s.colour ? `screen: ${s.colour}`
    : s.calibration ? "screen: crosshair" : "screen: off");
  const l = scene.leds;
  if (l) parts.push(`LEDs: ${l.mode ?? ""}${l.colour ? ` ${l.colour}` : ""}${l.brightness ? ` ${l.brightness}%` : ""}`.trim());
  if (scene.holo) parts.push(`holo: ${scene.holo.motion}`);
  if (scene.duration_s) parts.push(`${scene.duration_s} s`);
  if (scene.then && scene.then !== "stay") parts.push(scene.then === "restore" ? "then back as it was" : "then all off");
  return parts;
}

// Build a scene from the editor's state.
function sceneOf(form) {
  const scene = { name: form.name.value.trim() };
  if (form.description.value.trim()) scene.description = form.description.value.trim();
  if (form.useScreen.checked) {
    const kind = form.screenKind.value;
    if (kind === "file") {
      scene.screen = { path: form.screenPath.value };
      if (form.screenPlays.value === "forever") scene.screen.loop = true;
      else if (form.screenPlays.value === "times") scene.screen.loops = Math.max(1, Math.min(1000, Number(form.screenLoops.value) || 1));
    }
    else if (kind === "colour") scene.screen = { colour: form.screenColour.value };
    else if (kind === "calibration") scene.screen = { calibration: true };
    else scene.screen = { clear: true };
  }
  if (form.useLeds.checked) {
    scene.leds = { mode: form.ledMode.value };
    if (form.ledMode.value !== "rainbow" && form.ledMode.value !== "off") scene.leds.colour = form.ledColour.value;
    if (form.ledMode.value === "flicker") delete scene.leds.loop;
    if (form.ledMode.value === "wipe" || form.ledMode.value === "rainbow") scene.leds.loop = form.ledLoop.checked;
    scene.leds.brightness = Number(form.ledBright.value);
  }
  if (form.useHolo.checked) {
    scene.holo = { motion: form.holoMotion.value };
    if (["twitch", "scan", "wag", "nod", "circle"].includes(form.holoMotion.value)) scene.holo.range = Number(form.holoRange.value);
  }
  if (form.then.value !== "stay") scene.then = form.then.value;
  if (form.duration.value) scene.duration_s = Number(form.duration.value);
  return scene;
}

export default {
  id: "scenes",
  title: "Scenes",
  icon: ICON,
  feature: "scenes",
  primary: true,

  mount(el, ctx) {
    const alert = h("div");
    const listBox = h("div", { class: "stack" });
    const activeBox = h("div");
    const editorBox = h("div");
    el.append(h("h1", {}, "Scenes"),
      h("p", { class: "muted" }, "A scene is what the screen shows, what the LEDs do and how the holo moves, saved together. Any part can be left out: a scene with only LEDs is an LED preset."),
      alert, activeBox, editorBox, listBox);

    const fail = (e) => alert.replaceChildren(notice("bad", h("p", {}, e.message)));
    const say = (text) => alert.replaceChildren(notice("ok", h("p", {}, text)));
    let scenes = [];
    let settings = null;
    let media = [];

    let active = null;
    async function load() {
      let list;
      [list, settings] = await Promise.all([ctx.api.get("/scenes"), ctx.api.get("/settings")]);
      scenes = list.scenes;
      renderActive(list.active);
      render();
    }

    function renderActive(a) {
      active = a;
      activeBox.replaceChildren(...(a ? [h("section", { class: "card", style: "margin-bottom:16px" },
        h("h2", {}, `${a.name} is running`, chip(a.then === "restore" ? "then back as it was" : "then all off", "primary")),
        h("p", { class: "muted" }, a.remaining_s !== null ? `${Math.ceil(a.remaining_s)} s to go${a.until_clip_ends ? ", or until its clip is done" : ""}.` : "Until its clip is done."),
        h("div", { class: "actions" }, h("button", { class: "button", onclick: () => ctx.api.post("/scenes/end", {}).then(() => { say(`Ended ${a.name}.`); return load(); }).catch(fail) }, "End it now")))] : []));
    }

    // While a scene runs to its end, follow it
    const follow = setInterval(() => {
      if (active) ctx.api.get("/scenes").then((r) => renderActive(r.active)).catch(() => {});
    }, 2000);

    function render() {
      const newBtn = h("div", { class: "actions" }, h("button", { class: "button primary", onclick: () => edit(null) }, "New scene"));
      if (!scenes.length) {
        listBox.replaceChildren(newBtn, h("p", { class: "muted" }, "No scenes yet."));
        return;
      }
      listBox.replaceChildren(newBtn, ...scenes.map((scene) => {
        const boot = settings?.boot_scene === scene.name;
        return h("section", { class: "card" },
          h("h2", {}, scene.name, boot ? chip("at start", "primary") : null),
          scene.description ? h("p", {}, scene.description) : null,
          h("div", { class: "chips" }, summary(scene).map((p) => chip(p))),
          h("div", { class: "actions" },
            h("button", { class: "button primary", onclick: () => ctx.api.post("/scenes/apply", { name: scene.name }).then(() => { say(`Applied ${scene.name}.`); return load(); }).catch(fail) }, "Apply"),
            h("button", { class: "button", onclick: () => edit(scene) }, "Edit"),
            h("button", { class: "button", onclick: () => ctx.api.patch("/settings", { boot_scene: boot ? null : scene.name }).then(load)
              .then(() => say(boot ? "The board now starts with nothing." : `The board now starts with ${scene.name}.`)).catch(fail) },
            boot ? "Don't start with it" : "Start with it"),
            h("button", { class: "button danger", onclick: async () => {
              if (!await confirmAsk(`Delete the scene ${scene.name}?`, { ok: "Delete", danger: true })) return;
              ctx.api.del(`/scenes/scene?name=${encodeURIComponent(scene.name)}`).then(load).catch(fail);
            } }, "Delete")));
      }));
    }

    // --- the editor ---------------------------------------------------------------------------
    async function edit(scene) {
      alert.replaceChildren();
      media = (await ctx.api.get("/media").catch(() => ({ items: [] }))).items;
      const s = scene ?? { name: "" };
      const f = {};
      f.name = h("input", { type: "text", value: s.name, maxlength: 32, required: true, disabled: Boolean(scene), autocomplete: "off" });
      f.description = h("input", { type: "text", value: s.description ?? "", maxlength: 120 });

      // screen
      f.useScreen = h("input", { type: "checkbox", checked: Boolean(s.screen) });
      f.screenKind = h("select", {}, [["file", "A clip or image"], ["colour", "A colour"], ["calibration", "The crosshair"], ["clear", "Nothing (off)"]]
        .map(([v, t]) => h("option", { value: v }, t)));
      f.screenKind.value = s.screen?.path ? "file" : s.screen?.colour ? "colour" : s.screen?.calibration ? "calibration" : s.screen?.clear ? "clear" : "file";
      f.screenPath = h("select", {}, media.map((m) => h("option", { value: m.path }, m.path)));
      if (s.screen?.path && !media.some((m) => m.path === s.screen.path)) f.screenPath.prepend(h("option", { value: s.screen.path }, `${s.screen.path} (missing)`));
      if (s.screen?.path) f.screenPath.value = s.screen.path;
      f.screenPlays = h("select", {}, [["once", "Once"], ["times", "A number of times"], ["forever", "Over and over"]].map(([v, t]) => h("option", { value: v }, t)));
      f.screenPlays.value = s.screen?.loop ? "forever" : s.screen?.loops ? "times" : s.screen ? "once" : "forever";
      f.screenLoops = h("input", { type: "number", min: 1, max: 1000, value: s.screen?.loops ?? 2 });
      f.screenColour = h("input", { type: "color", value: colourOf(s.screen?.colour) });

      // LEDs
      f.useLeds = h("input", { type: "checkbox", checked: Boolean(s.leds) });
      f.ledMode = h("select", {}, ["solid", "wipe", "rainbow", "flicker", "off"].map((m) => h("option", { value: m }, m)));
      f.ledMode.value = s.leds?.mode ?? "solid";
      f.ledColour = h("input", { type: "color", value: colourOf(s.leds?.colour) });
      f.ledLoop = h("input", { type: "checkbox", checked: s.leds?.loop ?? true });
      f.ledBright = h("input", { type: "number", min: 1, max: 100, value: s.leds?.brightness ?? 30 });

      // holo
      f.useHolo = h("input", { type: "checkbox", checked: Boolean(s.holo) });
      f.holoMotion = h("select", {}, MOTIONS.map((m) => h("option", { value: m }, m)));
      f.holoMotion.value = s.holo?.motion ?? "twitch";
      f.holoRange = h("input", { type: "number", min: 1, max: 100, value: s.holo?.range ?? 50 });

      // when it ends
      f.then = h("select", {}, [["stay", "Leave it as it is"], ["restore", "Go back to before"], ["off", "All off"]].map(([v, t]) => h("option", { value: v }, t)));
      f.then.value = s.then ?? "stay";
      f.duration = h("input", { type: "number", min: 0.1, max: 86400, step: "any", value: s.duration_s ?? "", placeholder: "until its clip is done" });

      const part = (title, use, ...content) => {
        const body = h("div", { style: "margin-top:8px" }, ...content);
        const sync = () => { body.hidden = !use.checked; };
        use.addEventListener("change", sync);
        sync();
        return h("fieldset", { class: "card", style: "margin-top:12px" }, h("legend", {}, h("label", { class: "toggle" }, use, h("strong", {}, title))), body);
      };
      const screenFields = h("div");
      const syncScreen = () => {
        const k = f.screenKind.value;
        screenFields.replaceChildren(...[
          k === "file" ? field("File", f.screenPath, media.length ? null : "No clips or images on the board yet.") : null,
          k === "file" ? h("div", { class: "fields" }, field("Plays", f.screenPlays),
            f.screenPlays.value === "times" ? field("Times", f.screenLoops) : null) : null,
          k === "colour" ? field("Colour", f.screenColour) : null].filter(Boolean));
      };
      f.screenKind.addEventListener("change", syncScreen);
      f.screenPlays.addEventListener("change", syncScreen);
      syncScreen();

      const fill = h("button", { type: "button", class: "button", onclick: async () => {
        try {
          const [screen, leds, holo] = await Promise.all([ctx.api.get("/screen"), ctx.api.get("/leds"), ctx.api.get("/holo")]);
          f.useScreen.checked = true;
          f.screenKind.value = screen.showing === "clip" || screen.showing === "image" ? "file" : screen.showing === "colour" ? "colour"
            : screen.showing === "calibration" ? "calibration" : "clear";
          if (screen.path) {
            if (![...f.screenPath.options].some((o) => o.value === screen.path)) f.screenPath.append(h("option", { value: screen.path }, screen.path));
            f.screenPath.value = screen.path;
          }
          f.screenPlays.value = screen.showing === "clip" ? (screen.loop ? "forever" : "times") : "once";
          if (screen.clip?.plays) f.screenLoops.value = screen.clip.plays;
          if (screen.colour) f.screenColour.value = screen.colour;
          f.useLeds.checked = true;
          f.ledMode.value = leds.mode;
          f.ledColour.value = colourOf(leds.colour);
          f.ledLoop.checked = leds.loop;
          f.ledBright.value = leds.brightness;
          f.useHolo.checked = holo.motion !== "hold";
          if (holo.motion !== "hold" && MOTIONS.includes(holo.motion)) f.holoMotion.value = holo.motion;
          for (const c of [f.useScreen, f.useLeds, f.useHolo]) c.dispatchEvent(new Event("change"));
          syncScreen();
        } catch (e) { fail(e); }
      } }, "Fill from what's showing now");

      const card = h("section", { class: "card" },
        h("h2", {}, scene ? `Edit ${scene.name}` : "New scene"),
        h("div", { class: "fields" }, field("Name", f.name, scene ? null : "Letters, digits, spaces, _ . and -"), field("Description", f.description)),
        h("div", { class: "actions" }, fill),
        part("The screen", f.useScreen, field("Show", f.screenKind), screenFields),
        part("The LEDs", f.useLeds, h("div", { class: "fields" }, field("Pattern", f.ledMode), field("Colour", f.ledColour), field("Brightness (%)", f.ledBright)),
          h("label", { class: "toggle" }, f.ledLoop, h("span", {}, "Loop a wipe or rainbow"))),
        part("The holo", f.useHolo, h("div", { class: "fields" }, field("Motion", f.holoMotion), field("How far (%)", f.holoRange))),
        h("fieldset", { class: "card", style: "margin-top:12px" }, h("legend", {}, h("strong", {}, "When it ends")),
          h("div", { class: "fields", style: "margin-top:8px" }, field("Then", f.then), field("Or after (seconds)", f.duration)),
          h("p", { class: "muted hint" }, "The scene ends when its clip has played its number of times, or after the seconds given, whichever is first. ",
            "Going back restarts what the LEDs and the holo were doing (a twitch twitches again), and what the screen showed.")),
        h("div", { class: "actions" },
          h("button", { class: "button primary", onclick: () => save(f, scene) }, "Save"),
          h("button", { class: "button", onclick: () => ctx.api.post("/scenes/apply", { scene: sceneOf(f) }).then(() => { say("Tried it: nothing was saved."); return load(); }).catch(fail) }, "Try it"),
          h("button", { class: "button", onclick: () => editorBox.replaceChildren() }, "Cancel")));
      editorBox.replaceChildren(card);
      card.scrollIntoView({ block: "start", behavior: "smooth" });
    }

    async function save(f, existing) {
      const scene = sceneOf(f);
      if (!NAME.test(scene.name)) return fail(new Error("A name is 1-32 letters, digits, spaces, _ . and -."));
      if (!scene.screen && !scene.leds && !scene.holo) return fail(new Error("Choose at least one part: the screen, the LEDs or the holo."));
      if (scene.then && !scene.duration_s && !scene.screen?.loops) {
        return fail(new Error("To do something when it ends, the scene has to end: play its clip a number of times, or give it seconds."));
      }
      if (!existing && scenes.some((x) => x.name === scene.name)
        && !await confirmAsk(`A scene called ${scene.name} exists. Replace it?`, { ok: "Replace" })) return;
      try {
        await ctx.api.put(`/scenes/scene?name=${encodeURIComponent(scene.name)}`, scene);
        editorBox.replaceChildren();
        say(`Saved ${scene.name}.`);
        await load();
      } catch (e) { fail(e); }
    }

    load().catch(fail);
    return () => clearInterval(follow);
  },
};
