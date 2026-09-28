// The board's events (GET /api/v1/events): one EventSource for the whole app, carrying the
// kinds every page following it wants. A page says which kinds it follows and how to read its
// state whole:
//
//   return ctx.events.follow({ on: { screen: (s) => ..., scene_ended: (e) => ... }, refresh, fallback: 4000 });
//
// A state kind's handler gets the resource as its GET returns it; a happening's gets the event.
// `refresh` runs whenever the stream opens -- first, and after every reconnect, since states
// that changed meanwhile aren't sent again -- so a page starts from its GETs and follows from
// there. Where there is no stream -- an older board, or one with every stream taken (503) --
// `refresh` runs every `fallback` ms instead (a number, a function returning one, or null for
// never), and the stream is tried again every 30 s. `onError` hears of a refresh or a handler
// that failed; `onLost`, of the stream dropping (it comes back by itself, and refreshes). A tab
// hidden for 30 s lets its stream go, freeing the board's connection, and opens it again when it
// comes back.

import { BASE, poll } from "./api.js";

const RETRY_MS = 30000;
const HIDDEN_MS = 30000;

export function createEvents(ctx) {
  const subs = new Set();
  let es = null;
  let key = "";
  let lastSeq = 0;            // the last happening seen: a new stream starts after it
  let refused = false;        // the board said no: poll until the retry
  let retryTimer = null;
  let hideTimer = null;

  const supported = () => typeof EventSource === "function" && (ctx.info?.features ?? []).includes("events");
  const polling = () => refused || !supported();

  function kinds() {
    const all = new Set();
    for (const s of subs) Object.keys(s.on).forEach((k) => all.add(k));
    return [...all].sort();
  }

  function close() {
    es?.close();
    es = null;
    key = "";
  }

  function dispatch(kind, message) {
    let ev;
    try { ev = JSON.parse(message.data); } catch { return; }
    if (typeof ev.seq === "number") lastSeq = ev.seq;
    const arg = "state" in ev ? ev.state : ev;
    for (const s of subs) {
      try { s.on[kind]?.(arg); } catch (e) { s.onError?.(e); }
    }
  }

  // The stream for the kinds wanted now. True when the subscribers will be refreshed when it
  // opens; false when it is open already, or there is none.
  function open() {
    const want = kinds();
    if (polling()) {
      close();
      subs.forEach((s) => s.startPoll());
      return false;
    }
    if (!want.length || document.hidden) {
      close();
      return false;
    }
    const k = want.join(",");
    if (es && key === k) return es.readyState !== EventSource.OPEN;
    close();
    key = k;
    const query = new URLSearchParams({ kinds: k });
    if (lastSeq) query.set("after", lastSeq);
    const mine = new EventSource(`${BASE}/events?${query}`);
    es = mine;
    mine.onopen = () => {
      subs.forEach((s) => { s.stopPoll(); s.reload(); });
    };
    mine.onerror = () => {
      if (es !== mine) return;
      if (mine.readyState === EventSource.CLOSED) {
        // Refused (every stream taken), or not a stream at all: poll, and try again later
        refused = true;
        clearTimeout(retryTimer);
        retryTimer = setTimeout(() => { refused = false; subs.forEach((s) => s.stopPoll()); open(); }, RETRY_MS);
        open();
      } else {
        // Dropped: EventSource comes back by itself, and asks for what it missed
        subs.forEach((s) => s.onLost?.(new Error("reconnecting")));
      }
    };
    for (const kind of want) mine.addEventListener(kind, (m) => dispatch(kind, m));
    return true;
  }

  document.addEventListener("visibilitychange", () => {
    clearTimeout(hideTimer);
    if (document.hidden) {
      hideTimer = setTimeout(() => { if (!polling()) close(); }, HIDDEN_MS);
    } else if (!es && subs.size) {
      open();
    }
  });

  return {
    follow({ on = {}, refresh, fallback = 5000, onError, onLost }) {
      const sub = {
        on,
        onError,
        onLost,
        stopPolling: null,
        reload: () => refresh().catch((e) => onError?.(e)),
        startPoll() {
          if (this.stopPolling) return;
          if (fallback === null) {
            this.reload();
            this.stopPolling = () => {};
          } else {
            this.stopPolling = poll(refresh, fallback, onError);
          }
        },
        stopPoll() {
          this.stopPolling?.();
          this.stopPolling = null;
        },
      };
      subs.add(sub);
      if (!polling() && !open()) sub.reload();
      else if (polling()) sub.startPoll();
      return () => {
        subs.delete(sub);
        sub.stopPoll();
        // After the next page has mounted: it may want the same stream
        queueMicrotask(() => { if (subs.size) open(); else close(); });
      };
    },
  };
}
