// Every console command the firmware registers, grouped as manual/reference/console.md groups
// them, and what the pages need to know before running one. tools/check_command_docs.py checks
// that GROUPS names every registered command, so a new command can't be missed here.
//
// Pure data and functions, no DOM.

export const GROUPS = [
  ["The board", ["screen", "lcd", "touch", "imu"]],
  ["Images", ["image"]],
  ["Video", ["video"]],
  ["LEDs", ["leds"]],
  ["The holoprojector", ["holo", "servo_list", "servo_move", "servo_sweep", "servo_config", "servo_off", "servo_register"]],
  ["Files and firmware", ["fs", "ota"]],
  ["System", ["help", "version", "restart", "free", "heap", "membench", "flash-stats", "tasks", "top", "log_level", "gpio",
    "deep_sleep", "light_sleep"]],
  ["Networking", ["wifi", "wifi_save", "wifi_forget", "wifi_known", "join", "wifi_link", "wifi_ps", "wifi_txpower", "ip", "ping",
    "iperf", "traceroute", "dig"]],
  ["NVS and I2C", ["nvs_set", "nvs_get", "nvs_erase", "nvs_erase_namespace", "nvs_namespace", "nvs_list", "i2cconfig",
    "i2cdetect", "i2cget", "i2cset", "i2cdump"]],
];

export const COMMANDS = new Set(GROUPS.flatMap(([, names]) => names));

export function groupOf(name) {
  return GROUPS.find(([, names]) => names.includes(name))?.[0] ?? "Other";
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
