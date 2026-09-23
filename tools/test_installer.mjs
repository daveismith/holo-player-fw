// Tests for the browser installer's board inspection (manual/javascripts/installer/inspect.js).
//
//   python3 tools/web_install_manifest.py --fixtures build/installer-fixtures
//   node --test tools/test_installer.mjs
//
// `make docs-check` does both when node is installed. The fixtures come from partitions.csv and
// ESP-IDF's formats with no ESP-IDF install, so this runs in the docs workflow; when a firmware
// build exists, the real images are checked too. Node's built-in test runner: no packages.

import { test } from "node:test";
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

import {
  appPartitions, assess, compareVersions, crc32, otadataPartition, parseAppDesc, parseOtaData,
  parsePartitionTable, tablesMatch, TABLE_LENGTH,
} from "../manual/javascripts/installer/inspect.js";
import { md5Hex } from "../manual/javascripts/installer/md5.js";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const FIXTURES = process.env.INSTALLER_FIXTURES ?? join(ROOT, "build", "installer-fixtures");
const fixture = (name) => new Uint8Array(readFileSync(join(FIXTURES, name)));

const table = () => parsePartitionTable(fixture("partition-table.bin"), md5Hex);

// A board as the installer reads it, from fixture names.
function board(tableName, otadataName, apps) {
  const parsed = parsePartitionTable(fixture(tableName), md5Hex);
  const slots = appPartitions(parsed);
  return {
    table: parsed,
    otadata: otadataName ? fixture(otadataName) : null,
    apps: slots.map((entry, i) => ({ entry, bytes: fixture(apps[i] ?? "app-erased.bin") })),
  };
}

test("md5 matches node's", () => {
  for (const length of [0, 1, 55, 56, 63, 64, 65, 1000, 70000]) {
    const bytes = new Uint8Array(length).map((_, i) => (i * 31 + length) & 0xff);
    assert.equal(md5Hex(bytes), createHash("md5").update(bytes).digest("hex"), `length ${length}`);
  }
});

test("crc32 is zlib's, as ESP-IDF's otatool uses it", () => {
  // binascii.crc32(struct.pack('<I', 1), 0xFFFFFFFF)
  assert.equal(crc32(new Uint8Array([1, 0, 0, 0]), 0xffffffff), 0x4743989a);
  assert.equal(crc32(new TextEncoder().encode("123456789")), 0xcbf43926);
});

test("partition table from partitions.csv", () => {
  const parsed = table();
  assert.ok(parsed.valid);
  assert.deepEqual(parsed.entries.map((e) => [e.label, e.offset, e.size]), [
    ["nvs", 0x9000, 20 * 1024],
    ["otadata", 0xe000, 8 * 1024],
    ["ota_0", 0x10000, 2304 * 1024],
    ["ota_1", 0x250000, 2304 * 1024],
    ["coredump", 0x490000, 64 * 1024],
    ["storage", 0x500000, 11 * 1024 * 1024],
  ]);
  assert.deepEqual(appPartitions(parsed).map((e) => e.label), ["ota_0", "ota_1"]);
  assert.equal(otadataPartition(parsed).offset, 0xe000);
});

test("an erased sector is no table; a corrupted one fails its MD5", () => {
  assert.equal(parsePartitionTable(fixture("erased-table.bin"), md5Hex).valid, false);
  const bytes = fixture("partition-table.bin");
  bytes[20] ^= 1;   // inside the first entry's label
  assert.equal(parsePartitionTable(bytes, md5Hex).valid, false);
});

test("a changed layout does not match", () => {
  const other = parsePartitionTable(fixture("partition-table-other.bin"), md5Hex);
  assert.ok(other.valid);
  assert.ok(tablesMatch(table(), table()));
  assert.equal(tablesMatch(table(), other), false);
});

test("otadata: erased, slot 0, slot 1, bad CRC", () => {
  assert.equal(parseOtaData(fixture("otadata-erased.bin"), 2).bootSlot, null);
  assert.equal(parseOtaData(fixture("otadata-slot0.bin"), 2).bootSlot, 0);
  assert.equal(parseOtaData(fixture("otadata-slot1.bin"), 2).bootSlot, 1);
  assert.equal(parseOtaData(fixture("otadata-bad-crc.bin"), 2).bootSlot, null);
});

test("app descriptor", () => {
  assert.deepEqual(
    { ...parseAppDesc(fixture("app-holo.bin")) },
    { version: "v0.2.0", project: "holo-player-fw", time: "12:00:00", date: "Sep 23 2026", idf: "v6.1" },
  );
  assert.equal(parseAppDesc(fixture("app-erased.bin")), null);
});

test("version order", () => {
  assert.equal(compareVersions("v0.2.0", "v0.2.1"), -1);
  assert.equal(compareVersions("v0.10.0", "v0.9.9"), 1);
  assert.equal(compareVersions("v0.2.0-rc1", "v0.2.0"), -1);
  assert.equal(compareVersions("v0.2.0-rc2", "v0.2.0-rc1"), 1);
  assert.equal(compareVersions("v0.2.0", "v0.2.0"), 0);
  assert.equal(compareVersions("ceb3142-dirty", "v0.2.0"), null);
});

test("blank board: fresh install, nothing to confirm", () => {
  const result = assess(board("erased-table.bin", null, []), table(), "v0.3.0");
  assert.equal(result.blank, true);
  assert.equal(result.updateBlocked, "blank");
  assert.equal(result.defaultMode, "erase");
  assert.equal(result.confirmErase, false);
});

test("Holo Player with the same layout: update by default", () => {
  const result = assess(board("partition-table.bin", "otadata-slot0.bin", ["app-holo.bin"]), table(), "v0.3.0");
  assert.equal(result.holo.version, "v0.2.0");
  assert.equal(result.holo.label, "ota_0");
  assert.equal(result.updateBlocked, null);
  assert.equal(result.defaultMode, "update");
  assert.equal(result.confirmErase, true);
  assert.equal(result.downgrade, false);
});

test("Holo Player booting ota_1 after a console update", () => {
  const result = assess(
    board("partition-table.bin", "otadata-slot1.bin", ["app-other.bin", "app-holo.bin"]), table(), "v0.3.0");
  assert.equal(result.bootSlot, 1);
  assert.equal(result.holo.label, "ota_1");
  assert.equal(result.holo.boot, true);
  assert.equal(result.defaultMode, "update");
});

test("Holo Player with a different layout: update blocked", () => {
  const result = assess(board("partition-table-other.bin", "otadata-slot0.bin", ["app-holo.bin"]), table(), "v0.3.0");
  assert.equal(result.updateBlocked, "layout");
  assert.equal(result.defaultMode, "erase");
  assert.equal(result.confirmErase, true);
});

test("other firmware: fresh install, with a confirmation", () => {
  const result = assess(board("partition-table.bin", "otadata-erased.bin", ["app-other.bin"]), table(), "v0.3.0");
  assert.equal(result.holo, null);
  assert.equal(result.foreign, true);
  assert.equal(result.updateBlocked, "other");
  assert.equal(result.defaultMode, "erase");
  assert.equal(result.confirmErase, true);
});

test("downgrade and reinstall are flagged", () => {
  const holo = board("partition-table.bin", "otadata-slot0.bin", ["app-holo.bin"]);
  assert.equal(assess(holo, table(), "v0.1.0").downgrade, true);
  assert.equal(assess(holo, table(), "v0.2.0").same, true);
});

// The real images, when a firmware build exists (locally; not in the docs workflow).
const built = join(ROOT, "build");
test("the build's own images", { skip: !existsSync(join(built, "flasher_args.json")) && "no firmware build" }, () => {
  const real = new Uint8Array(readFileSync(join(built, "partition_table", "partition-table.bin")));
  const padded = new Uint8Array(TABLE_LENGTH).fill(0xff);
  padded.set(real.subarray(0, TABLE_LENGTH));
  assert.ok(tablesMatch(parsePartitionTable(padded, md5Hex), table()));

  const app = parseAppDesc(new Uint8Array(readFileSync(join(built, "holo-player-fw.bin"))).subarray(0, 256));
  const description = JSON.parse(readFileSync(join(built, "project_description.json"), "utf8"));
  assert.equal(app.project, "holo-player-fw");
  assert.equal(app.version, description.project_version);

  const initial = new Uint8Array(readFileSync(join(built, "ota_data_initial.bin")));
  assert.equal(parseOtaData(initial, 2).bootSlot, null);
});
