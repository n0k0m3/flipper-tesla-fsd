// Browser test of ble-board/web/index.html in headless Chrome. It needs playwright-core and a Chrome or Chromium:
//
//   PLAYWRIGHT_CORE=/path/to/playwright-core/index.mjs CHROME=/usr/bin/google-chrome make -C test check-web-browser
//
// Without PLAYWRIGHT_CORE the test looks for the package "playwright-core". Without it, the test skips.
//
// Two groups of scenarios:
//   1. The page with ?mock: its own simulated board (MockLink).
//   2. The page without ?mock and with a fake navigator.bluetooth: the page then uses WebLink, the class that talks
//      to a real browser. The fake hands out DataViews with a byte offset, reuses the characteristic objects on each
//      connection, and can cancel the chooser, reject long writes, or offer only writeValue (as Bluefy can).
// Both groups answer from the mock board in the page. They prove the wiring of the page, not a board and not a
// real Bluetooth stack.
import assert from "node:assert/strict";
import crypto from "node:crypto";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const PAGE = pathToFileURL(path.join(here, "../index.html")).href;
const spec = process.env.PLAYWRIGHT_CORE ? pathToFileURL(process.env.PLAYWRIGHT_CORE).href : "playwright-core";
const pw = await import(spec).catch(() => null);
if (!pw) {
  console.log("# skip: playwright-core not found. Set PLAYWRIGHT_CORE to its index.mjs");
  process.exit(0);
}
const { chromium } = pw;

// A small image with the layout of an ESP-IDF app: header, one segment header, app description, SHA-256 at the end.
function image(project, version = "9.9.9", size = 64 * 1024) {
  const b = Buffer.alloc(size);
  for (let i = 300; i < size - 32; i++) b[i] = (i * 31 + 7) & 0xFF;
  b[0] = 0xE9; b[1] = 1; b[12] = 0x10; b[23] = 1;
  b.writeUInt32LE(0xABCD5432, 32);
  b.write(version, 32 + 16); b.write(project, 32 + 48); b.write("12:00:00", 32 + 80); b.write("Jan  1 2026", 32 + 96); b.write("v5.5.6", 32 + 112);
  crypto.createHash("sha256").update(b.subarray(0, size - 32)).digest().copy(b, size - 32);
  return { name: `${project}.bin`, mimeType: "application/octet-stream", buffer: b };
}
const BIN = image("scrl-ble-board");
const HELLO = image("hello_world");

const browser = await chromium.launch({ executablePath: process.env.CHROME || undefined, headless: true });
const results = [];
async function scenario(name, query, { init, ...opts }, fn) {
  const ctx = await browser.newContext({ viewport: { width: 420, height: 900 }, deviceScaleFactor: 2, ...opts });
  const page = await ctx.newPage();
  if (init) await page.addInitScript(init[0], init[1]);
  const errors = [];
  page.on("pageerror", e => errors.push(`pageerror: ${e.message}`));
  page.on("console", m => { if (m.type() === "error") errors.push(`console: ${m.text()}`); });
  page.on("dialog", d => d.accept());
  await page.goto(`${PAGE}?${query}`);
  try {
    await fn(page);
    assert.deepEqual(errors, []);
    results.push(`ok   ${name}`);
  } catch (e) {
    results.push(`FAIL ${name}: ${e.message.split("\n").slice(0, 3).join(" ")}`);
    if (process.env.SHOTS) await page.screenshot({ path: path.join(process.env.SHOTS, `fail-${name}.png`), fullPage: true });
  }
  await ctx.close();
}
const text = (page, sel) => page.locator(sel).innerText();
const connect = async page => { await page.click("#btnLink"); await page.waitForSelector("#cardStatus:not([hidden])", { timeout: 20000 }); };

await scenario("connect-status-settings", "mock", {}, async page => {
  assert.equal(await page.evaluate(() => isSecureContext && !!crypto.subtle), true, "secure context on this URL");
  assert.equal(await page.isVisible("#mockBanner"), true);
  assert.equal(await page.isVisible("#cardStatus"), false);
  await connect(page);
  assert.equal(await page.isVisible("#cardConnect"), false);
  assert.match(await text(page, "#chips"), /Active/);
  assert.match(await text(page, "#chips"), /Scroll Ack on/);
  assert.match(await text(page, "#linkName"), /SCRL-MOCK/);
  await page.waitForFunction(() => /^\d+$/.test(document.getElementById("vSent").textContent));
  assert.equal(await page.inputValue("#sGap"), "100");
  assert.equal(await page.isDisabled("#btnSave"), true);
  // out of range: Save stays off
  await page.fill("#sGap", "2500");
  assert.equal(await page.isDisabled("#btnSave"), true);
  await page.fill("#sGap", "300");
  assert.equal(await page.isDisabled("#btnSave"), false);
  await page.click("#btnSave");
  await page.waitForFunction(() => document.getElementById("settingsMsg").textContent === "Saved.");
  assert.match(await text(page, "#log"), /SET {2}on=1 mode=0 pat=0 dly=600 gap=300 int=2500 op=1/);
  // to Listen-Only: no confirm needed
  await page.selectOption("#sOp", "0");
  assert.equal(await page.isVisible("#activeConfirm"), false);
  await page.click("#btnSave");
  // the form is locked until the board's values are read back
  await page.waitForFunction(() => document.getElementById("settingsMsg").textContent === "Saved." && !document.getElementById("settingsForm").disabled);
  await page.waitForFunction(() => /Listen-Only/.test(document.getElementById("chips").textContent));
  // back to Active: needs the confirm tick
  await page.selectOption("#sOp", "1");
  assert.equal(await page.isVisible("#activeConfirm"), true);
  assert.equal(await page.isDisabled("#btnSave"), true);
  await page.check("#sActiveOk");
  await page.click("#btnSave");
  await page.waitForFunction(() => /(^|\s)Active/.test(document.getElementById("chips").textContent));
  // live notifications move the counter
  const a = Number(await text(page, "#vSent"));
  await page.waitForFunction(v => Number(document.getElementById("vSent").textContent) > v, a, { timeout: 8000 });
  await page.click("#statusDetails summary");
  assert.match(await text(page, "#tblCounts"), /Firmware\s+0\.1\.0/);
  // reset counters
  await page.click("#btnCounters");
  await page.waitForFunction(() => Number(document.getElementById("vSent").textContent) < 10);
  // disconnect
  await page.click("#btnLink");
  await page.waitForSelector("#cardConnect:not([hidden])");
});

await scenario("pairing-retry", "mock&pair", {}, async page => {
  await page.click("#btnLink");
  await page.waitForFunction(() => /passkey/.test(document.getElementById("connectMsg").textContent));
  await page.waitForSelector("#cardStatus:not([hidden])", { timeout: 20000 });
});

await scenario("update-confirm", "mock", { colorScheme: "dark" }, async page => {
  await connect(page);
  assert.equal(await page.isDisabled("#btnUpdate"), true);
  await page.setInputFiles("#fwFile", BIN);
  await page.waitForFunction(() => /scrl-ble-board 9\.9\.9/.test(document.getElementById("imgInfo").textContent));
  assert.equal(await page.isDisabled("#btnUpdate"), false);
  await page.click("#btnUpdate");
  await page.waitForFunction(() => /Sent \d+ of/.test(document.getElementById("updateMsg").textContent), null, { timeout: 15000 });
  // the Live record (1 Hz) carries the update flag: the board is in Listen-Only during the transfer
  await page.waitForFunction(() => /Update runs/.test(document.getElementById("chips").textContent) && /Listen-Only/.test(document.getElementById("chips").textContent), null, { timeout: 5000 });
  assert.equal(await page.isDisabled("#btnLink"), true, "no disconnect button during the update");
  for (const id of ["#btnReboot", "#btnFactory", "#btnForget", "#btnCounters", "#btnSave"]) {
    assert.equal(await page.isDisabled(id), true, `${id} is off during the update`);
  }
  await page.click("#passkeyBox summary");
  await page.fill("#pkNew", "123456");
  await page.fill("#pkRepeat", "123456");
  assert.equal(await page.isDisabled("#btnPasskey"), true, "no passkey change during the update");
  assert.equal(await page.isVisible("#btnAbort"), true);
  await page.waitForFunction(() => /The new image runs/.test(document.getElementById("updateMsg").textContent), null, { timeout: 90000 });
  assert.match(await text(page, "#alerts"), /not confirmed/);
  assert.match(await text(page, "#chips"), /Listen-Only/);
  assert.equal(await page.isDisabled("#btnUpdate"), true, "no second update before the confirm");
  // the Board buttons come back when the update flow ends
  await page.waitForFunction(() => !document.getElementById("btnReboot").disabled && !document.getElementById("btnFactory").disabled, null, { timeout: 5000 });
  await page.click("#alerts button");
  await page.waitForFunction(() => /confirmed/.test(document.getElementById("updateMsg").textContent));
  await page.waitForFunction(() => !/not confirmed/.test(document.getElementById("alerts").textContent));
  await page.waitForFunction(() => /(^|\s)Active/.test(document.getElementById("chips").textContent), null, { timeout: 8000 });
});

await scenario("update-cut-write", "mock&cut=200", {}, async page => {
  await connect(page);
  await page.setInputFiles("#fwFile", BIN);
  await page.waitForFunction(() => !document.getElementById("btnUpdate").disabled);
  await page.click("#btnUpdate");
  await page.waitForFunction(() => /The update stopped/.test(document.getElementById("updateMsg").textContent), null, { timeout: 30000 });
  assert.match(await text(page, "#updateMsg"), /cut a write of 240 bytes/);
  assert.equal(await page.inputValue("#fwChunk"), "180");
  assert.match(await text(page, "#updateMsg"), /keeps its old image/);
  // the board is still usable: the next try with 180 bytes per write works
  await page.click("#btnUpdate");
  await page.waitForFunction(() => /The new image runs/.test(document.getElementById("updateMsg").textContent), null, { timeout: 120000 });
});

// Writes cut to less than half: the board never reaches a credit. The page stops after 10 s and steps down.
await scenario("update-stalled-write", "mock&cut=100", {}, async page => {
  await connect(page);
  await page.setInputFiles("#fwFile", BIN);
  await page.waitForFunction(() => !document.getElementById("btnUpdate").disabled);
  await page.click("#btnUpdate");
  await page.waitForFunction(() => /The update stopped/.test(document.getElementById("updateMsg").textContent), null, { timeout: 30000 });
  assert.match(await text(page, "#updateMsg"), /no credit for 10 s/);
  assert.equal(await page.inputValue("#fwChunk"), "180");
  assert.match(await text(page, "#log"), /\[page\]|UPD/);
});

await scenario("update-abort", "mock", {}, async page => {
  await connect(page);
  await page.setInputFiles("#fwFile", BIN);
  await page.waitForFunction(() => !document.getElementById("btnUpdate").disabled);
  await page.click("#btnUpdate");
  await page.waitForFunction(() => /Sent \d+ of/.test(document.getElementById("updateMsg").textContent), null, { timeout: 15000 });
  await page.click("#btnAbort");
  await page.waitForFunction(() => /you aborted the update/.test(document.getElementById("updateMsg").textContent), null, { timeout: 15000 });
  await page.waitForFunction(() => !document.getElementById("btnUpdate").disabled && document.getElementById("btnAbort").hidden && !document.getElementById("btnLink").disabled, null, { timeout: 8000 });
  await page.waitForFunction(() => /(^|\s)Active/.test(document.getElementById("chips").textContent) && !/Update runs/.test(document.getElementById("chips").textContent), null, { timeout: 8000 });
  assert.match(await text(page, "#log"), /UPD {2}started/);
});

await scenario("wrong-image", "mock", {}, async page => {
  await connect(page);
  await page.setInputFiles("#fwFile", HELLO);
  await page.waitForFunction(() => /project name is "hello_world"/.test(document.getElementById("imgInfo").textContent));
  assert.equal(await page.isDisabled("#btnUpdate"), true);
});

await scenario("bus-off-lock", "mock&lock", {}, async page => {
  await connect(page);
  assert.match(await text(page, "#alerts"), /3 bus-off events/);
  assert.match(await text(page, "#chips"), /Listen-Only/);
  assert.match(await text(page, "#statusMsg"), /setting is Active/);
  // a save of another key must not release the lock
  await page.fill("#sGap", "250");
  await page.click("#btnSave");
  await page.waitForFunction(() => document.getElementById("settingsMsg").textContent === "Saved.");
  assert.match(await text(page, "#alerts"), /3 bus-off events/);
  await page.click("#alerts button");
  await page.waitForFunction(() => !/bus-off/.test(document.getElementById("alerts").textContent));
});

await scenario("pending-at-connect-and-restart", "mock&pending", {}, async page => {
  await connect(page);
  assert.match(await text(page, "#alerts"), /goes back to the old image in (10 min 00|9 min \d\d) s/);
  // the countdown runs, and the button is not replaced by the 1 Hz render
  const btn = await page.locator("#alerts button").elementHandle();
  const t0 = await text(page, "#alerts");
  await page.waitForFunction(t => document.getElementById("alerts").textContent !== t, t0, { timeout: 5000 });
  assert.equal(await btn.evaluate(e => e.isConnected), true, "same button element after re-render");
  await page.click("#btnReboot");
  await page.waitForSelector("#cardConnect:not([hidden])");
  assert.match(await text(page, "#connectMsg"), /The board restarts/);
});


await scenario("change-passkey", "mock", {}, async page => {
  await connect(page);
  let asked = "";
  page.on("dialog", d => { asked = d.message(); });
  await page.click("#passkeyBox summary");
  assert.equal(await page.isDisabled("#btnPasskey"), true);
  await page.fill("#pkNew", "12345");
  await page.fill("#pkRepeat", "12345");
  assert.equal(await page.isDisabled("#btnPasskey"), true, "5 digits are not a passkey");
  await page.fill("#pkNew", "12345a");
  assert.equal(await page.isDisabled("#btnPasskey"), true, "a letter is not a digit");
  await page.fill("#pkNew", "004271");
  await page.fill("#pkRepeat", "004272");
  assert.equal(await page.isDisabled("#btnPasskey"), true, "the two fields differ");
  await page.fill("#pkRepeat", "004271");
  assert.equal(await page.isDisabled("#btnPasskey"), false);
  await page.click("#btnPasskey");
  await page.waitForFunction(() => /has the new passkey/.test(document.getElementById("passkeyMsg").textContent));
  assert.match(asked, /004271/);
  assert.equal(await page.evaluate(() => S.mockBoard.passkey), "004271", "the leading zeros reach the board");
  assert.equal(await page.inputValue("#pkNew"), "", "the page keeps no copy");
  assert.equal(await page.inputValue("#pkRepeat"), "");
  assert.equal(await page.isDisabled("#btnPasskey"), true);
  assert.match(await text(page, "#log"), /CMD {2}passkey changed/);
  assert.doesNotMatch(await text(page, "#log"), /004271/, "the digits are not in the log");
  // the link stays up: a new passkey does not touch the bond of this phone
  assert.equal(await page.isVisible("#cardStatus"), true);
});

await scenario("factory-reset", "mock", {}, async page => {
  await connect(page);
  let asked = "";
  page.on("dialog", d => { asked = d.message(); });
  await page.click("#btnFactory");
  await page.waitForSelector("#cardConnect:not([hidden])");
  assert.match(asked, /The passkey stays/);
  assert.match(await text(page, "#connectMsg"), /pair again with the passkey on the label/);
  // the board is back with its defaults: Scroll Ack off, Listen-Only
  await page.waitForTimeout(1600);
  await connect(page);
  assert.match(await text(page, "#chips"), /Listen-Only/);
  assert.match(await text(page, "#chips"), /Scroll Ack off/);
});

// ---- Group 2: the page without ?mock. It uses WebLink, and this fake stands in for the browser Bluetooth stack.
function fakeBluetooth(opts) {
  const U = n => `e591${n.toString(16).padStart(4, "0")}-51ba-4d0b-8422-5cb9dc778552`;
  const NAMES = { [U(2)]: "info", [U(3)]: "status", [U(4)]: "live", [U(5)]: "settings", [U(6)]: "command", [U(7)]: "log", [U(8)]: "otaCtrl", [U(9)]: "otaData" };
  const F = (window.__fake = { requests: [], connects: 0, cancel: !!opts.cancel, rejectOver: opts.rejectOver || 0,
    writes: { withResponse: 0, withoutResponse: 0, plain: 0 } });
  let board = null;
  const getBoard = () => board || (board = new MockLink());  // MockLink comes from the page script
  F.board = getBoard;
  const view = u8 => { const b = new Uint8Array(u8.length + 5); b.fill(0xEE); b.set(u8, 5); return new DataView(b.buffer, 5, u8.length); };
  const toU8 = v => new Uint8Array(v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength));
  class Chr extends EventTarget {
    constructor(uuid) {
      super(); this.uuid = uuid; this.name = NAMES[uuid]; this.value = null;
      if (!opts.bluefy) {
        this.writeValueWithResponse = async v => { F.writes.withResponse++; await getBoard().write(this.name, toU8(v), false); };
        this.writeValueWithoutResponse = async v => {
          F.writes.withoutResponse++;
          const u = toU8(v);
          if (F.rejectOver && u.length > F.rejectOver) throw new DOMException("GATT operation failed for unknown reason.", "NotSupportedError");
          await getBoard().write(this.name, u, true);
        };
      }
    }
    async readValue() { this.value = view(await getBoard().read(this.name)); return this.value; }
    async writeValue(v) { F.writes.plain++; await getBoard().write(this.name, toU8(v), this.name === "otaData"); }
    async startNotifications() {
      await getBoard().subscribe(this.name, u8 => { this.value = view(u8); this.dispatchEvent(new Event("characteristicvaluechanged")); });
      return this;
    }
  }
  const chrs = {};
  const device = new EventTarget();
  device.name = "SCRL-FAKE";
  device.gatt = {
    connected: false,
    async connect() {
      const b = getBoard();
      b.onDisconnect = () => { device.gatt.connected = false; device.dispatchEvent(new Event("gattserverdisconnected")); };
      await b.open();
      device.gatt.connected = true; F.connects++;
      return { getPrimaryService: async uuid => {
        if (uuid !== U(1)) throw new DOMException("no service", "NotFoundError");
        return { getCharacteristic: async u => { if (!NAMES[u]) throw new DOMException("no characteristic", "NotFoundError"); return (chrs[u] ||= new Chr(u)); } };
      } };
    },
    disconnect() { getBoard().close(); },
  };
  Object.defineProperty(navigator, "bluetooth", { configurable: true, value: {
    async requestDevice(o) { F.requests.push(JSON.parse(JSON.stringify(o))); if (F.cancel) throw new DOMException("User cancelled the requestDevice() chooser.", "NotFoundError"); return device; },
  } });
}
async function fakeScenario(name, query, opts, fn) {
  await scenario(name, query, { init: [fakeBluetooth, opts] }, fn);
}
const ready = page => page.waitForSelector("#cardStatus:not([hidden])", { timeout: 20000 });
const count = async (page, re) => ((await text(page, "#log")).match(re) || []).length;

await fakeScenario("weblink-connect-notify-reconnect", "x=1", {}, async page => {
  assert.equal(await page.isVisible("#mockBanner"), false, "this run does not use the mock link of the page");
  assert.equal(await text(page, "#btnLink"), "Connect");
  await page.click("#btnLink");
  await ready(page);
  assert.match(await text(page, "#linkName"), /SCRL-FAKE/);
  const req = await page.evaluate(() => __fake.requests);
  assert.equal(req.length, 1);
  assert.deepEqual(req[0].filters, [{ services: ["e5910001-51ba-4d0b-8422-5cb9dc778552"] }, { namePrefix: "SCRL-" }]);
  assert.deepEqual(req[0].optionalServices, ["e5910001-51ba-4d0b-8422-5cb9dc778552"]);
  // values come as DataViews with a byte offset: the numbers must still be right
  await page.click("#statusDetails summary");
  assert.match(await text(page, "#tblCounts"), /Firmware\s+0\.1\.0 \(4d298926\)/);
  assert.equal(await page.inputValue("#sInterval"), "2500");
  // a notification moves the counter
  const a = Number(await text(page, "#vSent"));
  await page.waitForFunction(v => Number(document.getElementById("vSent").textContent) > v, a, { timeout: 8000 });
  await page.fill("#sGap", "333");
  await page.click("#btnSave");
  await page.waitForFunction(() => document.getElementById("settingsMsg").textContent === "Saved.");
  assert.equal(await count(page, /gap=333/g), 1);
  assert.ok((await page.evaluate(() => __fake.writes.withResponse)) >= 1);
  // the board drops the link
  await page.evaluate(() => __fake.board().drop(0));
  await page.waitForSelector("#cardConnect:not([hidden])");
  assert.match(await text(page, "#connectMsg"), /The board disconnected/);
  assert.equal(await text(page, "#btnLink"), "Reconnect");
  assert.equal(await page.isVisible("#btnOther"), true);
  // Reconnect uses the same device: no new chooser
  await page.click("#btnLink");
  await ready(page);
  assert.equal(await page.evaluate(() => __fake.requests.length), 1);
  assert.equal(await page.evaluate(() => __fake.connects), 2);
  // the characteristic objects are the same as before: each log line must still come once
  await page.fill("#sGap", "444");
  await page.click("#btnSave");
  await page.waitForFunction(() => document.getElementById("settingsMsg").textContent === "Saved.");
  await page.waitForFunction(() => /gap=444/.test(document.getElementById("log").textContent));
  await page.waitForTimeout(300);
  assert.equal(await count(page, /gap=444/g), 1, "no double notification after a reconnect");
});

await fakeScenario("weblink-chooser-cancel", "x=1", { cancel: true }, async page => {
  await page.click("#btnLink");
  await page.waitForFunction(() => /No board selected/.test(document.getElementById("connectMsg").textContent));
  assert.equal(await text(page, "#btnLink"), "Connect", "a cancelled chooser leaves no dead link");
  assert.equal(await page.isVisible("#btnOther"), false);
  await page.evaluate(() => { __fake.cancel = false; });
  await page.click("#btnLink");
  await ready(page);
  await page.evaluate(() => __fake.board().drop(0));
  await page.waitForSelector("#cardConnect:not([hidden])");
  // cancel "Select another board": the board from before stays usable
  await page.evaluate(() => { __fake.cancel = true; });
  await page.click("#btnOther");
  await page.waitForFunction(() => /No board selected/.test(document.getElementById("connectMsg").textContent));
  assert.equal(await text(page, "#btnLink"), "Reconnect");
  await page.click("#btnLink");
  await ready(page);
});

await fakeScenario("weblink-rejected-write", "x=1", { rejectOver: 200 }, async page => {
  await page.click("#btnLink");
  await ready(page);
  await page.setInputFiles("#fwFile", BIN);
  await page.waitForFunction(() => !document.getElementById("btnUpdate").disabled);
  await page.click("#btnUpdate");
  await page.waitForFunction(() => /The update stopped/.test(document.getElementById("updateMsg").textContent), null, { timeout: 30000 });
  assert.match(await text(page, "#updateMsg"), /the browser refused a write of 240 bytes/);
  assert.equal(await page.inputValue("#fwChunk"), "180");
  // the page sent ABORT: the board left the update at once, not after its 30 s timeout
  assert.equal(await page.evaluate(() => __fake.board().ota), null);
  assert.equal(await count(page, /UPD {2}started/g), 1);
  // so a new start works at once, with 180 bytes per write, and the page finds the board again by itself
  await page.click("#btnUpdate");
  await page.waitForFunction(() => /The new image runs/.test(document.getElementById("updateMsg").textContent), null, { timeout: 120000 });
  assert.equal(await page.evaluate(() => __fake.requests.length), 1, "no chooser for the reconnect after the update");
  await page.click("#alerts button");
  await page.waitForFunction(() => /confirmed/.test(document.getElementById("updateMsg").textContent));
});

await fakeScenario("weblink-only-writeValue", "x=1", { bluefy: true }, async page => {
  await page.click("#btnLink");
  await ready(page);
  await page.fill("#sDelay", "700");
  await page.click("#btnSave");
  await page.waitForFunction(() => document.getElementById("settingsMsg").textContent === "Saved.");
  await page.setInputFiles("#fwFile", BIN);
  await page.waitForFunction(() => !document.getElementById("btnUpdate").disabled);
  await page.click("#btnUpdate");
  await page.waitForFunction(() => /The new image runs/.test(document.getElementById("updateMsg").textContent), null, { timeout: 120000 });
  const w = await page.evaluate(() => __fake.writes);
  assert.ok(w.plain > 200 && w.withResponse === 0 && w.withoutResponse === 0, JSON.stringify(w));
});

console.log(results.join("\n"));
await browser.close();
process.exit(results.some(r => r.startsWith("FAIL")) ? 1 : 0);
