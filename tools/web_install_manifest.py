#!/usr/bin/env python3
"""Flash images for a release, and the manifest the browser installer flashes from.

A blank board needs four images, not just the application: the bootloader, the partition table,
the initial OTA data and the app. `idf.py build` lists them with their offsets in
build/flasher_args.json, and this script takes them from there rather than hardcoding either.
They are flashed as four parts, never as one merged image: a merged image fills the gap between
the partition table and otadata with 0xFF, which would wipe NVS on every update.

  --dist DIR --tag vX.Y.Z
      Copy the four images into DIR under their release names:
      holo-player-fw-vX.Y.Z.bin, -bootloader.bin, -partition-table.bin, -ota-data-initial.bin.

  --stage DIR --from DIST --tag vX.Y.Z
      Copy those images from DIST into DIR (manual/firmware/, which the docs build publishes
      as-is) with manifest.json and SHA256SUMS. The installer checks every part against the
      manifest's SHA-256 before it writes anything.

  --fixtures DIR
      Test inputs for tools/test_installer.mjs, from partitions.csv alone -- no ESP-IDF, so the
      docs workflow can run the installer's tests. When a build exists, the generated partition
      table is checked against the one ESP-IDF wrote.

Why the files are copied into the documentation at all, when the Release has them: a browser
cannot read Release downloads from another origin. github.com/.../releases/download/... and the
storage host it redirects to send no Access-Control-Allow-Origin header (RELEASING.md).

Standard library only.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import shutil
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
PROJECT = "holo-player-fw"
CHIP = "esp32s3"

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_version import TAG_RE  # noqa: E402  (same directory; one definition of a release tag)

# flasher_args.json key -> (role in the manifest, suffix of the release asset name).
PARTS = {
    "bootloader": ("bootloader", "-bootloader"),
    "partition-table": ("partition-table", "-partition-table"),
    "otadata": ("otadata", "-ota-data-initial"),
    "app": ("app", ""),
}


def asset_name(tag: str, suffix: str) -> str:
    return f"{PROJECT}-{tag}{suffix}.bin"


def load_flasher_args() -> dict:
    path = BUILD / "flasher_args.json"
    if not path.is_file():
        sys.exit(f"error: no {path.relative_to(ROOT)}; run `idf.py build` first")
    args = json.loads(path.read_text(encoding="utf-8"))
    chip = args.get("extra_esptool_args", {}).get("chip")
    if chip != CHIP:
        sys.exit(f"error: the build is for {chip!r}; the installer flashes {CHIP} only")
    covered = {int(args[key]["offset"], 16) for key in PARTS}
    listed = {int(offset, 16) for offset in args["flash_files"]}
    if covered != listed:
        extra = ", ".join(hex(offset) for offset in sorted(listed - covered))
        sys.exit(f"error: flasher_args.json flashes images the installer does not know: {extra}")
    return args


def repo_url() -> str | None:
    text = (ROOT / "mkdocs.yml").read_text(encoding="utf-8")
    match = re.search(r"^repo_url:\s*(\S+)", text, re.M)
    return match.group(1).rstrip("/") if match else None


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dist(out: Path, tag: str) -> None:
    args = load_flasher_args()
    out.mkdir(parents=True, exist_ok=True)
    for key, (_, suffix) in PARTS.items():
        source = BUILD / args[key]["file"]
        target = out / asset_name(tag, suffix)
        shutil.copyfile(source, target)
        print(f"{args[key]['offset']:>8}  {target.relative_to(ROOT) if target.is_relative_to(ROOT) else target}")


def stage(out: Path, source: Path, tag: str) -> None:
    args = load_flasher_args()
    settings = args["flash_settings"]
    release = repo_url() if TAG_RE.match(tag) else None

    for _, suffix in PARTS.values():
        if not (source / asset_name(tag, suffix)).is_file():
            sys.exit(f"error: no {source / asset_name(tag, suffix)}; run --dist first")
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    parts, sums = [], []
    for key, (role, suffix) in PARTS.items():
        name = asset_name(tag, suffix)
        shutil.copyfile(source / name, out / name)
        digest = sha256(out / name)
        part = {
            "role": role,
            "offset": int(args[key]["offset"], 16),
            "path": name,
            "size": (out / name).stat().st_size,
            "sha256": digest,
        }
        if release:
            part["release_url"] = f"{release}/releases/download/{tag}/{name}"
        parts.append(part)
        sums.append(f"{digest}  {name}\n")

    manifest = {
        "project": PROJECT,
        "version": tag,
        "chip": CHIP,
        "flash_size": settings["flash_size"],
        "flash_mode": settings["flash_mode"],
        "flash_freq": settings["flash_freq"],
        "release": f"{release}/releases/tag/{tag}" if release else None,
        "parts": sorted(parts, key=lambda part: part["offset"]),
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (out / "SHA256SUMS").write_text("".join(sums), encoding="utf-8")
    print(f"staged {tag} in {out.relative_to(ROOT) if out.is_relative_to(ROOT) else out}: "
          + ", ".join(f"{part['role']}@{part['offset']:#x}" for part in manifest["parts"]))


# --- fixtures -------------------------------------------------------------------------------
# The partition table format is ESP-IDF's (components/partition_table/gen_esp32part.py):
# 32-byte entries, then an MD5 entry over them, padded with 0xFF to 0xC00.

TYPES = {"app": 0x00, "data": 0x01}
SUBTYPES = {
    "app": {"factory": 0x00, "test": 0x20, **{f"ota_{n}": 0x10 + n for n in range(16)}},
    "data": {"ota": 0x00, "phy": 0x01, "nvs": 0x02, "coredump": 0x03, "nvs_keys": 0x04,
             "efuse": 0x05, "undefined": 0x06, "esphttpd": 0x80, "fat": 0x81, "spiffs": 0x82,
             "littlefs": 0x83},
}
TABLE_OFFSET = 0x8000
TABLE_LENGTH = 0xC00


def parse_size(text: str) -> int | None:
    text = text.strip()
    if not text:
        return None
    for unit, scale in (("K", 1024), ("M", 1024 * 1024)):
        if text.upper().endswith(unit):
            return int(text[:-1], 0) * scale
    return int(text, 0)


def partition_table(csv_path: Path) -> bytes:
    rows = [row for row in csv.reader(line for line in csv_path.read_text().splitlines()
                                      if line.strip() and not line.lstrip().startswith("#"))]
    entries = b""
    cursor = TABLE_OFFSET + 0x1000
    for row in rows:
        name, kind, subtype, offset, size, *rest = [field.strip() for field in row] + [""]
        type_id = TYPES[kind]
        subtype_id = SUBTYPES[kind][subtype]
        align = 0x10000 if type_id == TYPES["app"] else 0x1000
        start = parse_size(offset)
        if start is None:
            start = (cursor + align - 1) // align * align
        length = parse_size(size)
        flags = 1 if "encrypted" in (rest[0] if rest else "") else 0
        entries += struct.pack("<2sBBLL16sL", b"\xaa\x50", type_id, subtype_id, start, length,
                               name.encode()[:16], flags)
        cursor = start + length
    table = entries + b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(entries).digest()
    return table + b"\xff" * (TABLE_LENGTH - len(table))


def otadata(seq_by_sector: list[int | None], state: int = 0xFFFFFFFF) -> bytes:
    """Two 4 KB sectors, each an esp_ota_select_entry_t or erased. CRC as ESP-IDF's otatool."""
    out = b""
    for seq in seq_by_sector:
        sector = bytearray(b"\xff" * 0x1000)
        if seq is not None:
            crc = zlib.crc32(struct.pack("<I", seq), 0xFFFFFFFF) & 0xFFFFFFFF
            sector[:32] = struct.pack("<I20sII", seq, b"\xff" * 20, state, crc)
        out += bytes(sector)
    return out


def app_header(project: str, version: str) -> bytes:
    """The first 256 bytes of an app image: image header, first segment header, esp_app_desc_t."""
    image = struct.pack("<BBBBIBHBHB?", 0xE9, 4, 2, 0x2F, 0x40380000, 0xEE, 9, 0, 0, 0, False)
    image += b"\x00" * (24 - len(image))
    segment = struct.pack("<II", 0x3C000020, 0x100)
    desc = struct.pack("<IIII32s32s16s16s32s", 0xABCD5432, 0, 0, 0, version.encode(),
                       project.encode(), b"12:00:00", b"Sep 23 2026", b"v6.1")
    blob = image + segment + desc
    return blob + b"\x00" * (256 - len(blob))


def fixtures(out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    table = partition_table(ROOT / "partitions.csv")
    built = BUILD / "partition_table" / "partition-table.bin"
    if built.is_file():
        if built.read_bytes()[:TABLE_LENGTH].ljust(TABLE_LENGTH, b"\xff") != table:
            sys.exit(f"error: the partition table generated from partitions.csv differs from "
                     f"{built.relative_to(ROOT)}; the fixture generator is out of step with ESP-IDF")
        print(f"partition table matches {built.relative_to(ROOT)}")

    other = bytearray(table)
    # The same table with `storage` (the sixth entry) shrunk from 11M to 10M: a changed layout.
    # An entry is magic 2, type 1, subtype 1, offset 4, then size.
    struct.pack_into("<L", other, 32 * 5 + 8, 10 * 1024 * 1024)
    entries_end = table.index(b"\xeb\xeb")
    other[entries_end + 16:entries_end + 32] = hashlib.md5(bytes(other[:entries_end])).digest()

    files = {
        "partition-table.bin": table,
        "partition-table-other.bin": bytes(other),
        "erased-table.bin": b"\xff" * TABLE_LENGTH,
        "otadata-erased.bin": otadata([None, None]),
        "otadata-slot0.bin": otadata([1, None], state=2),
        "otadata-slot1.bin": otadata([1, 2], state=2),
        "otadata-bad-crc.bin": otadata([1, None])[:28] + b"\0\0\0\0" + otadata([1, None])[32:],
        "app-holo.bin": app_header(PROJECT, "v0.2.0"),
        "app-other.bin": app_header("hello_world", "v5.5-dirty"),
        "app-erased.bin": b"\xff" * 256,
    }
    for name, data in files.items():
        (out / name).write_bytes(data)
    print(f"wrote {len(files)} fixtures to {out.relative_to(ROOT) if out.is_relative_to(ROOT) else out}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--dist", type=Path, metavar="DIR", help="write release-named images")
    mode.add_argument("--stage", type=Path, metavar="DIR", help="stage images and manifest.json")
    mode.add_argument("--fixtures", type=Path, metavar="DIR", help="write the installer's test inputs")
    parser.add_argument("--from", dest="source", type=Path, metavar="DIST", help="with --stage: where --dist wrote")
    parser.add_argument("--tag", help="release tag; default: the version the build reports")
    args = parser.parse_args()

    if args.fixtures:
        fixtures(args.fixtures.resolve())
        return 0

    tag = args.tag
    if not tag:
        description = json.loads((BUILD / "project_description.json").read_text(encoding="utf-8"))
        tag = description["project_version"]
    if args.dist:
        dist(args.dist.resolve(), tag)
    else:
        if not args.source:
            parser.error("--stage needs --from")
        stage(args.stage.resolve(), args.source.resolve(), tag)
    return 0


if __name__ == "__main__":
    sys.exit(main())
