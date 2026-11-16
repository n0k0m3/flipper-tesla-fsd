// Test of the protocol codec in ble-board/web/index.html against the firmware.
//
//   make -C test check-web        (from the repo root; builds test/scrl_vectors first)
//
// The codec is the block between "codec:start" and "codec:end" in the page. This test checks it against:
//   - the #define values in ble-board/main/*.h and the UUIDs in scrl_ble.cpp (read from the source files),
//   - the status JSON and the Live record that the firmware packers make (test/scrl_vectors),
//   - the settings reader of the firmware (test/scrl_vectors parse),
//   - a real image, when ble-board/build/scrl-ble-board.bin exists.
// It does not test the page in a browser, and it does not test a board.
import fs from "node:fs";
import path from "node:path";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, "../../..");
const mainDir = path.join(root, "ble-board/main");
const read = f => fs.readFileSync(f, "utf8");

const html = read(path.join(here, "../index.html"));
const m = html.match(/\/\* codec:start \*\/([\s\S]*?)\/\* codec:end \*\//);
assert.ok(m, "the page has a codec block");
const SCRL = new Function(`${m[1]}; return SCRL;`)();

let n = 0;
const test = async (name, fn) => { await fn(); n++; console.log(`ok ${n} - ${name}`); };
const fromHex = h => Uint8Array.from(h.match(/../g), x => parseInt(x, 16));

// All "#define NAME value" of one family in a header. Values: 0x1Fu, 12u, (1u << 5), (10u * 60u * 1000u).
function defines(file, prefix) {
  const out = {};
  for (const [, name, expr] of read(path.join(mainDir, file)).matchAll(/^#define\s+(\w+)\s+(.+?)\s*(?:\/\/.*)?$/gm)) {
    if (!name.startsWith(prefix)) continue;
    const js = expr.replace(/(0x[0-9a-fA-F]+|\d+)u\b/g, "$1");
    assert.match(js, /^[\s\dxa-fA-F()<*+]+$/, `${name}: unexpected expression ${expr}`);
    out[name.slice(prefix.length)] = Function(`return (${js});`)();
  }
  return out;
}

const vectorsBin = path.join(root, "test/scrl_vectors");
if (!fs.existsSync(vectorsBin)) execFileSync("make", ["-C", path.join(root, "test"), "scrl_vectors"], { stdio: "inherit" });
const V = JSON.parse(execFileSync(vectorsBin, { encoding: "utf8" }));
const boardPasskey = bytes =>
  JSON.parse(execFileSync(vectorsBin, ["passkey"], { input: `${SCRL.hex(bytes)}\n`, encoding: "utf8" }));
const boardParse = (current, write) =>
  JSON.parse(execFileSync(vectorsBin, ["parse"], { input: `${current}\n${write}\n`, encoding: "utf8" }));

await test("flag bits match scrl_status.h, and none is missing", () => {
  const fw = defines("scrl_status.h", "SCRL_FLAG_");
  assert.ok(Object.keys(fw).length >= 15);
  assert.deepEqual(SCRL.FLAG, fw);
});

await test("command bytes match scrl_ble.h", () => {
  assert.deepEqual(SCRL.CMD, defines("scrl_ble.h", "SCRL_CMD_"));
});

await test("update constants and status codes match scrl_ota.h", () => {
  const fw = defines("scrl_ota.h", "SCRL_OTA_");
  const errs = { OK: fw.OK };
  for (const [k, v] of Object.entries(fw)) if (k.startsWith("ERR_")) errs[k.slice(4)] = v;
  assert.deepEqual(SCRL.OTA_ERR, errs);
  for (const v of Object.values(errs)) assert.ok(SCRL.OTA_ERR_TEXT[v], `status ${v} has a text`);
  assert.equal(SCRL.OTA_ERR_TEXT.length, Object.keys(errs).length);
  for (const k of ["OP_BEGIN", "OP_END", "OP_ABORT", "EVT_BEGIN", "EVT_CREDIT", "EVT_END", "EVT_STOPPED",
    "WINDOW", "CREDIT_STEP", "EVENT_LEN", "IDLE_MS", "CONFIRM_MS"]) {
    assert.equal(SCRL.OTA[k], fw[k], k);
  }
  // Every name of the header is covered above, except the status codes.
  const known = new Set([...Object.keys(SCRL.OTA), "OK"]);
  for (const k of Object.keys(fw)) assert.ok(known.has(k) || k.startsWith("ERR_"), `${k} is not in the page`);
});

await test("the app slot size matches partitions.csv", () => {
  const sizes = [...read(path.join(root, "ble-board/partitions.csv")).matchAll(/^ota_\d,\s*app,\s*ota_\d,\s*\S+,\s*(\S+)/gm)]
    .map(x => Number(x[1]));
  assert.equal(sizes.length, 2);
  assert.equal(SCRL.OTA.SLOT_SIZE, Math.min(...sizes));
});

await test("UUIDs match scrl_ble.cpp", () => {
  const src = read(path.join(mainDir, "scrl_ble.cpp"));
  const macro = src.match(/BLE_UUID128_INIT\(([^)]*\(n\)[^)]*)\)/);
  assert.ok(macro, "SCRL_UUID macro found");
  const bytes = macro[1].replace(/\\\s*\n/g, " ").split(",").map(x => x.trim());
  assert.equal(bytes.length, 16);
  const make = num => {
    // The macro lists the least significant byte first.
    const h = bytes.map(x => (x === "(n)" ? num : parseInt(x, 16)).toString(16).padStart(2, "0")).reverse().join("");
    return `${h.slice(0, 8)}-${h.slice(8, 12)}-${h.slice(12, 16)}-${h.slice(16, 20)}-${h.slice(20)}`;
  };
  const names = { SERVICE: "service", INFO: "info", STATUS: "status", LIVE: "live", SETTINGS: "settings",
    COMMAND: "command", LOG: "log", OTA_CTRL: "otaCtrl", OTA_DATA: "otaData" };
  const found = [...src.matchAll(/UUID_(\w+) = SCRL_UUID\((0x[0-9a-fA-F]+)\)/g)];
  assert.equal(found.length, Object.keys(names).length);
  for (const [, name, num] of found) assert.equal(SCRL.UUIDS[names[name]], make(Number(num)), name);
});

await test("phase and block names match scroll_ack.h, and each block has a text", () => {
  assert.deepEqual(SCRL.PHASES, V.phases);
  assert.deepEqual(SCRL.BLOCKS, V.blocks);
  for (const b of V.blocks) assert.ok(SCRL.BLOCK_TEXT[b], `text for ${b}`);
  assert.equal(SCRL.LIVE_LEN, V.live_len);
  assert.equal(SCRL.STATUS_MAX, V.status_max);
});

// The expected values below are the values in test/scrl_sample.h.
await test("parseStatus reads the status JSON of the firmware packer", () => {
  const st = SCRL.parseStatus(V.status_json);
  assert.deepEqual(st, {
    sent: 0x01020304, phase: "SecondPending", block: "gap", nativeAge: 2, dasAge: 40, apState: 6, handsOn: 3,
    vehicleBus: "can0", lastTxAllowed: true, pairsAborted: 7, timerArmed: true, opMode: 1,
    flags: SCRL.FLAG.TX_ALLOWED | SCRL.FLAG.ACTIVE | SCRL.FLAG.ENABLED | SCRL.FLAG.HW4_SEEN,
    rx: { all: 1000, x3c2: 10, x39b: 20, x370: 0, x257: 30, x318: 40, x129: 50, dropped: 1 },
    tx: { ok: 8, failed: 2, refused: 3 }, busOff: 1, resetReason: 1, powerOn: 42, uptime: 3600, rxPin: 2,
  });
});

await test("parseStatus reads the largest values and refuses a damaged status", () => {
  assert.ok(V.big_status_json.length <= SCRL.STATUS_MAX);
  const st = SCRL.parseStatus(V.big_status_json);
  assert.equal(st.sent, 4294967295);
  assert.equal(st.rx.dropped, 4294967295);
  assert.equal(st.flags, 0xFFFF);
  assert.equal(st.rxPin, -1);
  assert.equal(st.vehicleBus, null);
  assert.throws(() => SCRL.parseStatus(V.status_json.slice(0, 200)));
  assert.throws(() => SCRL.parseStatus(V.status_json.replace('"rx":[1000,', '"rx":[')), /rx/);
  assert.throws(() => SCRL.parseStatus(V.status_json.replace('"flags":1061', '"flags":"x"')), /flags/);
});

await test("parseLive reads the Live record of the firmware packer", () => {
  assert.deepEqual(SCRL.parseLive(fromHex(V.live_hex)), {
    phase: 2, block: 14, apState: 6, handsOn: 3, flagsLow: 0x25, sent: 0x01020304, pairsAborted: 7,
    nativeAge: 2, dasAge: 40, feedMv: 0, boardMa: 0,
  });
  const big = SCRL.parseLive(fromHex(V.big_live_hex));
  assert.equal(big.sent, 4294967295);
  assert.equal(big.pairsAborted, 65535);
  assert.equal(big.nativeAge, 65535);
  assert.equal(big.flagsLow, 0xFF);
  assert.throws(() => SCRL.parseLive(fromHex(V.live_hex).subarray(0, 18)), /19/);
});

await test("a Live record does not clear the high flag bits", () => {
  const F = SCRL.FLAG;
  const full = F.IMAGE_PENDING | F.PINS_CHECKED | F.CAN_RUNNING | F.ACTIVE;
  const merged = SCRL.mergeFlags(full, F.ENABLED | F.WRONG_BUS);
  assert.equal(merged, F.IMAGE_PENDING | F.PINS_CHECKED | F.CAN_RUNNING | F.ENABLED | F.WRONG_BUS);
  assert.equal(SCRL.mergeFlags(0xFFFF, 0), 0xFF00);
});

await test("settings: the page reads the board's JSON and uses the board's limits", () => {
  const s = SCRL.parseSettings(V.default_settings_json);
  assert.deepEqual(s, { enabled: false, mode: 0, pattern: 0, delay_ms: 600, gap_ms: 100, interval_ms: 2500, op_mode: 0 });
  assert.deepEqual(SCRL.LIMITS.delay_ms, [0, V.limits.delay_max]);
  assert.deepEqual(SCRL.LIMITS.gap_ms, [0, V.limits.gap_max]);
  assert.deepEqual(SCRL.LIMITS.interval_ms, [V.limits.interval_min, V.limits.interval_max]);
  assert.deepEqual(SCRL.LIMITS.mode, [0, V.limits.mode_max]);
  assert.deepEqual(SCRL.LIMITS.pattern, [0, V.limits.pattern_max]);
  assert.deepEqual(SCRL.settingsErrors(s), []);
  assert.deepEqual(SCRL.settingsErrors({ ...s, delay_ms: 5001, interval_ms: 999, gap_ms: NaN }).sort(), ["delay_ms", "gap_ms", "interval_ms"]);
});

await test("settings: the firmware reader accepts each write of the page", () => {
  const board = SCRL.parseSettings(V.default_settings_json);
  const forms = [
    { ...board, enabled: true },
    { ...board, mode: 1, pattern: 3 },
    { ...board, delay_ms: 0, gap_ms: 2000, interval_ms: 10000 },
    { ...board, op_mode: 1 },
    { enabled: true, mode: 1, pattern: 2, delay_ms: 5000, gap_ms: 0, interval_ms: 1000, op_mode: 1 },
  ];
  for (const form of forms) {
    const diff = SCRL.settingsDiff(board, form);
    const r = boardParse(V.default_settings_json, JSON.stringify(diff));
    assert.equal(r.result, 0, JSON.stringify(diff));
    assert.deepEqual(SCRL.parseSettings(JSON.stringify(r.settings)), form);
    assert.equal(r.op_mode_named, form.op_mode !== board.op_mode);
  }
});

await test("settings: a save names op_mode only when the user changed it", () => {
  const active = '{"enabled":true,"mode":0,"pattern":0,"delay_ms":600,"gap_ms":100,"interval_ms":2500,"op_mode":1}';
  const board = SCRL.parseSettings(active);
  const diff = SCRL.settingsDiff(board, { ...board, gap_ms: 300 });
  assert.deepEqual(diff, { gap_ms: 300 });
  const r = boardParse(active, JSON.stringify(diff));
  assert.equal(r.result, 0);
  assert.equal(r.op_mode_named, false, "a gap change must not release a bus-off lock");
  assert.equal(r.settings.op_mode, 1);
  assert.deepEqual(SCRL.settingsDiff(board, { ...board }), {});
  // The release action of the page writes exactly this.
  assert.equal(boardParse(active, JSON.stringify({ op_mode: 1 })).op_mode_named, true);
});

await test("passkey: the firmware reader takes the command of the page", () => {
  for (const [text, value] of [["123456", 123456], ["000042", 42], ["000000", 0], ["999999", 999999]]) {
    const cmd = SCRL.passkeyCommand(text);
    assert.equal(cmd.length, 7);
    assert.equal(cmd[0], SCRL.CMD.SET_PASSKEY);
    assert.deepEqual(boardPasskey(cmd), { ok: true, passkey: value }, text);
  }
  for (const bad of ["", "12345", "1234567", "12345a", "12 456", "１２３４５６", " 123456"]) {
    assert.ok(!SCRL.isPasskey(bad), `"${bad}" is not a passkey`);
    assert.throws(() => SCRL.passkeyCommand(bad));
  }
  // The board refuses what the page would never send.
  assert.equal(boardPasskey(Uint8Array.from([SCRL.CMD.SET_PASSKEY, 0x31, 0x32, 0x33])).ok, false);
  assert.equal(boardPasskey(Uint8Array.from([SCRL.CMD.SET_PASSKEY, 0x31, 0x32, 0x33, 0x34, 0x35, 0x61])).ok, false);
  assert.equal(boardPasskey(Uint8Array.from([SCRL.CMD.CONFIRM_IMAGE, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36])).ok, false);
});

await test("update: BEGIN bytes, events, window and cut detection", () => {
  const sha = Uint8Array.from({ length: 32 }, (_, i) => i + 1);
  const b = SCRL.otaBegin(0x000AD320, sha);
  assert.equal(b.length, 37);
  assert.deepEqual([...b.subarray(0, 5)], [0x01, 0x20, 0xD3, 0x0A, 0x00]);
  assert.deepEqual([...b.subarray(5)], [...sha]);
  assert.throws(() => SCRL.otaBegin(10, new Uint8Array(31)));
  assert.deepEqual(SCRL.parseOtaEvent(fromHex("0200000a0800")), { event: 2, status: 0, offset: 0x00080A00 });
  assert.deepEqual(SCRL.parseOtaEvent(fromHex("0406ffffffff")), { event: 4, status: 6, offset: 4294967295 });
  assert.throws(() => SCRL.parseOtaEvent(fromHex("0200")));

  assert.ok(SCRL.mayWrite(0, 240, 0));
  assert.ok(SCRL.mayWrite(4096 - 240, 240, 0));
  assert.ok(!SCRL.mayWrite(4096 - 239, 240, 0), "never more than WINDOW past the credit");
  assert.ok(SCRL.mayWrite(4096, 240, 2160));

  assert.ok(SCRL.creditIsWhole(2160, 240, 700000));
  assert.ok(!SCRL.creditIsWhole(2100, 240, 700000), "writes of 240 cut to 100");
  assert.ok(SCRL.creditIsWhole(709408, 240, 709408), "the last credit is the image size");
  assert.deepEqual([500, 240, 180, 100, 20].map(SCRL.nextChunk), [240, 180, 100, 20, 0]);
  assert.equal(SCRL.platformChunk("Mozilla/5.0 (iPhone; CPU iPhone OS 17_0 like Mac OS X)"), 180);
  assert.equal(SCRL.platformChunk("Mozilla/5.0 (Linux; Android 14)"), 240);
});

// A small image with the layout of an ESP-IDF app: header, one segment header, app description, SHA-256 at the end.
async function fakeImage({ chip = 0x0010, project = "scrl-ble-board", version = "9.9.9", size = 1024 } = {}) {
  const b = new Uint8Array(size);
  b[0] = 0xE9; b[1] = 1; b[12] = chip & 0xFF; b[13] = chip >> 8; b[23] = 1;
  new DataView(b.buffer).setUint32(32, 0xABCD5432, true);
  const put = (o, s) => { for (let i = 0; i < s.length; i++) b[o + i] = s.charCodeAt(i); };
  put(32 + 16, version); put(32 + 48, project); put(32 + 80, "12:00:00"); put(32 + 96, "Jan  1 2026"); put(32 + 112, "v5.5.6");
  b.set(await SCRL.sha256(b.subarray(0, size - 32)), size - 32);
  return b;
}

await test("image check: a good image passes, each wrong image fails", async () => {
  const good = await fakeImage();
  const r = SCRL.checkImage(good);
  assert.deepEqual(r.errors, []);
  assert.deepEqual(r.info, { size: 1024, version: "9.9.9", project: "scrl-ble-board", built: "Jan  1 2026 12:00:00", idf: "v5.5.6", hashAppended: true });
  assert.ok(await SCRL.appendedHashOk(good));

  const flipped = good.slice(); flipped[500] ^= 1;
  assert.ok(!(await SCRL.appendedHashOk(flipped)), "one changed bit fails the hash");
  const noMagic = good.slice(); noMagic[0] = 0x7F;
  assert.match(SCRL.checkImage(noMagic).errors.join(), /0xE9/);
  assert.match(SCRL.checkImage(await fakeImage({ chip: 0x000D })).errors.join(), /chip ID is 0xd/);
  assert.match(SCRL.checkImage(await fakeImage({ project: "hello_world" })).errors.join(), /hello_world/);
  assert.match(SCRL.checkImage(new Uint8Array(100)).errors.join(), /too small/);
  assert.match(SCRL.checkImage(await fakeImage({ size: SCRL.OTA.SLOT_SIZE + 16 })).errors.join(), /app slot/);
  const noHash = good.slice(); noHash[23] = 0;
  assert.match(SCRL.checkImage(noHash).errors.join(), /no SHA-256/);
});

const realBin = path.join(root, "ble-board/build/scrl-ble-board.bin");
if (fs.existsSync(realBin)) {
  await test("image check: the built firmware image passes", async () => {
    const b = new Uint8Array(fs.readFileSync(realBin));
    const r = SCRL.checkImage(b);
    assert.deepEqual(r.errors, []);
    assert.equal(r.info.project, "scrl-ble-board");
    assert.match(r.info.version, /^\d+\.\d+\.\d+/);
    assert.match(r.info.idf, /^v5\./);
    assert.ok(await SCRL.appendedHashOk(b));
  });
} else {
  console.log("# skip: ble-board/build/scrl-ble-board.bin is not built");
}

console.log(`\n${n} passed`);
