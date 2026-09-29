// DOM helpers shared by the installer and the board pages. Everything from the board or a
// manifest goes in as text, never HTML.

export function h(tag, attrs = {}, ...children) {
  const el = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (value === false || value === null || value === undefined) continue;
    if (key === "class") el.className = value;
    else if (key.startsWith("on")) el.addEventListener(key.slice(2), value);
    else el.setAttribute(key, value === true ? "" : value);
  }
  for (const child of children.flat()) {
    if (child !== null && child !== undefined && child !== false) el.append(child);
  }
  return el;
}

export const code = (text) => h("code", {}, text);

// The root of this version of the site, from the theme's own config: the site is built both with
// and without directory URLs, so no fixed relative path works from every page.
export function siteRoot() {
  try {
    const base = JSON.parse(document.getElementById("__config").textContent).base;
    return new URL(base.endsWith("/") ? base : `${base}/`, location.href);
  } catch {
    return new URL("./", location.href);
  }
}

// A page of this site, by its path under the root without an extension ("use/board"), whichever
// way the site was built: the offline copy has no directory URLs.
export function pageUrl(path) {
  const root = siteRoot();
  const directoryUrls = !/\.html$/.test(location.pathname);
  return new URL(directoryUrls ? `${path}/` : `${path}.html`, root);
}
