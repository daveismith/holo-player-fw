#!/usr/bin/env python3
"""Talk to a board's host link from a computer, through a USB-serial (or USB-RS485) adapter.

JSON lines -- the board's HTTP API, one request a line:

    tools/hostlink.py /dev/cu.usbserial-X GET /leds
    tools/hostlink.py /dev/cu.usbserial-X PATCH /leds '{"mode":"solid","colour":"orange"}'
    tools/hostlink.py /dev/cu.usbserial-X --address 3 POST /scenes/apply '{"name":"cantina"}'

Native frames -- the small binary messages (see manual/reference/host-protocol.md):

    tools/hostlink.py /dev/cu.usbserial-X --native ping
    tools/hostlink.py /dev/cu.usbserial-X --native status
    tools/hostlink.py /dev/cu.usbserial-X --native scene 3
    tools/hostlink.py /dev/cu.usbserial-X --native leds solid 255 128 0 30
    tools/hostlink.py /dev/cu.usbserial-X --native holo nod

Events -- ask for them, then print them as they come, until Ctrl-C:

    tools/hostlink.py /dev/cu.usbserial-X --listen scene_ended,clip_ended,leds

Needs pyserial (`pip install pyserial`).
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("hostlink.py needs pyserial: pip install pyserial")

PROTOCOL = 1

# ---------------------------------------------------------------- native frames

TYPES = {"ping": 0x01, "version": 0x02, "status": 0x03, "scene": 0x10, "end": 0x11, "screen-off": 0x20,
         "colour": 0x21, "backlight": 0x22, "leds": 0x30, "holo": 0x40, "events-set": 0x50, "events-get": 0x51,
         "restart": 0x7F}
STATUS = ["ok", "bad request", "not found", "not playable", "not ready", "busy", "failed", "unknown type"]
LED_MODES = ["off", "solid", "wipe", "rainbow", "flicker"]
MOTIONS = ["center", "move", "nudge", "twitch", "wag", "nod", "scan", "circle", "stop", "off"]
SHOWING = ["nothing", "colour", "calibration", "image", "clip"]
HOLO = ["hold", "move", "twitch", "wag", "nod", "scan", "circle"]
KINDS = {1: "clip_ended", 2: "scene_ended", 3: "touch", 4: "clip_started", 5: "scene_started", 15: "ready"}


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE"""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc


def cobs_encode(data: bytes) -> bytes:
    out, block = bytearray(), bytearray()
    for b in data:
        if b == 0:
            out += bytes([len(block) + 1]) + block
            block = bytearray()
        else:
            block.append(b)
            if len(block) == 254:
                out += bytes([255]) + block
                block = bytearray()
    return bytes(out + bytes([len(block) + 1]) + block)


def cobs_decode(data: bytes) -> bytes:
    out, i = bytearray(), 0
    while i < len(data):
        code = data[i]
        if code == 0:
            raise ValueError("a zero inside a frame")
        out += data[i + 1:i + code]
        i += code
        if code < 255 and i < len(data):
            out.append(0)
    return bytes(out)


def frame(addr: int, seq: int, type_: int, payload: bytes = b"") -> bytes:
    body = bytes([addr, seq, type_]) + payload
    return b"\x00" + cobs_encode(body + struct.pack("<H", crc16(body))) + b"\x00"


def event_record(r: bytes) -> dict:
    kind = KINDS.get(r[0], f"kind {r[0]}")
    e = {"event": kind, "seq": r[1] | r[2] << 8}
    if kind == "clip_ended":
        e["finished"] = bool(r[3])
    elif kind == "scene_ended":
        e.update(slot=r[3] or None, then=["stay", "restore", "off"][r[4]] if r[4] < 3 else r[4],
                 by=["clip", "time", "end", "replaced"][r[5]] if r[5] < 4 else r[5])
    elif kind == "clip_started":
        e.update(loop=bool(r[3]), plays=r[4], frames=r[5] | r[6] << 8, fps=r[7])
    elif kind == "scene_started":
        e.update(slot=r[3] or None, then=["stay", "restore", "off"][r[4]] if r[4] < 3 else r[4],
                 until_clip_ends=bool(r[5]), duration_s=(r[6] | r[7] << 8) or None)
    elif kind == "touch":
        e.update(action="up" if r[3] else "down", x=r[4], y=r[5])
    elif kind == "ready":
        e.update(protocol=r[3], address=r[4])
    return e


def describe(type_: int, payload: bytes) -> dict:
    """A reply's payload, as words"""
    if type_ == 0x70:
        return event_record(payload)
    st, pending, data = payload[0], payload[1], payload[2:]
    out = {"status": STATUS[st] if st < len(STATUS) else st, "pending": pending}
    t = type_ & 0x7F
    if st != 0:
        return out
    if t == 0x01:
        out.update(protocol=data[0], uptime_s=struct.unpack("<I", data[1:5])[0])
    elif t == 0x02:
        out["version"] = data.decode(errors="replace")
    elif t == 0x03:
        b = data
        out.update(showing=SHOWING[b[0]] if b[0] < 5 else b[0], scene_slot=None if b[1] == 0 else b[1],
                   remaining_s=None if b[2] | b[3] << 8 == 0xFFFF else (b[2] | b[3] << 8) / 10,
                   holo="can't move" if b[4] == 0xFF else HOLO[b[4]], leds=LED_MODES[b[5]] if b[5] < 5 else b[5],
                   backlight=b[6], brightness=b[7], seq=b[8] | b[9] << 8, last_status=b[10],
                   touch=bool(b[11] & 1), holo_ready=bool(b[11] & 2))
    elif t == 0x50:
        out["seq"] = data[0] | data[1] << 8
    elif t == 0x51:
        out.update(lost=data[0], events=[event_record(data[2 + 8 * i:10 + 8 * i]) for i in range(data[1])])
    return out


def native_payload(words: list[str]) -> tuple[int, bytes]:
    name, args = words[0], words[1:]
    if name not in TYPES:
        sys.exit(f"no native message {name!r}: {', '.join(TYPES)}")
    t = TYPES[name]
    num = [int(a, 0) for a in args if a.lstrip("-").isdigit() or a.startswith("0x")]
    if name == "scene":
        return t, bytes([num[0]])
    if name == "colour":
        return t, bytes(num[:3])
    if name == "backlight":
        return t, bytes([num[0]])
    if name == "leds":
        # leds <mode> [r g b] [brightness]
        mode = LED_MODES.index(args[0]) if args[0] in LED_MODES else 0xFF
        rgb = num[:3] if len(num) >= 3 else [0, 0, 0]
        bright = num[3] if len(num) >= 4 else 0
        flags = (2 if len(num) >= 3 else 0) | (1 if "loop" in args else 0)
        return t, bytes([mode, *rgb, bright, flags])
    if name == "holo":
        # holo <motion> [x y] [duration_ms] [range] [count]
        m = MOTIONS.index(args[0])
        x, y = (num[0], num[1]) if len(num) >= 2 else (0, 0)
        return t, struct.pack("<BbbHBB", m, x, y, num[2] if len(num) > 2 else 0,
                              num[3] if len(num) > 3 else 0, num[4] if len(num) > 4 else 0)
    if name == "events-set":
        # events-set <mask> [push]: bit 0 clip_ended, 1 scene_ended, 2 touch, 3 clip_started, 4 scene_started
        return t, bytes([num[0], 1 if "push" in args else 0])
    if name == "events-get":
        return t, struct.pack("<H", num[0] if num else 0)
    return t, b""


def read_frames(port: serial.Serial, until: float):
    """Frames as they arrive, decoded: (addr, seq, type, payload)"""
    buf = bytearray()
    in_frame = False
    while time.time() < until:
        b = port.read(1)
        if not b:
            continue
        if b == b"\x00":
            if in_frame and buf:
                try:
                    body = cobs_decode(bytes(buf))
                except ValueError:
                    body = b""
                if len(body) >= 5 and crc16(body[:-2]) == struct.unpack("<H", body[-2:])[0]:
                    yield body[0], body[1], body[2], body[3:-2]
                buf.clear()
                in_frame = False
            else:
                in_frame = True
                buf.clear()
        elif in_frame:
            buf += b


# ---------------------------------------------------------------- main

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("port")
    parser.add_argument("words", nargs="*", help="METHOD PATH [JSON], or with --native a message")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--address", type=int, help="the board's address (needed on RS485)")
    parser.add_argument("--native", action="store_true", help="send a native frame")
    parser.add_argument("--listen", metavar="KINDS", help="ask for these events, pushed, and print them")
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()
    port = serial.Serial(args.port, args.baud, timeout=0.05)
    at = f"@{args.address} " if args.address else ""

    if args.listen is not None:
        kinds = [k for k in args.listen.split(",") if k]
        port.write(f'{at}PATCH /link/events {json.dumps({"kinds": kinds, "push": True})}\n'.encode())
        try:
            while True:
                line = port.readline().decode(errors="replace").strip()
                if line:
                    print(line, flush=True)
        except KeyboardInterrupt:
            port.write(f'{at}PATCH /link/events {{"kinds":[]}}\n'.encode())
            return 0

    if not args.words:
        parser.error("say what to send")

    if args.native:
        t, payload = native_payload(args.words)
        port.write(frame(args.address or 0, 1, t, payload))
        for addr, seq, type_, data in read_frames(port, time.time() + args.timeout):
            what = describe(type_, data)
            print(json.dumps(what))
            if type_ == (t | 0x80):
                return 0 if what.get("status") == "ok" else 1
        print("no reply", file=sys.stderr)
        return 2

    method, path, *body = args.words
    port.write(f"{at}#h {method.upper()} {path}{' ' + body[0] if body else ''}\n".encode())
    end = time.time() + args.timeout
    while time.time() < end:
        line = port.readline().decode(errors="replace").strip()
        if not line:
            continue
        if line.startswith("!"):
            print(line, file=sys.stderr)        # an event on its way
            continue
        rest = line.split(" ", 1)[1] if line.startswith("@") else line
        if not rest.startswith("#h "):
            continue
        status, _, rest = rest[3:].partition(" ")
        if rest.startswith("+"):
            pending, _, rest = rest.partition(" ")
            print(f"({pending[1:]} events waiting)", file=sys.stderr)
        print(f"{status} {rest}".strip())
        return 0 if status.startswith("2") else 1
    print("no reply", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
