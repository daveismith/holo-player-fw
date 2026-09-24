// The serial side of the installer: esptool-js (vendored, see tools/vendor_js.py), wrapped into
// the few operations the page needs. ui.js imports this only when Connect is pressed.

import { ESPLoader, Transport } from "../vendor/esptool-js/bundle.js";
import {
  APP_HEADER_LENGTH, appPartitions, OTADATA_LENGTH, otadataPartition, parsePartitionTable,
  TABLE_LENGTH, TABLE_OFFSET,
} from "./inspect.js";
import { md5Hex } from "./md5.js";

const CHIP_NAME = "ESP32-S3";
const ROM_BAUD = 115200;          // the ROM bootloader, and the firmware's console
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// An error the page can explain in words, rather than print an esptool-js message.
export class InstallError extends Error {
  constructor(kind, detail) {
    super(detail ?? kind);
    this.kind = kind;
  }
}

async function sha256Hex(bytes) {
  const digest = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(digest)].map((b) => b.toString(16).padStart(2, "0")).join("");
}

// Fetch every part in the manifest and check it against the manifest's SHA-256 before anything
// is written. `base` is the manifest's own URL; part paths are relative to it.
export async function loadFirmware(manifest, base) {
  const parts = [];
  for (const part of manifest.parts) {
    const response = await fetch(new URL(part.path, base));
    if (!response.ok) throw new InstallError("download", `${part.path}: HTTP ${response.status}`);
    const data = new Uint8Array(await response.arrayBuffer());
    if (data.length !== part.size || (await sha256Hex(data)) !== part.sha256) {
      throw new InstallError("checksum", part.path);
    }
    parts.push({ ...part, data });
  }
  return parts;
}

export class Board {
  // `log(text)` receives esptool-js's own output, line by line.
  constructor(log) {
    this.log = log;
    this.port = null;
    this.transport = null;
    this.loader = null;
  }

  // Must run inside the click handler: requestPort() needs the user's gesture.
  static async choosePort() {
    try {
      return await navigator.serial.requestPort();
    } catch (error) {
      if (error.name === "NotFoundError") throw new InstallError("cancelled");
      throw error;
    }
  }

  // Reset into the ROM bootloader through the CH343P's DTR/RTS, load the flasher stub, and move
  // to `baud`. Refuses anything but an ESP32-S3 with 16 MB of flash.
  async connect(port, baud) {
    this.port = port;
    this.transport = new Transport(port, false);
    const terminal = {
      clean: () => {},
      writeLine: (line) => this.log(line),
      write: (text) => this.log(text),
    };
    this.loader = new ESPLoader({ transport: this.transport, baudrate: baud, romBaudrate: ROM_BAUD, terminal });
    try {
      await this.loader.main();
    } catch (error) {
      await this.close();
      const message = String(error?.message ?? error);
      if (/open|already open|access denied|NetworkError/i.test(message) || error?.name === "InvalidStateError") {
        throw new InstallError("busy", message);
      }
      if (/connect|sync|timeout/i.test(message)) throw new InstallError("sync", message);
      throw error;
    }
    const chip = this.loader.chip.CHIP_NAME;
    if (chip !== CHIP_NAME) {
      await this.close();
      throw new InstallError("chip", chip);
    }
    const flashSize = await this.loader.detectFlashSize();
    return { chip, flashSize };
  }

  // Read `size` bytes of flash through the stub, and wait for the MD5 it sends after the data.
  //
  // esptool-js's own readFlash returns as soon as the data is in and leaves that MD5 packet
  // unread, so the next command goes out while the board is still sending. Apple's USB serial
  // driver, which Chrome may pick for this board's CH343 bridge (cu.usbmodem…), then loses the
  // command: nothing answers, and esptool-js reports "Serial data stream stopped". Reproduced on
  // the board, 11 failures in 12 at 115200; consuming the MD5 first fixed it. It also means
  // every read is checked.
  async readFlash(addr, size) {
    const loader = this.loader;
    const transport = this.transport;
    const args = [addr, size, 0x1000, 64].map((value) => loader._intToByteArray(value));
    const packet = args.reduce((all, part) => loader._appendArray(all, part), new Uint8Array(0));
    const status = await loader.checkCommand("read flash", loader.ESP_READ_FLASH, packet);
    if (status !== 0) throw new InstallError("read", `read flash at 0x${addr.toString(16)}: status ${status}`);

    let data = new Uint8Array(0);
    while (data.length < size) {
      const chunk = await transport.read(loader.FLASH_READ_TIMEOUT);
      if (!(chunk instanceof Uint8Array)) throw new InstallError("read", `read flash: ${chunk}`);
      if (!chunk.length) continue;
      data = loader._appendArray(data, chunk);
      await transport.write(loader._intToByteArray(data.length));
    }
    if (data.length > size) data = data.subarray(0, size);

    const digest = await transport.read(3000);
    const want = [...digest].map((byte) => byte.toString(16).padStart(2, "0")).join("");
    if (digest.length !== 16 || md5Hex(data) !== want) {
      throw new InstallError("read", `read flash at 0x${addr.toString(16)}: MD5 mismatch`);
    }
    return data;
  }

  // Read what decides the installer's offer: the partition table, otadata, and the header of
  // every app partition. About 12 KB, all reads.
  async inspect() {
    const table = parsePartitionTable(await this.readFlash(TABLE_OFFSET, TABLE_LENGTH), md5Hex);
    const board = { table, otadata: null, apps: [] };
    if (!table.valid) return board;
    const ota = otadataPartition(table);
    if (ota) board.otadata = await this.readFlash(ota.offset, Math.min(ota.size, OTADATA_LENGTH));
    for (const entry of appPartitions(table)) {
      board.apps.push({ entry, bytes: await this.readFlash(entry.offset, APP_HEADER_LENGTH) });
    }
    return board;
  }

  // `progress(phase, fraction)`, phase "erase" or the part's role. The flasher stub checks each
  // part's MD5 once it is written, and esptool-js throws if it differs.
  async install(parts, erase, progress) {
    if (erase) {
      progress("erase", null);
      await this.loader.eraseFlash();
    }
    const total = parts.reduce((sum, part) => sum + part.data.length, 0);
    const before = parts.map((_, i) => parts.slice(0, i).reduce((sum, part) => sum + part.data.length, 0));
    await this.loader.writeFlash({
      fileArray: parts.map((part) => ({ data: part.data, address: part.offset })),
      flashMode: "keep",
      flashFreq: "keep",
      flashSize: "keep",
      eraseAll: false,
      compress: true,
      calculateMD5Hash: md5Hex,
      reportProgress: (index, written, size) => {
        // `written`/`size` are compressed bytes; scale to the part's share of the whole.
        const share = parts[index].data.length;
        const done = before[index] + (size ? (written / size) * share : 0);
        progress(parts[index].role, done / total);
      },
    });
    progress("done", 1);
  }

  // Close the flasher's session, reset the board into the new firmware and collect what it
  // prints for `ms` milliseconds at the console's baud rate. Then let go of the port.
  //
  // The reset is done here rather than by esptool-js so that the port is already listening at
  // 115200 when the board starts. On this board's auto-reset circuit EN is held low only while
  // RTS is asserted and DTR is not.
  async resetAndListen(ms, onText) {
    await this.close();
    const port = this.port;
    await port.open({ baudRate: ROM_BAUD });
    const decoder = new TextDecoder();
    const reader = port.readable.getReader();
    let stop = false;
    const pump = (async () => {
      try {
        while (!stop) {
          const { value, done } = await reader.read();
          if (done) break;
          if (value) onText(decoder.decode(value, { stream: true }));
        }
      } catch {
        // cancelled
      }
    })();
    await port.setSignals({ dataTerminalReady: false, requestToSend: true });
    await sleep(100);
    await port.setSignals({ dataTerminalReady: false, requestToSend: false });
    await sleep(ms);
    stop = true;
    await reader.cancel().catch(() => {});
    await pump;
    reader.releaseLock();
    await port.close().catch(() => {});
  }

  async close() {
    if (!this.transport) return;
    const transport = this.transport;
    this.transport = null;
    this.loader = null;
    await transport.disconnect().catch(() => {});
  }
}
