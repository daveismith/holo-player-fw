// The board's API (/api/v1; see /api/v1/openapi.json). The one place requests are made: it adds
// the password when there is one, asks for it on a 401, and turns error bodies into ApiError.

import { store } from "./ui.js";

export const BASE = "/api/v1";

export class ApiError extends Error {
  constructor(status, code, message) {
    super(message || `HTTP ${status}`);
    this.status = status;
    this.code = code;
  }
}

async function errorOf(res) {
  let body = null;
  try { body = await res.json(); } catch { /* not JSON */ }
  return new ApiError(res.status, body?.error ?? "http", body?.message ?? `${res.status} ${res.statusText}`);
}

// `askPassword(retry)` shows the password dialog and resolves to a password, or null to give up.
export function createApi({ askPassword }) {
  let password = store.get("holo.password");

  function headers(extra = {}) {
    return password ? { ...extra, Authorization: `Bearer ${password}` } : extra;
  }

  async function withAuth(send) {
    for (let attempt = 0; ; attempt++) {
      const res = await send();
      if (res.status !== 401) return res;
      const pw = await askPassword(attempt > 0);
      if (pw === null) throw await errorOf(res);
      password = pw;
      store.set("holo.password", pw);
    }
  }

  async function request(method, path, json) {
    const res = await withAuth(() => fetch(BASE + path, {
      method,
      cache: "no-store",
      headers: headers(json === undefined ? {} : { "Content-Type": "application/json" }),
      body: json === undefined ? undefined : JSON.stringify(json),
    }));
    if (!res.ok) throw await errorOf(res);
    return res.status === 204 ? null : res.json();
  }

  // PUT a body with XMLHttpRequest, which reports upload progress (fetch does not).
  function sendBody(path, blob, query, onProgress) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      xhr.open("PUT", `${BASE}${path}${query ? `?${new URLSearchParams(query)}` : ""}`);
      xhr.setRequestHeader("Content-Type", "application/octet-stream");
      if (password) xhr.setRequestHeader("Authorization", `Bearer ${password}`);
      xhr.upload.onprogress = (e) => onProgress?.(e.loaded, e.total || blob.size);
      xhr.onload = () => resolve({ status: xhr.status, text: xhr.responseText });
      xhr.onerror = () => reject(new ApiError(0, "network", "the connection to the board was lost"));
      xhr.send(blob);
    });
  }

  // PUT `blob` to `path`, reporting progress; resolves to the reply. A 401 asks for the password
  // and sends it again, from the start: the board read none of it.
  async function uploadTo(path, blob, query, onProgress) {
    for (let attempt = 0; ; attempt++) {
      const res = await sendBody(path, blob, query, onProgress);
      let body = null;
      try { body = JSON.parse(res.text); } catch { /* not JSON */ }
      if (res.status === 401) {
        const pw = await askPassword(attempt > 0);
        if (pw === null) throw new ApiError(401, "auth_required", body?.message);
        password = pw;
        store.set("holo.password", pw);
        continue;
      }
      if (res.status < 200 || res.status >= 300) {
        throw new ApiError(res.status, body?.error ?? "http", body?.message ?? `HTTP ${res.status}`);
      }
      return body;
    }
  }

  return {
    get: (path) => request("GET", path),
    post: (path, json = {}) => request("POST", path, json),
    put: (path, json = {}) => request("PUT", path, json),
    patch: (path, json) => request("PATCH", path, json),
    del: (path) => request("DELETE", path),

    // A firmware image: resolves to the session once it is staged.
    upload: (blob, query, onProgress) => uploadTo("/ota/image", blob, query, onProgress),
    // A file onto the volume: resolves to its entry.
    uploadFile: (path, blob, query, onProgress) => uploadTo("/fs/file", blob, { path, ...query }, onProgress),

    // A file from the volume, as a Blob: fetched with the password (a plain link would not carry it).
    async download(path) {
      const res = await withAuth(() => fetch(`${BASE}/fs/file?${new URLSearchParams({ path })}`, { cache: "no-store", headers: headers() }));
      if (!res.ok) throw await errorOf(res);
      return res.blob();
    },

    hasPassword: () => Boolean(password),
    forgetPassword() { password = null; store.set("holo.password", null); },
    // A password this page has just set on the board: used from now on, rather than asked for.
    usePassword(pw) { password = pw; store.set("holo.password", pw); },
  };
}

// Call `fn` every `ms` until the returned stop() is called; errors go to `onError`.
export function poll(fn, ms, onError) {
  let timer = null;
  let stopped = false;
  const tick = async () => {
    try { await fn(); } catch (e) { onError?.(e); }
    if (!stopped) timer = setTimeout(tick, typeof ms === "function" ? ms() : ms);
  };
  tick();
  return () => { stopped = true; clearTimeout(timer); };
}
