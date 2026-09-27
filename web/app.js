// The shell: header, navigation and routing. Each page is a module in pages/ with
//   { id, title, icon, feature, primary, mount(el, ctx) -> unmount }
// and is shown when the board lists its `feature` in /api/v1/info (null: always). On a phone the
// `primary` pages are the tabs along the bottom, and the rest are under More; on a wider screen
// the side bar lists them all.

import { createApi } from "./api.js";
import { h, icon, notice, code, store, sheet } from "./ui.js";
import status from "./pages/status.js";
import show from "./pages/show.js";
import holo from "./pages/holo.js";
import scenes from "./pages/scenes.js";
import files from "./pages/files.js";
import settings from "./pages/settings.js";
import network from "./pages/network.js";
import update from "./pages/update.js";

const PAGES = [status, show, holo, scenes, files, settings, network, update];
const MORE_ICON = "M6 10c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2zm12 0c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2zm-6 0c-1.1 0-2 .9-2 2s.9 2 2 2 2-.9 2-2-.9-2-2-2z";
const wide = window.matchMedia("(min-width: 720px)");

const $ = (id) => document.getElementById(id);

// --- the password dialog ------------------------------------------------------------------------

function askPassword(retry) {
  const dialog = $("auth");
  const form = dialog.querySelector("form");
  const error = dialog.querySelector(".error");
  error.hidden = !retry;
  error.textContent = retry ? "That password was not accepted." : "";
  form.reset();
  return new Promise((resolve) => {
    dialog.addEventListener("close", () => {
      resolve(dialog.returnValue === "ok" ? form.password.value : null);
    }, { once: true });
    dialog.showModal();
  });
}

const api = createApi({ askPassword });

// --- shared state -------------------------------------------------------------------------------

const ctx = {
  api,
  info: null,
  async refreshInfo() {
    ctx.info = await api.get("/info");
    renderChrome();
    return ctx.info;
  },
  navigate(id) { location.hash = `#/${id}`; },
};

function linkLabel(info) {
  if (info.via === "ap") return `Access point · ${info.ap?.ip ?? ""}`;
  if (info.sta?.connected) return `${info.sta.ssid} · ${info.sta.ip}`;
  return "Connected";
}

function visiblePages() {
  const features = ctx.info?.features ?? [];
  return PAGES.filter((p) => p.feature === null || features.includes(p.feature));
}

function renderChrome() {
  const info = ctx.info;
  if (!info) return;
  $("name").textContent = info.hostname;
  document.title = `${info.hostname} · Holo Player`;
  const link = $("link");
  link.hidden = false;
  link.textContent = linkLabel(info);

  const current = route();
  const pageLink = (p) => h("a", { href: `#/${p.id}`, "aria-current": p.id === current ? "page" : false }, icon(p.icon), h("span", {}, p.title));
  const pages = visiblePages();
  const tabs = pages.filter((p) => p.primary);
  const rest = pages.filter((p) => !p.primary);
  if (wide.matches || rest.length === 0) {
    $("nav").replaceChildren(...pages.map(pageLink));
  } else {
    const onRest = rest.some((p) => p.id === current);
    const more = h("button", {
      type: "button", class: "more", "aria-current": onRest ? "page" : false, "aria-haspopup": "dialog",
      onclick: () => {
        const s = sheet(h("nav", { class: "sheet-nav", "aria-label": "More pages" }, rest.map((p) => {
          const a = pageLink(p);
          a.addEventListener("click", () => s.close());
          return a;
        })));
      },
    }, icon(MORE_ICON), h("span", {}, onRest ? rest.find((p) => p.id === current).title : "More"));
    $("nav").replaceChildren(...tabs.map(pageLink), more);
  }

  const banners = [];
  if (!info.auth && store.get("holo.dismiss-auth") !== "1") {
    const close = h("button", { class: "close", "aria-label": "Dismiss", onclick: () => { store.set("holo.dismiss-auth", "1"); renderChrome(); } }, "×");
    const n = notice("warn", h("p", {}, "No password is set: anyone on this network can change or update the board. ",
      info.features.includes("settings") ? h("a", { href: "#/settings" }, "Set one") : ["Set one on the console with ", code("web password <password>")], "."));
    n.append(close);
    banners.push(n);
  }
  if (info.via === "ap") {
    banners.push(notice("", h("p", {}, "You are on the board's own access point. If this page opened in a sign-in window, open ",
      h("a", { href: `http://${info.ap?.ip ?? "192.168.4.1"}/`, target: "_blank" }, `http://${info.ap?.ip ?? "192.168.4.1"}/`),
      " in your browser to upload files.")));
  }
  $("banners").replaceChildren(...banners);
}

// --- routing ------------------------------------------------------------------------------------

function route() {
  const id = location.hash.replace(/^#\/?/, "").split("/")[0];
  return PAGES.some((p) => p.id === id) ? id : PAGES[0].id;
}

let unmount = null;

function showPage() {
  const id = route();
  const page = visiblePages().find((p) => p.id === id) ?? PAGES[0];
  unmount?.();
  unmount = null;
  const el = $("page");
  el.replaceChildren();
  renderChrome();
  unmount = page.mount(el, ctx) ?? null;
  el.focus({ preventScroll: true });
}

async function start() {
  try {
    await ctx.refreshInfo();
  } catch (e) {
    $("page").replaceChildren(notice("bad", h("p", {}, `The board did not answer: ${e.message}`),
      h("p", {}, h("button", { class: "button", onclick: () => location.reload() }, "Try again"))));
    return;
  }
  window.addEventListener("hashchange", showPage);
  wide.addEventListener("change", renderChrome);
  showPage();
}

start();
