// What the board's commands print, as data. Pure functions: tools/test_board.mjs runs them against
// output captured from a real board (tools/fixtures/board/). The formats are esp-console-kit's
// (cmd_fs.c, cmd_ota.c, cmd_system) and esp_console's own `help`.

// `fs ls [dir]`:
//         3260  calibration.png
//        <dir>  clips/
//   2 files, 3440 bytes; 1 directory
export function parseLs(text) {
  const entries = [];
  for (const line of text.split("\n")) {
    const match = /^\s*(<dir>|-?\d+)  (.+)$/.exec(line.replace(/\r$/, ""));
    if (!match) continue;
    if (match[1] === "<dir>") entries.push({ name: match[2].replace(/\/$/, ""), dir: true, size: null });
    else entries.push({ name: match[2], dir: false, size: Number(match[1]) < 0 ? null : Number(match[1]) });
  }
  entries.sort((a, b) => (a.dir === b.dir ? a.name.localeCompare(b.name) : a.dir ? -1 : 1));
  return entries;
}

// `fs df`:  /data: 496 KB used of 11264 KB, 10768 KB free
export function parseDf(text) {
  const match = /^(\S+): (\d+) KB used of (\d+) KB, (\d+) KB free/m.exec(text);
  if (!match) return null;
  return { mount: match[1], usedKB: Number(match[2]), totalKB: Number(match[3]), freeKB: Number(match[4]) };
}

// `fs sha256 <path>`:  <64 hex>  /data/<path>
export function parseSha256(text) {
  return /\b([0-9a-f]{64})\b/.exec(text)?.[1] ?? null;
}

// `ota` / `ota status`, one line a slot, columns two or more spaces apart:
//   ota_0    0x010000  2304 KB  running, boots  holo-player-fw 5c48843, built Sep 23 2026 23:16:59, confirmed
//   ota_1    0x250000  2304 KB  next update     empty
export function parseOta(text) {
  const slots = [];
  for (const line of text.split("\n")) {
    if (!/^ota_\d+\s/.test(line)) continue;
    const [label, offset, size, state = "", image = ""] = line.trim().split(/\s{2,}/);
    const app = /^(\S+) (\S+), built (.+?)(?:, (.*))?$/.exec(image);
    slots.push({
      label,
      offset: Number.parseInt(offset, 16),
      sizeKB: Number.parseInt(size, 10),
      state,
      running: /\brunning\b/.test(state),
      boots: /\bboots\b/.test(state),
      empty: image === "empty" || !app,
      project: app?.[1] ?? null,
      version: app?.[2] ?? null,
      built: app?.[3] ?? null,
      note: app?.[4] ?? null,
    });
  }
  return slots;
}

// `version`:
//   App:holo-player-fw 5c48843
//   	built:Sep 23 2026 23:16:59
//   IDF Version:v6.1
//   	model:ESP32-S3
export function parseVersion(text) {
  const app = /^App:(\S+) (\S+)/m.exec(text);
  return {
    project: app?.[1] ?? null,
    version: app?.[2] ?? null,
    built: /built:(.+)$/m.exec(text)?.[1].trim() ?? null,
    idf: /IDF Version:(\S+)/.exec(text)?.[1] ?? null,
    chip: /model:(\S+)/.exec(text)?.[1] ?? null,
  };
}

// `help`: a block a command, blank lines between.
//   name  hint
//     Description, wrapped
//     over lines.
//         <arg>  what the argument is
export function parseHelp(text) {
  const commands = [];
  for (const block of text.replace(/\r/g, "").split(/\n\s*\n/)) {
    const lines = block.split("\n").filter((line) => line.trim());
    if (!lines.length) continue;
    const head = /^(\S+) ?(.*)$/.exec(lines[0]);
    if (!head || /^\s/.test(lines[0])) continue;
    const body = lines.slice(1);
    // The description is the run of lines indented by exactly two spaces that don't start an
    // argument's entry; what follows is argtable's glossary of arguments.
    const description = [];
    let i = 0;
    for (; i < body.length; i++) {
      const line = body[i];
      if (!/^ {2}\S/.test(line) || /^ {2}(-|<)/.test(line)) break;
      description.push(line.trim());
    }
    commands.push({
      name: head[1],
      hint: head[2].trim(),
      description: description.join(" "),
      glossary: body.slice(i).join("\n"),
    });
  }
  return commands;
}

// --- hints into something to click -----------------------------------------------------------

// Split at `sep` where it is not inside brackets.
function splitTop(text, sep) {
  const parts = [];
  let depth = 0;
  let start = 0;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (c === "[" || c === "<" || c === "(") depth++;
    else if (c === "]" || c === ">" || c === ")") depth = Math.max(0, depth - 1);
    else if (depth === 0 && text.startsWith(sep, i)) {
      parts.push(text.slice(start, i));
      start = i + sep.length;
      i += sep.length - 1;
    }
  }
  parts.push(text.slice(start));
  return parts.map((part) => part.trim()).filter(Boolean);
}

// The bracket that closes the one at `open`, or -1.
function closing(text, open) {
  let depth = 0;
  for (let i = open; i < text.length; i++) {
    if (text[i] === "[") depth++;
    else if (text[i] === "]" && --depth === 0) return i;
  }
  return -1;
}

const WORD = /^[a-z][\w-]*$/i;

// The ways to call a command, from its hint, as { label, template }: `video`'s
// "play <file> [loop] [frame] | stop | status | …" gives play → "play <file>", stop → "stop", and
// so on. A template keeps the required <placeholders> for the user to fill in, and drops what is
// optional. Hints made only of options (-t <n>, --io=<n>) give none: those take free text.
export function hintVariants(hint) {
  let text = hint.trim();
  if (text.startsWith("[") && closing(text, 0) === text.length - 1) text = text.slice(1, -1).trim();
  let alternatives = splitTop(text, " | ");
  if (alternatives.length === 1) {
    // "on|off|status", or "ls|df|…|bench ...": bare words run together.
    const words = splitTop(text.split(" ")[0], "|");
    if (words.length > 1 && words.every((word) => WORD.test(word))) {
      const rest = text.slice(text.split(" ")[0].length).trim();
      alternatives = words.map((word) => (rest && rest !== "..." ? `${word} ${rest}` : word));
    } else {
      alternatives = [text];
    }
  }
  const variants = [];
  for (const alternative of alternatives) {
    const tokens = splitTop(alternative, " ");
    const words = [];
    let i = 0;
    for (; i < tokens.length && WORD.test(tokens[i]); i++) words.push(tokens[i]);
    if (!words.length) continue;
    const required = tokens.slice(i).filter((token) => /^<[^>]+>$/.test(token));
    variants.push({ label: words.join(" "), template: [...words, ...required].join(" "), full: alternative });
  }
  return variants;
}

// The <placeholders> left in a command line, with where they are.
export function placeholders(line) {
  return [...line.matchAll(/<[^<>\s]+>/g)].map((m) => ({ text: m[0], start: m.index, end: m.index + m[0].length }));
}
