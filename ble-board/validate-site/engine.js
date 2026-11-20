// Rule engine of the validation site. Pure functions: no BLE, no DOM, no storage. Works in the browser and in Node.
// The rule format is in validate-site/rules.json ("about"). The Status layout is in main/scrl_status.h.

const isCount = v => Number.isInteger(v) && v >= 0 && v <= 0xFFFFFFFF;

// Status JSON of the board. Throws when a field is missing or has the wrong type.
export function parseStatus(text) {
  const j = typeof text === "string" ? JSON.parse(text) : text;
  const d = j && j.scroll_ack_diag;
  const need = (ok, what) => { if (!ok) throw new Error(`Status JSON: bad ${what}`); };
  need(d && typeof d === "object", "scroll_ack_diag");
  need(isCount(j.scroll_ack_sent_count), "scroll_ack_sent_count");
  for (const k of ["das_ap_state", "das_hands_on"]) need(isCount(d[k]), k);
  need(Array.isArray(j.rx) && j.rx.length === 8 && j.rx.every(isCount), "rx");
  need(Array.isArray(j.tx) && j.tx.length === 3 && j.tx.every(isCount), "tx");
  for (const k of ["op", "flags", "boff", "rst", "pwr", "up"]) need(isCount(j[k]), k);
  need(Number.isInteger(j.rxpin), "rxpin");
  return {
    sent: j.scroll_ack_sent_count, apState: d.das_ap_state, handsOn: d.das_hands_on, op: j.op, flags: j.flags,
    rx: { all: j.rx[0], x3c2: j.rx[1], x39b: j.rx[2], x370: j.rx[3], x257: j.rx[4], x318: j.rx[5], x129: j.rx[6], dropped: j.rx[7] },
    tx: { ok: j.tx[0], failed: j.tx[1], refused: j.tx[2] },
    busOff: j.boff, rst: j.rst, pwr: j.pwr, up: j.up, rxPin: j.rxpin,
  };
}

// Live record, 19 bytes, little-endian (scrl_status.h). Only the fields that the rules read.
export function parseLive(b) {
  if (!b || b.length < 19) throw new Error(`Live record has ${b ? b.length : 0} bytes, need 19`);
  const u32 = o => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;
  return { apState: b[2], handsOn: b[3], flagsLow: b[4], sent: u32(5) };
}

export function parseSettings(text) {
  const j = typeof text === "string" ? JSON.parse(text) : text;
  if (!j || typeof j !== "object" || typeof j.enabled !== "boolean") throw new Error("Settings JSON: bad enabled");
  for (const k of ["mode", "op_mode"]) if (!Number.isInteger(j[k])) throw new Error(`Settings JSON: bad ${k}`);
  return j;
}

// One flat sample for the rules. Each part can be null: its fields are then unknown.
// live: a Live record that is newer than the Status read. It replaces the AP state, hands-on, sent count and the
// low flag byte (Live carries only bits 0 to 7). dSent: ticks since the last sample of this connection.
// prevHandsOn: hands-on of the last sample. The ticks clear the prompt, so when the sent count rises the hands-on
// level can already be 0. "handsOn.recent" is the higher level of the two samples.
export function makeSample({ info = null, status = null, settings = null, live = null, dSent = null, prevHandsOn = null } = {}) {
  const f = {};
  if (info) for (const k of ["fw", "board", "can", "chip", "build"]) if (info[k] !== undefined) f[`info.${k}`] = info[k];
  let flags = null;
  if (status) {
    f["status.ok"] = true;
    for (const [k, v] of Object.entries(status.rx)) f[`rx.${k}`] = v;
    for (const [k, v] of Object.entries(status.tx)) f[`tx.${k}`] = v;
    for (const k of ["busOff", "op", "rxPin", "up", "apState", "handsOn", "sent"]) f[k] = status[k];
    flags = status.flags;
  }
  if (live) {
    f.apState = live.apState; f.handsOn = live.handsOn; f.sent = live.sent;
    if (flags !== null) flags = ((flags & 0xFF00) | (live.flagsLow & 0xFF)) >>> 0;
  }
  if (settings) for (const k of ["enabled", "mode", "op_mode"]) f[`set.${k}`] = settings[k];
  if (dSent !== null) f["d.sent"] = dSent;
  if (f.handsOn !== undefined) f["handsOn.recent"] = Math.max(f.handsOn, prevHandsOn ?? 0);
  return { f, flags };
}

// Ticks since the last sample. A smaller count means a restart or a counter reset: no new tick is proven.
export const sentDelta = (prev, now) => (prev == null || now == null || now < prev ? 0 : now - prev);

const OPS = {
  "==": (a, b) => a === b, "!=": (a, b) => a !== b, ">": (a, b) => a > b, ">=": (a, b) => a >= b,
  "<": (a, b) => a < b, "<=": (a, b) => a <= b, in: (a, b) => Array.isArray(b) && b.includes(a),
};

// true, false, or null (no data).
export function evalCond(cond, sample, ctx) {
  if (cond[0] === "flag" || cond[0] === "!flag") {
    if (sample.flags === null || sample.flags === undefined) return null;
    const bit = ctx.flags[cond[1]];
    if (bit === undefined) throw new Error(`unknown flag ${cond[1]}`);
    const on = (sample.flags & bit) !== 0;
    return cond[0] === "flag" ? on : !on;
  }
  const [field, op, raw] = cond;
  if (!(op in OPS)) throw new Error(`unknown op ${op}`);
  const v = sample.f[field];
  if (v === undefined || v === null) return null;
  return OPS[op](v, raw === "$version" ? ctx.version : raw);
}

const evalAll = (conds, s, ctx) => {
  let unknown = false;
  for (const c of conds) { const r = evalCond(c, s, ctx); if (r === false) return false; if (r === null) unknown = true; }
  return unknown ? null : true;
};

// The values that a rule read, for the evidence text.
function evidence(rule, sample) {
  const out = [];
  const seen = new Set();
  for (const c of [...(rule.pass || []), ...(rule.fail || [])]) {
    const key = c[0] === "flag" || c[0] === "!flag" ? c[1] : c[0];
    if (seen.has(key)) continue;
    seen.add(key);
    if (c[0] === "flag" || c[0] === "!flag") continue;
    const v = sample.f[c[0]];
    if (v !== undefined) out.push(`${c[0]}=${JSON.stringify(v)}`);
  }
  const flagNames = [...seen].filter(k => /^[A-Z0-9_]+$/.test(k));
  return { values: out.join(", "), flagNames };
}

// Result of one rule on one sample: { state: "pass" | "fail" | "none", warn, evidence }.
export function evalRule(rule, sample, ctx) {
  let state = "none";
  if (rule.fail && rule.fail.some(c => evalCond(c, sample, ctx) === true)) state = "fail";
  else if (rule.pass && evalAll(rule.pass, sample, ctx) === true) state = "pass";
  let warn = null;
  if (rule.warn && evalAll(rule.warn[0], sample, ctx) === true) warn = rule.warn[1];
  const ev = evidence(rule, sample);
  const flagText = ev.flagNames.map(n => `${n}=${sample.flags !== null && sample.flags !== undefined ? ((sample.flags & ctx.flags[n]) ? 1 : 0) : "?"}`);
  return { state, warn, evidence: [ev.values, ...flagText].filter(Boolean).join(", ") };
}

// True when a check of the README is for this board.
export const appliesTo = (check, board) => !check.boards || !board || check.boards.includes(board);

// Store one auto result into the record of a check. meta = { at, fw, build, source }.
// A pass stays (the counters reset at power-on). A fail replaces it and is also kept in lastFail.
// "none" changes nothing. A pass of the same firmware keeps its first time.
export function mergeAuto(rec, result, meta) {
  const r = { ...(rec || {}) };
  if (result.state === "pass") {
    if (!(r.auto && r.auto.state === "pass" && r.auto.fw === meta.fw)) r.auto = { state: "pass", ...meta, evidence: result.evidence };
  } else if (result.state === "fail") {
    r.auto = { state: "fail", ...meta, evidence: result.evidence };
    r.lastFail = { ...meta, evidence: result.evidence };
  }
  return r;
}

// State of a check for the list: "pass", "done" (ticked by hand), "fail", "old" (a pass of other firmware), "open".
export function checkState(rec, fw) {
  if (rec && rec.auto && rec.auto.state === "fail") return "fail";
  const pass = rec && rec.auto && rec.auto.state === "pass";
  if (pass && !(fw && rec.auto.fw !== fw)) return "pass";
  if (rec && rec.manual && rec.manual.done) return "done";
  return pass ? "old" : "open";
}
export const isFinished = s => s === "pass" || s === "done";

// The first check in README order that is for this board and is not finished.
export function nextCheck(checks, records, board, fw) {
  return checks.find(c => appliesTo(c, board) && !isFinished(checkState(records[c.id], fw))) || null;
}
