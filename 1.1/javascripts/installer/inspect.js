// What is on the board, and what the installer should offer to do about it.
//
// Pure functions over bytes read from flash: no DOM, no serial port, so tools/test_installer.mjs
// runs them under Node. The formats are ESP-IDF's:
//   partition table  components/partition_table/gen_esp32part.py
//   otadata          esp_ota_select_entry_t, components/bootloader_support/include/esp_flash_partitions.h
//   app descriptor   esp_app_desc_t, components/esp_app_format/include/esp_app_desc.h

export const PROJECT = "holo-player-fw";

export const TABLE_OFFSET = 0x8000;
export const TABLE_LENGTH = 0xc00;
export const OTADATA_LENGTH = 0x2000;   // two 4 KB sectors, one esp_ota_select_entry_t each
export const APP_HEADER_LENGTH = 256;   // image header, first segment header, esp_app_desc_t

const TYPE_APP = 0x00;
const TYPE_DATA = 0x01;
const SUBTYPE_DATA_OTA = 0x00;
const SUBTYPE_APP_FACTORY = 0x00;
const SUBTYPE_APP_OTA_0 = 0x10;
const SUBTYPE_APP_OTA_15 = 0x1f;

const text = new TextDecoder();

function cString(bytes) {
  const end = bytes.indexOf(0);
  return text.decode(end < 0 ? bytes : bytes.subarray(0, end));
}

function view(bytes) {
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
}

// --- partition table --------------------------------------------------------------------------

// Returns { valid, entries }. `valid` is false for an erased or foreign sector; a table whose MD5
// entry does not match is also invalid, as it is to the bootloader.
export function parsePartitionTable(bytes, md5Hex) {
  const entries = [];
  const data = view(bytes);
  for (let at = 0; at + 32 <= bytes.length; at += 32) {
    const magic = data.getUint16(at, true);
    if (magic === 0x50aa) {
      entries.push({
        type: data.getUint8(at + 2),
        subtype: data.getUint8(at + 3),
        offset: data.getUint32(at + 4, true),
        size: data.getUint32(at + 8, true),
        label: cString(bytes.subarray(at + 12, at + 28)),
        flags: data.getUint32(at + 28, true),
      });
      continue;
    }
    if (magic === 0xebeb && md5Hex) {
      const want = [...bytes.subarray(at + 16, at + 32)].map((b) => b.toString(16).padStart(2, "0")).join("");
      if (md5Hex(bytes.subarray(0, at)) !== want) return { valid: false, entries: [] };
    }
    break;   // 0xEBEB (MD5) or 0xFFFF (end) or anything else: no more entries
  }
  return { valid: entries.length > 0, entries };
}

export function tablesMatch(a, b) {
  if (!a.valid || !b.valid || a.entries.length !== b.entries.length) return false;
  return a.entries.every((entry, i) => {
    const other = b.entries[i];
    return entry.type === other.type && entry.subtype === other.subtype && entry.offset === other.offset
      && entry.size === other.size && entry.label === other.label && entry.flags === other.flags;
  });
}

export function appPartitions(table) {
  return table.entries.filter((entry) => entry.type === TYPE_APP);
}

export function otadataPartition(table) {
  return table.entries.find((entry) => entry.type === TYPE_DATA && entry.subtype === SUBTYPE_DATA_OTA) ?? null;
}

// --- otadata -----------------------------------------------------------------------------------

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();

// zlib's crc32(data, value): the convention ESP-IDF's otatool uses, with value 0xFFFFFFFF.
export function crc32(bytes, value = 0) {
  let c = (value ^ 0xffffffff) >>> 0;
  for (const byte of bytes) c = CRC_TABLE[(c ^ byte) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

export const OTA_STATE = {
  0x0: "new", 0x1: "pending verify", 0x2: "valid", 0x3: "invalid", 0x4: "aborted", 0xffffffff: "undefined",
};

// Which OTA slot the bootloader will choose: the valid entry with the highest sequence number,
// slot (seq - 1) % otaCount. With no valid entry it boots the first OTA slot (there is no factory
// partition here), so bootSlot is null and the caller treats ota_0 as the boot image.
export function parseOtaData(bytes, otaCount) {
  const data = view(bytes);
  const entries = [];
  for (let sector = 0; sector * 0x1000 + 32 <= bytes.length; sector++) {
    const at = sector * 0x1000;
    const seq = data.getUint32(at, true);
    const state = data.getUint32(at + 24, true);
    const crc = data.getUint32(at + 28, true);
    const valid = seq !== 0xffffffff && crc === crc32(bytes.subarray(at, at + 4), 0xffffffff);
    entries.push({ seq, state, valid });
  }
  const usable = entries.filter((entry) => entry.valid && entry.state !== 0x3 && entry.state !== 0x4);
  if (!usable.length || !otaCount) return { entries, bootSlot: null };
  const newest = usable.reduce((a, b) => (b.seq > a.seq ? b : a));
  return { entries, bootSlot: (newest.seq - 1) % otaCount, state: newest.state };
}

export function otaIndex(entry) {
  return entry.subtype >= SUBTYPE_APP_OTA_0 && entry.subtype <= SUBTYPE_APP_OTA_15 ? entry.subtype - SUBTYPE_APP_OTA_0 : null;
}

export function isFactory(entry) {
  return entry.type === TYPE_APP && entry.subtype === SUBTYPE_APP_FACTORY;
}

// --- app descriptor ----------------------------------------------------------------------------

export function parseAppDesc(bytes) {
  if (bytes.length < 32 + 144 || bytes[0] !== 0xe9) return null;
  const desc = bytes.subarray(32);
  if (view(desc).getUint32(0, true) !== 0xabcd5432) return null;
  return {
    version: cString(desc.subarray(16, 48)),
    project: cString(desc.subarray(48, 80)),
    time: cString(desc.subarray(80, 96)),
    date: cString(desc.subarray(96, 112)),
    idf: cString(desc.subarray(112, 144)),
  };
}

// --- versions ----------------------------------------------------------------------------------

const VERSION_RE = /^v(\d+)\.(\d+)\.(\d+)(?:-rc(\d+))?$/;

// -1, 0 or 1 for two release tags; null when either is not one (a development build).
export function compareVersions(a, b) {
  const x = VERSION_RE.exec(a ?? "");
  const y = VERSION_RE.exec(b ?? "");
  if (!x || !y) return null;
  for (let i = 1; i <= 3; i++) {
    if (+x[i] !== +y[i]) return +x[i] < +y[i] ? -1 : 1;
  }
  // A release sorts after its own release candidates.
  const rx = x[4] === undefined ? Infinity : +x[4];
  const ry = y[4] === undefined ? Infinity : +y[4];
  return rx === ry ? 0 : rx < ry ? -1 : 1;
}

// --- putting it together -----------------------------------------------------------------------

// `board` is what was read: { table, otadata, apps: [{ entry, bytes }] }; `release` is the parsed
// partition table from the release being installed, and `version` its tag. The result drives the
// installer: what to show, which modes to allow, and which to preselect.
//
//   update  write the four images, erase nothing: NVS (settings) and storage (clips) survive
//   erase   erase the whole chip, then write the four images
export function assess(board, release, version) {
  const apps = board.apps.map(({ entry, bytes }) => ({
    label: entry.label,
    offset: entry.offset,
    index: otaIndex(entry),
    desc: parseAppDesc(bytes),
  }));
  const otaCount = apps.filter((app) => app.index !== null).length;
  const ota = board.otadata ? parseOtaData(board.otadata, otaCount) : { entries: [], bootSlot: null };
  const bootApp = apps.find((app) => app.index === (ota.bootSlot ?? 0)) ?? apps[0] ?? null;
  for (const app of apps) app.boot = app === bootApp;

  const holo = bootApp?.desc?.project === PROJECT ? bootApp : apps.find((app) => app.desc?.project === PROJECT) ?? null;
  const anything = board.table.valid || apps.some((app) => app.desc);
  const layoutMatches = tablesMatch(board.table, release);

  let updateBlocked = null;
  if (!holo) updateBlocked = anything ? "other" : "blank";
  else if (!layoutMatches) updateBlocked = "layout";

  const order = holo ? compareVersions(holo.desc.version, version) : null;
  return {
    apps,
    bootSlot: ota.bootSlot,
    tableValid: board.table.valid,
    layoutMatches,
    holo: holo ? { ...holo.desc, label: holo.label, boot: holo.boot } : null,
    foreign: !holo && anything,
    blank: !anything,
    updateBlocked,                                   // null, "blank", "other" or "layout"
    defaultMode: updateBlocked ? "erase" : "update",
    confirmErase: anything,                          // erasing would destroy something: ask first
    downgrade: order === 1,
    same: order === 0 || (holo !== null && holo.desc.version === version),
  };
}
