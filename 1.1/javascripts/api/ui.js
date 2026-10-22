// The HTTP API explorer (reference/http-api-explorer.md): Swagger UI over reference/openapi.json.
//
// Loaded on every page through extra_javascript, and does nothing unless the page has a
// #swagger-ui element. Swagger UI is vendored (tools/vendor_js.py), never from a CDN, so the
// explorer works in the offline documentation too.
//
// "Try it out" calls a real board: this site's origins are on the board's CORS allowlist
// (`web cors`; CONFIG_WEB_SERVER_CORS_ORIGINS). Chrome and Edge allow an https page to reach a
// plain-http device on the local network; Safari and Firefox don't (the page says so).

import { siteRoot } from "../common/dom.js";

function load(tag, attrs) {
  return new Promise((resolve, reject) => {
    const el = Object.assign(document.createElement(tag), attrs, { onload: resolve, onerror: reject });
    document.head.append(el);
  });
}

async function mount(el) {
  const root = siteRoot();
  const vendor = new URL("javascripts/vendor/swagger-ui/", root);
  el.textContent = "Loading the API description…";
  try {
    await Promise.all([
      load("link", { rel: "stylesheet", href: new URL("swagger-ui.css", vendor).href }),
      load("script", { src: new URL("swagger-ui-bundle.js", vendor).href }),
    ]);
  } catch {
    el.textContent = "Swagger UI did not load. The description itself is reference/openapi.json.";
    return;
  }
  el.textContent = "";
  window.SwaggerUIBundle({
    url: new URL("reference/openapi.json", root).href,
    domNode: el,
    deepLinking: true,
    docExpansion: "list",
    defaultModelsExpandDepth: 0,
    validatorUrl: null,
    persistAuthorization: true,
    showExtensions: true,   // x-planned: described ahead of the firmware
  });
}

const el = document.getElementById("swagger-ui");
if (el) mount(el);
