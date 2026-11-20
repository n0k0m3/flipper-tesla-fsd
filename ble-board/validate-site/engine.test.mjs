// Test of the validation site rule engine (validate-site/engine.js) on the real firmware packers.
//
//     make -C test scrl_vectors && node ble-board/validate-site/engine.test.mjs      (in the repo root)
//
// test/scrl_vectors prints the Status JSON, the Live record and the default Settings that the firmware makes.
// The engine has its own copy of the Status layout: this test fails when the two disagree.
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import * as E from "./engine.js";

const HERE = dirname(fileURLToPath(import.meta.url));
const BOARD = join(HERE, "..");
const vec = JSON.parse(execFileSync(join(BOARD, "..", "test", "scrl_vectors"), { encoding: "utf8" }));
const version = /set\(PROJECT_VER "([^"]+)"\)/.exec(readFileSync(join(BOARD, "CMakeLists.txt"), "utf8"))[1];
const flags = {};
for (const m of readFileSync(join(BOARD, "main", "scrl_status.h"), "utf8").matchAll(/^#define SCRL_FLAG_(\w+)\s+\(1u << (\d+)\)/gm)) flags[m[1]] = 1 << +m[2];
const rules = JSON.parse(readFileSync(join(HERE, "rules.json"), "utf8")).rules;
const ctx = { version, flags };
const ev = (id, s) => E.evalRule(rules[id], s, ctx).state;
const all = s => Object.fromEntries(Object.keys(rules).map(id => [id, ev(id, s)]));

// Real packer output. Sample Status: op 1, flags TX_ALLOWED|ACTIVE|ENABLED|HW4_SEEN, rx [1000,10,20,0,30,40,50,1],
// tx [8,2,3], boff 1, up 3600, rxpin 2.
const st = E.parseStatus(vec.status_json);
const big = E.parseStatus(vec.big_status_json);
const set = E.parseSettings(vec.default_settings_json);
const live = E.parseLive(Uint8Array.from(Buffer.from(vec.live_hex, "hex")));
assert.equal(st.flags, flags.TX_ALLOWED | flags.ACTIVE | flags.ENABLED | flags.HW4_SEEN);
assert.deepEqual([live.apState, live.handsOn], [st.apState, st.handsOn]);
assert.equal(live.sent, st.sent);

const r = all(E.makeSample({ info: { fw: version, board: "nanoh2-minican" }, status: st, settings: set, dSent: 0 }));
assert.deepEqual(r, {
  "usb-flash": "pass", "ble-pair": "pass", "defaults-safe": "pass", "ble-update": "none", "update-rollback": "none",
  "can-running": "none", "frames-arrive": "pass", "nanoh2-rx-pin": "pass", "vehicle-bus": "none", "see-3c2": "pass",
  "see-39b": "pass", "see-257": "pass", "see-318": "pass", "drive-listen": "none", "no-bus-errors": "fail",
  "nanoh2-pins-build": "none", "go-active": "pass", "tx-allowed": "pass", "timer-ticks": "none", "prompt-3": "none",
  "prompt-4": "none", "prompt-5": "none", "tx-no-fail": "fail",
});

// No data: no rule passes or fails. Info only: only usb-flash can pass.
for (const v of Object.values(all(E.makeSample({})))) assert.equal(v, "none");
for (const [id, v] of Object.entries(all(E.makeSample({ info: { fw: version } })))) assert.equal(v, id === "usb-flash" ? "pass" : "none", id);
assert.equal(ev("usb-flash", E.makeSample({ info: { fw: "0.0.1" } })), "none");

// The big Status has every flag: WRONG_BUS fails the bus check, IMAGE_PENDING warns.
assert.equal(ev("vehicle-bus", E.makeSample({ status: big })), "fail");
assert.ok(E.evalRule(rules["ble-update"], E.makeSample({ status: big }), ctx).warn);
assert.equal(E.evalRule(rules["ble-update"], E.makeSample({ status: st }), ctx).warn, null);
assert.equal(ev("vehicle-bus", E.makeSample({ status: { ...st, flags: 0, rx: { ...st.rx, all: 20000 } } })), "pass");

// Active set on a build with no TX pin (NanoH2, pins not checked): the board stays in Listen-Only. Warn.
const noPin = { ...st, op: 1, flags: flags.ENABLED };
for (const id of ["go-active", "timer-ticks"]) {
  assert.ok(E.evalRule(rules[id], E.makeSample({ status: noPin }), ctx).warn, id);
  assert.equal(E.evalRule(rules[id], E.makeSample({ status: { ...noPin, flags: flags.ENABLED | flags.PINS_CHECKED } }), ctx).warn, null, id);
  assert.equal(E.evalRule(rules[id], E.makeSample({ status: { ...noPin, op: 0 } }), ctx).warn, null, id);
}

// Live replaces the low flag byte only.
const withLive = E.makeSample({ status: st, live: { ...live, flagsLow: 0 } });
assert.equal(withLive.flags, st.flags & 0xFF00);

// Prompt ticks. The ticks clear the prompt: the sample with the new ticks can show hands-on 0 already.
const prompt = { ...set, mode: 0 };
const seq = (levels, sents) => {
  let prevSent = null, prevHandsOn = null, out = [];
  levels.forEach((h, i) => {
    const s = E.makeSample({ status: st, settings: prompt, live: { ...live, handsOn: h, sent: sents[i] }, prevHandsOn,
      dSent: E.sentDelta(prevSent, sents[i]) });
    prevSent = sents[i]; prevHandsOn = h;
    out.push(["prompt-3", "prompt-4", "prompt-5"].filter(id => ev(id, s) === "pass"));
  });
  return out;
};
assert.deepEqual(seq([3, 0], [10, 12]), [[], ["prompt-3"]]);            // level 3, then 2 ticks at level 0
assert.deepEqual(seq([3, 4, 0], [10, 12, 14]), [[], ["prompt-4"], ["prompt-4"]]); // escalation 3 -> 4
assert.deepEqual(seq([5, 5], [10, 10]), [[], []]);                        // no new tick, no pass
assert.deepEqual(seq([0, 0], [10, 12]), [[], []]);                        // ticks with no prompt: not a prompt pass
assert.equal(ev("timer-ticks", E.makeSample({ status: st, settings: { ...set, mode: 1 }, dSent: 2 })), "pass");
assert.equal(ev("timer-ticks", E.makeSample({ status: st, settings: prompt, dSent: 2 })), "none");
assert.equal(E.sentDelta(null, 5), 0); assert.equal(E.sentDelta(5, 3), 0); assert.equal(E.sentDelta(5, 7), 2);

// Latch: a pass stays, a fail replaces it and stays in lastFail, other firmware makes a pass old.
let rec = E.mergeAuto(undefined, { state: "pass", evidence: "a" }, { at: "t1", fw: "0.3.2" });
rec = E.mergeAuto(rec, { state: "none", evidence: "" }, { at: "t2", fw: "0.3.2" });
rec = E.mergeAuto(rec, { state: "pass", evidence: "b" }, { at: "t3", fw: "0.3.2" });
assert.equal(rec.auto.at, "t1");
assert.equal(E.checkState(rec, "0.3.2"), "pass");
assert.equal(E.checkState(rec, "0.3.3"), "old");
rec = E.mergeAuto(rec, { state: "fail", evidence: "c" }, { at: "t4", fw: "0.3.2" });
assert.equal(E.checkState(rec, "0.3.2"), "fail");
rec = E.mergeAuto(rec, { state: "pass", evidence: "d" }, { at: "t5", fw: "0.3.2" });
assert.equal(E.checkState(rec, "0.3.2"), "pass");
assert.equal(rec.lastFail.at, "t4");
assert.equal(E.checkState({ manual: { done: true } }, "0.3.2"), "done");
assert.equal(E.checkState({ manual: { done: true }, auto: { state: "pass", fw: "0.3.1" } }, "0.3.2"), "done");

// Next check, with the board filter.
const checks = [{ id: "a", boards: ["nanoh2-minican"] }, { id: "b", boards: null }];
assert.equal(E.nextCheck(checks, {}, "nanoh2-minican", version).id, "a");
assert.equal(E.nextCheck(checks, {}, "lilygo-t2can", version).id, "b");
assert.equal(E.nextCheck(checks, { a: { manual: { done: true } }, b: { auto: { state: "pass", fw: version } } }, "nanoh2-minican", version), null);
console.log("validate-site engine: all checks pass");
