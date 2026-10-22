// XMODEM-1K with CRC-16, both ways: a port of xmodem_send and xmodem_receive in esp-console-kit's
// tools/fs_xfer.py, the host side of the board's `fs put`, `fs get` and `ota put`.
//
// `queue` is where the port's bytes arrive while the transfer holds it (a ByteQueue), and `port`
// has write(bytes). No DOM; tools/test_board.mjs runs both directions against each other.

import { BoardError } from "./serial.js";

export const SOH = 0x01, STX = 0x02, EOT = 0x04, ACK = 0x06, NAK = 0x15, CAN = 0x18, SUB = 0x1a;
const C = 0x43;

export class XmodemError extends BoardError {
  constructor(detail) {
    super("xmodem", detail);
  }
}

// Every block was acknowledged but the end of transfer was not: the ACK may have been lost on the
// way back, so the board's own report has the last word.
export class EotUnconfirmed extends XmodemError {}

// CRC-16/XMODEM (poly 0x1021, initial 0), as Python's binascii.crc_hqx(data, 0).
export function crc16(bytes) {
  let crc = 0;
  for (const byte of bytes) {
    crc ^= byte << 8;
    for (let i = 0; i < 8; i++) crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
  }
  return crc;
}

// One block: header, number, its complement, the payload padded with SUB, and the CRC. A block is
// 1024 bytes unless `small`, or unless what is left fits in 128.
export function frame(num, chunk, small = false) {
  const size = small || chunk.length <= 128 ? 128 : 1024;
  const out = new Uint8Array(3 + size + 2);
  out[0] = size === 1024 ? STX : SOH;
  out[1] = num & 0xff;
  out[2] = 0xff - (num & 0xff);
  out.fill(SUB, 3, 3 + size);
  out.set(chunk.subarray(0, size), 3);
  const crc = crc16(out.subarray(3, 3 + size));
  out[3 + size] = crc >> 8;
  out[4 + size] = crc & 0xff;
  return out;
}

function cancelled(signal) {
  if (signal?.aborted) throw new XmodemError("cancelled");
}

async function cancel(port) {
  await port.write(new Uint8Array([CAN, CAN, CAN])).catch(() => {});
}

// The next ACK, NAK or CAN, or null on timeout. Anything else read meanwhile is skipped.
async function waitReply(queue, ms) {
  const end = Date.now() + ms;
  for (;;) {
    const left = end - Date.now();
    if (left <= 0) return null;
    const byte = await queue.readByte(Math.min(left, 200));
    if (byte === ACK || byte === NAK || byte === CAN) return byte;
  }
}

// Send `data`. Returns { retries, small }: the number of resent blocks, and whether it fell back
// to 128-byte blocks. `progress(done, total)` after each block.
//
// macOS offers this board's CH343 bridge twice, and with Apple's driver (cu.usbmodem…) a
// 1024-byte block reaches the board whole but with its payload changed, every time, while 128-byte
// blocks arrive intact; WCH's driver (cu.wchusbserial…) carries either. So after two bad 1K
// blocks in a row the rest goes as 128-byte blocks, which XMODEM-1K allows at any point.
//
// The queue is the transfer's own, begun right after the board's ready line, so nothing in it is
// stale: the board's first "C" may already be there.
export async function send(queue, port, data, { progress, signal } = {}) {
  const start = Date.now();
  for (;;) {
    cancelled(signal);
    const byte = await queue.readByte(1000);
    if (byte === C) break;
    if (byte === CAN && (await queue.readByte(1000)) === CAN) throw new XmodemError("the board cancelled before the first block");
    if (Date.now() - start > 60000) throw new XmodemError("the board never asked for data");
  }

  let num = 1;
  let retries = 0;
  let small = false;
  let offset = 0;
  while (offset < data.length) {
    let attempt = 0;
    let naks = 0;
    for (;;) {
      if (signal?.aborted) {
        await cancel(port);
        throw new XmodemError("cancelled");
      }
      const size = small ? 128 : 1024;
      await port.write(frame(num, data.subarray(offset, offset + size), small));
      const reply = await waitReply(queue, 10000);
      if (reply === ACK) {
        offset = Math.min(offset + size, data.length);
        break;
      }
      if (reply === CAN) throw new XmodemError(`the board cancelled at block ${num}`);
      retries++;
      if (reply === NAK && !small && ++naks >= 2) small = true;
      if (++attempt > 10) {
        await cancel(port);
        throw new XmodemError(`block ${num} was never acknowledged`);
      }
    }
    num++;
    progress?.(offset, data.length);
  }

  // Briefly: the board answers repeats for 2 s and then goes back to the console.
  for (let attempt = 0; attempt < 3; attempt++) {
    await port.write(new Uint8Array([EOT]));
    if ((await waitReply(queue, 600)) === ACK) return { retries, small };
  }
  throw new EotUnconfirmed("end of transfer was never acknowledged");
}

// Receive exactly `size` bytes. `progress(done, total)` after each block.
export async function receive(queue, port, size, { progress, signal } = {}) {
  const chunks = [];
  let received = 0;
  let expected = 1;
  let errors = 0;

  const start = Date.now();
  let header = null;
  while (header === null) {
    cancelled(signal);
    await port.write(new Uint8Array([C]));
    const byte = await queue.readByte(1000);
    if (byte === SOH || byte === STX || byte === EOT) header = byte;
    else if (Date.now() - start > 60000) throw new XmodemError("the board never started sending");
  }

  for (;;) {
    if (signal?.aborted) {
      await cancel(port);
      throw new XmodemError("cancelled");
    }
    if (header === EOT) {
      await port.write(new Uint8Array([ACK]));
      break;
    }
    if (header === CAN) throw new XmodemError("the board cancelled");
    if (header === SOH || header === STX) {
      const n = header === STX ? 1024 : 128;
      const body = await queue.read(2 + n + 2, 2000);
      const good = body.length === 2 + n + 2 && body[0] === 0xff - body[1]
        && crc16(body.subarray(2, 2 + n)) === ((body[2 + n] << 8) | body[3 + n]);
      if (good) {
        if (body[0] === (expected & 0xff)) {
          chunks.push(body.slice(2, 2 + n));
          received += n;
          expected++;
          errors = 0;
          await port.write(new Uint8Array([ACK]));
          progress?.(Math.min(received, size), size);
        } else if (body[0] === ((expected - 1) & 0xff)) {
          await port.write(new Uint8Array([ACK]));   // a repeat of the last block: its ACK was lost
        } else {
          await cancel(port);
          throw new XmodemError("blocks out of sequence");
        }
      } else {
        if (++errors > 10) {
          await cancel(port);
          throw new XmodemError("too many bad blocks");
        }
        while ((await queue.read(256, 100)).length) { /* let the line go quiet */ }
        await port.write(new Uint8Array([NAK]));
      }
    }
    let next = await queue.readByte(10000);
    if (next === null) {
      errors++;
      await port.write(new Uint8Array([NAK]));
      next = await queue.readByte(10000);
      if (next === null) throw new XmodemError("the board went quiet");
    }
    header = next;
  }

  if (received < size) throw new XmodemError(`only ${received} of ${size} bytes arrived`);
  const out = new Uint8Array(size);
  let at = 0;
  for (const chunk of chunks) {
    out.set(chunk.subarray(0, Math.min(chunk.length, size - at)), at);
    at += chunk.length;
    if (at >= size) break;
  }
  return out;
}
