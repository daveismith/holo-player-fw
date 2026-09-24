// The board's LittleFS volume over the console: list, move, delete, and transfer files with
// XMODEM-1K, checked with SHA-256 both ways. Upload is esp-console-kit's fs_xfer.py `put`, step
// for step: the size goes with the command, so the board stores exactly that many bytes, into
// <path>.part, renamed into place only once complete.
//
// Transfers run at the console's own rate. Web Serial can only change rate by closing and
// reopening the port, and a reopen is where a board is most likely to be reset.

import { BoardError } from "./serial.js";
import { quoteArg } from "./console.js";
import { parseDf, parseLs, parseSha256 } from "./parse.js";
import { EotUnconfirmed, receive, send } from "./xmodem.js";
import { sha256Hex } from "../common/digest.js";

export function join(dir, name) {
  return dir ? `${dir.replace(/\/+$/, "")}/${name}` : name;
}

export function parent(path) {
  const at = path.replace(/\/+$/, "").lastIndexOf("/");
  return at < 0 ? "" : path.slice(0, at);
}

export class Files {
  constructor(console) {
    this.console = console;
  }

  async #run(cmd, timeout = 20000) {
    const { ok, text } = await this.console.command(cmd, { timeout });
    if (!ok) throw new BoardError("fs", text.replace(/^fs: /, ""));
    return text;
  }

  async list(dir = "") {
    return parseLs(await this.#run(dir ? `fs ls ${quoteArg(dir)}` : "fs ls"));
  }

  async usage() {
    return parseDf(await this.#run("fs df"));
  }

  async sha256(path) {
    return parseSha256(await this.#run(`fs sha256 ${quoteArg(path)}`, 120000));
  }

  mkdir(path) {
    return this.#run(`fs mkdir ${quoteArg(path)}`);
  }

  rmdir(path) {
    return this.#run(`fs rmdir ${quoteArg(path)}`);
  }

  rm(path) {
    return this.#run(`fs rm ${quoteArg(path)}`);
  }

  mv(from, to) {
    return this.#run(`fs mv ${quoteArg(from)} ${quoteArg(to)}`);
  }

  // Upload `data` to `path`. `force` replaces an existing file; without it the board refuses,
  // and this throws BoardError("exists"). Returns { sha256, received, readBack, retries, small, seconds }.
  async put(path, data, { force = false, progress, signal } = {}) {
    const local = await sha256Hex(data);
    const flags = force ? " -f" : "";
    const started = Date.now();
    let outcome;
    try {
      outcome = await this.console.transfer(`fs put${flags} ${quoteArg(path)} ${data.length}`,
        async (_ready, queue, port) => {
          try {
            return await send(queue, port, data, { progress, signal });
          } catch (error) {
            if (error instanceof EotUnconfirmed) return null;   // the board's report decides
            throw error;
          }
        });
    } catch (error) {
      if (error.report) error.message = `${error.message}; the board says: ${error.report}`;
      throw error;
    }
    if (outcome.refused) {
      if (/exists/.test(outcome.text)) throw new BoardError("exists", outcome.text);
      if (/will not fit/.test(outcome.text)) throw new BoardError("full", outcome.text);
      throw new BoardError("fs", outcome.text.replace(/^fs: /, ""));
    }
    const received = parseSha256(outcome.text);
    if (received !== local) throw new BoardError("checksum", `the board received ${received ?? "nothing it could hash"}, not ${local}`);
    const readBack = await this.sha256(path);
    if (readBack !== local) throw new BoardError("checksum", `read back from flash as ${readBack}, not ${local}`);
    return { sha256: local, received, readBack, ...(outcome.result ?? {}), seconds: (Date.now() - started) / 1000 };
  }

  // Download `path`. Returns { data, sha256 }, checked against the board's own hash of the file.
  async get(path, { progress, signal } = {}) {
    const outcome = await this.console.transfer(`fs get -s 1024 ${quoteArg(path)}`,
      async (ready, queue, port) => {
        // The board switches to the transfer 50 ms after its ready line.
        await new Promise((resolve) => setTimeout(resolve, 100));
        return receive(queue, port, Number(ready[2]), { progress, signal });
      });
    if (outcome.refused) throw new BoardError("fs", outcome.text.replace(/^fs: /, ""));
    const data = outcome.result;
    const local = await sha256Hex(data);
    const remote = await this.sha256(path);
    if (remote !== local) throw new BoardError("checksum", `downloaded ${local}, but the board has ${remote}`);
    return { data, sha256: local };
  }
}
