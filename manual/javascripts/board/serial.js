// The board's console port over Web Serial: opened without resetting the board, with one reader
// that hands every byte to whoever holds the port -- the console, or a file transfer.
//
// No DOM here, and nothing but the Web Serial API itself, so tools/test_board.mjs drives it with a
// fake port under Node.

export const CONSOLE_BAUD = 115200;

// The Waveshare ESP32-S3-Touch-LCD-1.28's CH343P bridge.
export const BRIDGE = { usbVendorId: 0x1a86, usbProductId: 0x55d3 };

// An error the pages can explain in words.
export class BoardError extends Error {
  constructor(kind, detail) {
    super(detail ?? kind);
    this.kind = kind;
  }
}

// Bytes waiting to be read, for code that wants them in order and with timeouts (XMODEM).
export class ByteQueue {
  constructor() {
    this.bytes = new Uint8Array(0);
    this.wake = null;
    this.closed = false;
  }

  push(chunk) {
    const joined = new Uint8Array(this.bytes.length + chunk.length);
    joined.set(this.bytes);
    joined.set(chunk, this.bytes.length);
    this.bytes = joined;
    this.#notify();
  }

  close() {
    this.closed = true;
    this.#notify();
  }

  clear() {
    this.bytes = new Uint8Array(0);
  }

  #notify() {
    const wake = this.wake;
    this.wake = null;
    wake?.();
  }

  #wait(ms) {
    return new Promise((resolve) => {
      const timer = setTimeout(() => { this.wake = null; resolve(); }, ms);
      this.wake = () => { clearTimeout(timer); resolve(); };
    });
  }

  // Up to `n` bytes, as soon as there are `n` or the time is up: fewer (possibly none) on timeout.
  async read(n, ms) {
    const end = Date.now() + ms;
    while (this.bytes.length < n && !this.closed) {
      const left = end - Date.now();
      if (left <= 0) break;
      await this.#wait(left);
    }
    if (this.closed && !this.bytes.length) throw new BoardError("lost", "the port closed");
    const out = this.bytes.slice(0, n);
    this.bytes = this.bytes.slice(out.length);
    return out;
  }

  async readByte(ms) {
    const got = await this.read(1, ms);
    return got.length ? got[0] : null;
  }
}

export class BoardPort {
  // `port` is a Web Serial SerialPort.
  constructor(port) {
    this.port = port;
    this.sink = null;          // (Uint8Array) => void: where incoming bytes go
    this.onLost = null;        // () => void: the port went away without close()
    this.writer = null;
    this.reader = null;
    this.pump = null;
    this.closing = false;
    this.isOpen = false;
  }

  static supported() {
    return typeof navigator !== "undefined" && "serial" in navigator && globalThis.isSecureContext !== false;
  }

  // Ask the user for a port. Must run inside a click handler: requestPort() needs the gesture.
  static async choose() {
    try {
      return new BoardPort(await navigator.serial.requestPort());
    } catch (error) {
      if (error.name === "NotFoundError") throw new BoardError("cancelled");
      throw error;
    }
  }

  // A port the user has already granted to this site, without asking again: the board's bridge if
  // there is one, otherwise the only granted port. Null when there is nothing to reuse.
  static async reuse() {
    const ports = await navigator.serial.getPorts();
    const bridge = ports.filter((port) => {
      const info = port.getInfo();
      return info.usbVendorId === BRIDGE.usbVendorId && info.usbProductId === BRIDGE.usbProductId;
    });
    const port = bridge[0] ?? (ports.length === 1 ? ports[0] : null);
    return port ? new BoardPort(port) : null;
  }

  // Open at the console's rate without resetting the board.
  //
  // On this board's auto-reset circuit EN is pulled low only while RTS is asserted and DTR is not.
  // Opening asserts both, which drives neither pin; dropping RTS first only ever touches GPIO0,
  // which is harmless while the application runs. The same order as esp-console-kit's fs_xfer.py.
  async open(baud = CONSOLE_BAUD) {
    try {
      await this.port.open({ baudRate: baud, bufferSize: 64 * 1024 });
    } catch (error) {
      if (error.name === "InvalidStateError" || error.name === "NetworkError" || /open/i.test(error.message)) {
        throw new BoardError("busy", error.message);
      }
      throw error;
    }
    try {
      await this.port.setSignals({ requestToSend: false });
      await this.port.setSignals({ dataTerminalReady: false });
    } catch {
      // Not every bridge has the control lines; nothing to protect then.
    }
    this.closing = false;
    this.isOpen = true;
    this.writer = this.port.writable.getWriter();
    this.pump = this.#pump();
  }

  async #pump() {
    while (!this.closing && this.port.readable) {
      this.reader = this.port.readable.getReader();
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
          if (value?.length) this.sink?.(value);
        }
      } catch (error) {
        // A framing, parity or overrun error gives a fresh readable; a lost device does not.
        if (error?.name === "NetworkError") break;
      } finally {
        this.reader.releaseLock();
        this.reader = null;
      }
    }
    if (!this.closing) {
      this.isOpen = false;
      this.onLost?.();
    }
  }

  async write(bytes) {
    if (!this.writer) throw new BoardError("lost", "the port is closed");
    try {
      await this.writer.write(bytes);
    } catch (error) {
      throw new BoardError("lost", error.message);
    }
  }

  info() {
    return this.port.getInfo?.() ?? {};
  }

  async close() {
    if (!this.isOpen && !this.pump) return;
    this.closing = true;
    this.isOpen = false;
    await this.reader?.cancel().catch(() => {});
    await this.pump?.catch(() => {});
    this.pump = null;
    try {
      this.writer?.releaseLock();
    } catch {
      // already released
    }
    this.writer = null;
    await this.port.close().catch(() => {});
  }
}
