// Every console command the firmware registers, in the groups the Board page lists them in.
// Anything the board lists that isn't here goes under "Other". tools/check_command_docs.py checks
// that GROUPS names every registered command, so a new one can't be missed for long.
//
// Pure data and functions, no DOM.

import { hintVariants } from "./parse.js";

export const GROUPS = [
  ["Display", ["screen", "lcd"]],
  ["Images", ["image"]],
  ["Video", ["video"]],
  ["LEDs", ["leds"]],
  ["Holoprojector", ["holo", "servo_list", "servo_move", "servo_sweep", "servo_config", "servo_off", "servo_register"]],
  ["Touch and motion", ["touch", "imu"]],
  ["Files", ["fs"]],
  ["Firmware", ["version", "ota", "restart"]],
  ["System", ["help", "free", "heap", "membench", "flash-stats", "tasks", "top", "log_level", "gpio", "deep_sleep",
    "light_sleep"]],
  ["Wi-Fi", ["wifi", "wifi_save", "wifi_forget", "wifi_known", "join", "wifi_link", "wifi_ps", "wifi_txpower"]],
  ["Network", ["ip", "ping", "iperf", "traceroute", "dig"]],
  ["NVS", ["nvs_set", "nvs_get", "nvs_erase", "nvs_erase_namespace", "nvs_namespace", "nvs_list"]],
  ["I2C", ["i2cconfig", "i2cdetect", "i2cget", "i2cset", "i2cdump"]],
];

export const COMMANDS = new Set(GROUPS.flatMap(([, names]) => names));

export function groupOf(name) {
  return GROUPS.find(([, names]) => names.includes(name))?.[0] ?? "Other";
}

// `fs` lists its sub-commands in its hint but not their arguments; these are its usage text's.
const SUB_HINTS = {
  fs: {
    ls: "[path]", stat: "<path>", mkdir: "<path>", rmdir: "<path>", rm: "<path>", mv: "<from> <to>", cat: "<path>",
    hexdump: "<path> [offset] [len]", sha256: "<path>", bench: "[kb]",
  },
};

// The entries the Board page lists for a command from `help`: one a sub-command (`video play`,
// `video stop`, …), plus the command on its own when it can be run bare (`leds` reports the
// strip). Each is { label, hint, line }: the line starts the entry's input, with its required
// <placeholders> in. Sub-commands the pages won't run (fs put, ota put) are left out.
export function entries(name, hint) {
  const variants = hintVariants(hint);
  const list = [];
  if (!variants.length || !hint || hint.startsWith("[")) list.push({ label: name, hint: variants.length ? "" : hint, line: name });
  for (const variant of variants) {
    const sub = variant.label;
    const extra = SUB_HINTS[name]?.[sub];
    const args = extra ?? variant.full.slice(sub.length).trim();
    const required = extra ? extra.split(" ").filter((a) => /^<[^>]+>$/.test(a)) : [];
    const line = [name, variant.template, ...required].join(" ");
    if (refusal(line)) continue;
    list.push({ label: `${name} ${sub}`, hint: args, line });
  }
  return list;
}

// Split a command line into words, as esp_console does closely enough for these checks.
export function words(line) {
  return [...line.matchAll(/"((?:[^"\\]|\\.)*)"|(\S+)/g)].map((m) => (m[1] !== undefined ? m[1].replace(/\\(.)/g, "$1") : m[2]));
}

// A command line as written in the docs: comments ("  # …") and surrounding space removed.
export function commandLine(line) {
  return line.replace(/\s+#\s.*$/, "").trim();
}

// Whether a line of text is a board command, by its first word.
export function isCommand(line) {
  const first = words(commandLine(line))[0];
  return first !== undefined && COMMANDS.has(first);
}

// Why the pages won't run a line at all, or null. Transfers need the file manager, which speaks
// XMODEM; typed at the console they would leave the board waiting for data that never comes.
export function refusal(line) {
  const [name, sub] = words(line);
  if (name === "fs" && (sub === "put" || sub === "get")) return "transfer";
  if (name === "ota" && sub === "put") return "ota";
  return null;
}

// What running a line would change, for a confirmation, or null when it only looks.
export function danger(line) {
  const [name, sub, ...rest] = words(line);
  switch (name) {
    case "fs":
      if (sub === "rm" || sub === "rmdir") return `This deletes ${rest[0] ?? "it"} from the board.`;
      if (sub === "mv") return `This renames ${rest[0] ?? "it"} on the board.`;
      return null;
    case "restart":
      return "The board restarts: a clip or pattern stops, and the page reconnects when it is back.";
    case "deep_sleep":
    case "light_sleep":
      return "The board goes to sleep, and the console stops answering until it wakes.";
    case "gpio":
      return sub === "set" ? "This drives a pin. Check what is wired to it first." : null;
    case "nvs_set":
    case "nvs_erase":
    case "nvs_erase_namespace":
      return "This changes the settings stored on the board.";
    case "wifi_save":
    case "wifi_forget":
      return "This changes the Wi-Fi networks the board remembers.";
    case "servo_config":
      return "This saves a servo's working range on the board.";
    case "i2cset":
      return "This writes to a device on the I2C bus.";
    default:
      return null;
  }
}
