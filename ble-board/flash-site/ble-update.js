// Firmware update of the board over Bluetooth, for the flash site. Protocol: main/scrl_ota.h.
// The update loop is the one of the control app (tsl6-webble public/app/scrl-link.js, ScrlLink.update), without
// the TSL6 layer: the same credit window, the same chunk steps, the same reconnect after the restart.
//
// The board needs Bluetooth pairing. Its first protected read (Status) starts it: the phone asks for the passkey.
import * as SC from "./scrl.js";

const sleep = ms => new Promise(r => setTimeout(r, ms));
const dec = new TextDecoder();
const O = SC.OTA;
const CHUNK_KEY = "scrl-flash.chunk";

function errText(e) {
  if (e == null || typeof e !== "object") return String(e);
  const name = e.name && e.name !== "Error" ? `${e.name} ` : "";
  return `${name}${e.message || String(e)}`;
}
function store(key, val) {
  try { if (val === undefined) return localStorage.getItem(key); localStorage.setItem(key, val); } catch { /* storage blocked */ }
  return null;
}

export const bluetoothSupported = () => !!(globalThis.navigator && navigator.bluetooth);

// The chooser shows the boards: name SCRL-xxxx, or the board service.
export function pickBoard() {
  return navigator.bluetooth.requestDevice({
    filters: [{ namePrefix: "SCRL-" }, { services: [SC.UUIDS.service] }],
    optionalServices: [SC.UUIDS.service],
  });
}

export class BoardLink {
  // log(text, tone): one line for the page. onChange(): the board state changed. onGone(): the link dropped.
  constructor(device, { log = () => {}, onChange = () => {}, onGone = () => {} } = {}) {
    this.device = device;
    this.log = log; this.onChange = onChange; this.onGoneCb = onGone;
    this.chr = {}; this.queue = Promise.resolve(); this.gen = 0;
    this.info = null; this.status = null; this.statusAt = 0;
    this.connected = false; this.updating = false; this.expectRestart = false; this.pollTimer = 0;
    this.otaWaiters = []; this.otaCredit = 0; this.otaStopped = null;
    this.chunk = +store(CHUNK_KEY) || SC.platformChunk(navigator.userAgent);
    this.onDisc = () => this.gone();
    device.addEventListener("gattserverdisconnected", this.onDisc);
  }
  get name() { return this.device.name || "BLE board"; }
  get gattUp() { return !!(this.device.gatt && this.device.gatt.connected); }
  has(flag) { return !!(this.status && (this.status.flags & flag)); }

  // ---- GATT: one operation at a time
  op(fn) { const job = this.queue.then(fn); this.queue = job.catch(() => {}); return job; }
  read(name) { return this.op(async () => { const v = await this.chr[name].readValue(); return new Uint8Array(v.buffer, v.byteOffset, v.byteLength); }); }
  write(name, bytes, noResponse = false) {
    return this.op(async () => {
      const c = this.chr[name];
      if (noResponse && typeof c.writeValueWithoutResponse === "function") await c.writeValueWithoutResponse(bytes);
      else if (!noResponse && typeof c.writeValueWithResponse === "function") await c.writeValueWithResponse(bytes);
      else await c.writeValue(bytes);
    });
  }
  subscribe(name, cb) {
    const gen = this.gen;
    return this.op(async () => {
      const c = this.chr[name];
      c.addEventListener("characteristicvaluechanged", e => {
        if (gen !== this.gen || e.target !== this.chr[name]) return; // a listener of an older connection
        const v = e.target.value;
        cb(new Uint8Array(v.buffer, v.byteOffset, v.byteLength));
      });
      await c.startNotifications();
    });
  }

  // ---- connection
  async gattOpen() {
    this.gen++;
    this.queue = Promise.resolve();
    const server = await this.device.gatt.connect();
    const svc = await server.getPrimaryService(SC.UUIDS.service);
    for (const name of ["info", "status", "command", "otaCtrl", "otaData"]) this.chr[name] = await svc.getCharacteristic(SC.UUIDS[name]);
  }
  // Info is open. The first Status read starts the pairing. The board drops a link with no pairing after 30 s:
  // read at once, and again until the read works or the link drops. onPairing(): the read fails or takes long.
  async session(onPairing = () => {}) {
    this.info = null; this.status = null;
    try { this.info = JSON.parse(dec.decode(await this.read("info"))); } catch (e) { this.log(`Info read failed: ${errText(e)}`, "bad"); }
    let hinted = false;
    const hint = () => { if (!hinted) { hinted = true; onPairing(); } };
    const slow = setTimeout(hint, 2000);
    let first = null, lastErr = null;
    const until = Date.now() + 45000;
    while (!first && this.gattUp && Date.now() < until) {
      try { first = await this.read("status"); } catch (e) { lastErr = e; hint(); await sleep(1500); }
    }
    clearTimeout(slow);
    if (!first) throw new Error(this.gattUp ? `no secure link (${lastErr ? errText(lastErr) : "no answer"}). Pair with the passkey of the board` : "the board dropped the link");
    this.takeStatus(first);
    await this.subscribe("otaCtrl", b => { try { this.onOtaEvent(SC.parseOtaEvent(b)); } catch (e) { this.log(e.message, "bad"); } });
    this.connected = true;
    clearInterval(this.pollTimer);
    this.pollTimer = setInterval(() => { if (this.connected && !this.updating) this.refresh().catch(() => {}); }, 3000);
    this.onChange();
  }
  async open(onPairing) { await this.gattOpen(); await this.session(onPairing); }
  takeStatus(bytes) { this.status = SC.parseStatus(dec.decode(bytes)); this.statusAt = Date.now(); }
  async refresh() { this.takeStatus(await this.read("status")); this.onChange(); return this.status; }
  gone() {
    clearInterval(this.pollTimer);
    for (const w of this.otaWaiters.splice(0)) w.resolve(null);
    const was = this.connected;
    this.connected = false;
    if (this.expectRestart) return; // the update connects again by itself
    if (was) this.onGoneCb();
  }
  close() {
    this.expectRestart = false;
    clearInterval(this.pollTimer);
    this.gen++;
    this.device.removeEventListener("gattserverdisconnected", this.onDisc);
    if (this.gattUp) this.device.gatt.disconnect();
    this.connected = false;
  }

  // Seconds until an unconfirmed image goes back to the old one.
  pendingLeft() {
    if (!this.status) return 0;
    const up = this.status.uptime + Math.floor((Date.now() - this.statusAt) / 1000);
    return Math.max(0, O.CONFIRM_MS / 1000 - up);
  }
  async confirmImage() {
    await this.write("command", Uint8Array.of(SC.CMD.CONFIRM_IMAGE));
    await this.refresh();
    return !this.has(SC.FLAG.IMAGE_PENDING);
  }

  // ---- update
  onOtaEvent(ev) {
    const i = this.otaWaiters.findIndex(w => w.accept(ev));
    if (i >= 0) this.otaWaiters.splice(i, 1)[0].resolve(ev);
    else if (ev.event === O.EVT_CREDIT) this.otaCredit = Math.max(this.otaCredit, ev.offset);
    else if (ev.event === O.EVT_STOPPED) this.otaStopped = ev;
  }
  // Arm the waiter before the write: the board notifies from inside its write handler.
  waitOta(accept, timeout) {
    let w;
    const got = new Promise(resolve => { w = { accept, resolve }; this.otaWaiters.push(w); });
    return Promise.race([got, sleep(timeout).then(() => { this.otaWaiters = this.otaWaiters.filter(x => x !== w); return null; })]);
  }

  // image = { bytes, sha }. hooks: onText(text), onProgress(sent, size), onBegun(), onSent(), aborted() -> bool,
  // onPairing(). Resolves { pending } when the board is back. Throws an Error; e.nextChunk is set when the next
  // try uses fewer bytes per write.
  async update(image, hooks = {}) {
    const say = t => { this.log(t); if (hooks.onText) hooks.onText(t); };
    const size = image.bytes.length, chunk = this.chunk;
    if (this.updating) throw new Error("an update runs");
    this.updating = true; this.otaCredit = 0; this.otaStopped = null;
    let begun = false; // the board took BEGIN and END is not sent: an error sends ABORT
    const fail = (text, { cut = false } = {}) => {
      const e = new Error(text);
      if (cut) { const next = SC.nextChunk(chunk); if (next) { this.chunk = next; store(CHUNK_KEY, String(next)); e.nextChunk = next; } }
      throw e;
    };
    try {
      say("The board prepares the update.");
      let wait = this.waitOta(e => e.event === O.EVT_BEGIN || e.event === O.EVT_STOPPED, 20000);
      await this.write("otaCtrl", SC.otaBegin(size, image.sha));
      let ev = await wait;
      if (!ev) fail("the board did not answer the start request");
      if (ev.status !== SC.OTA_ERR.OK) fail(SC.otaErrText(ev.status));
      begun = true;
      if (hooks.onBegun) hooks.onBegun();

      let sent = 0;
      while (sent < size) {
        if (hooks.aborted && hooks.aborted()) fail("you stopped the update");
        if (this.otaStopped) fail(SC.otaErrText(this.otaStopped.status));
        const n = Math.min(chunk, size - sent);
        if (!SC.mayWrite(sent, n, this.otaCredit)) {
          ev = await this.waitOta(e => e.event === O.EVT_CREDIT || e.event === O.EVT_STOPPED, 10000);
          if (!ev) fail(`the board sent no credit for 10 s. A write of ${chunk} bytes can be too long for this link`, { cut: true });
          if (ev.event === O.EVT_STOPPED) fail(SC.otaErrText(ev.status));
          this.otaCredit = Math.max(this.otaCredit, ev.offset);
        }
        if (!SC.creditIsWhole(this.otaCredit, chunk, size)) fail(`the link cut a write of ${chunk} bytes`, { cut: true });
        if (!SC.mayWrite(sent, n, this.otaCredit)) continue;
        try {
          await this.write("otaData", image.bytes.subarray(sent, sent + n), true);
        } catch (e) {
          if (!this.gattUp) fail("the board disconnected");
          fail(`the browser refused a write of ${n} bytes (${errText(e)})`, { cut: true });
        }
        sent += n;
        if (hooks.onProgress) hooks.onProgress(sent, size);
      }
      if (hooks.onSent) hooks.onSent();
      // The last CREDIT has the full size: every byte is in the flash.
      const deadline = Date.now() + 15000;
      while (this.otaCredit < size) {
        if (this.otaStopped) fail(SC.otaErrText(this.otaStopped.status));
        if (Date.now() > deadline) fail(`the board has ${this.otaCredit} of ${size} bytes: writes of ${chunk} bytes were cut or lost`, { cut: true });
        ev = await this.waitOta(e => e.event === O.EVT_CREDIT || e.event === O.EVT_STOPPED, 2000);
        if (ev && ev.event === O.EVT_STOPPED) fail(SC.otaErrText(ev.status));
        if (ev) this.otaCredit = Math.max(this.otaCredit, ev.offset);
      }
      say("The board checks the image.");
      this.expectRestart = true; // the board restarts about 1.5 s after a good END
      wait = this.waitOta(e => e.event === O.EVT_END || e.event === O.EVT_STOPPED, 60000);
      begun = false; // from here the board ends the update by itself, good or bad
      await this.write("otaCtrl", Uint8Array.of(O.OP_END));
      ev = await wait;
      if (ev && ev.status !== SC.OTA_ERR.OK) fail(SC.otaErrText(ev.status));
      if (!ev && this.gattUp) fail("the board did not answer the end request");
      return await this.afterUpdate(say, hooks.onPairing);
    } catch (e) {
      this.expectRestart = false;
      if (begun && this.gattUp) { try { await this.write("otaCtrl", Uint8Array.of(O.OP_ABORT)); } catch { /* the link is gone */ } }
      if (!this.gattUp && this.connected) { this.connected = false; this.onGoneCb(); }
      throw e;
    } finally {
      this.updating = false;
      if (this.connected) this.refresh().catch(() => {});
    }
  }
  // The board restarts into the new image. Connect to the same device again, with no new chooser.
  async afterUpdate(say, onPairing) {
    say("The board restarts into the new image.");
    const gone = Date.now() + 8000;
    while (this.gattUp && Date.now() < gone) await sleep(200);
    if (this.gattUp) this.device.gatt.disconnect();
    this.connected = false;
    this.onChange();
    await sleep(1500);
    say("Connecting to the board again.");
    const until = Date.now() + 40000;
    let lastErr = null;
    while (Date.now() < until) {
      try {
        await this.gattOpen();
        this.expectRestart = false;
        await this.session(onPairing);
        return { pending: this.has(SC.FLAG.IMAGE_PENDING) };
      } catch (e) { lastErr = e; if (this.gattUp) this.device.gatt.disconnect(); await sleep(2000); }
    }
    this.expectRestart = false;
    throw new Error(`the board did not come back (${lastErr ? errText(lastErr) : "no answer"}). Connect again and confirm the image. Without a confirm it goes back to the old image after 10 minutes`);
  }
}
