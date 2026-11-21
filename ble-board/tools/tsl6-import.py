#!/usr/bin/env python3
"""Import the TSL6 feature code from the C rewrite into components/tsl6/.

    tools/tsl6-import.py ~/Programming/tsl6-firmware

The C rewrite (tsl6-firmware, fw/src) is a function-by-function rewrite of
the TSL6 runtime V1.0.00. This script copies its car feature modules,
applies the port patches below, and generates:

  components/tsl6/vendor/   the copied and patched sources (do not edit:
                            change the patch list here and run it again)
  components/tsl6/gen/      ram_regions.S (the globals at their stock offsets,
                            in 3 small regions) and resident_slots.c (the slot
                            table of the stock BLE ROM API)
  components/tsl6/IMPORT.md the source commit and the patch list

Not copied (platform, or security material): m1 ble/ota/sys/led/buzzer/uid/
memset/ctx, m5 ble_central*/ble_keys, m6 (CAN driver, filters, phone
protocol, flash tables: tsl6_tables.c in port/ replaces tables.c), m7
ble_cmd/link (holds the vendor key), m8 rf_link, sdk, image.

Each patch is an exact text replacement that must match the given number of
times. The script stops on the first patch that does not match.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent  # ble-board/
COMP = HERE / "components" / "tsl6"

# (module, files). "*" = every .c and .h of the module.
COPY = [
    ("m2", "*"),
    ("m3", "*"),
    ("m4", "*"),
    ("m5", ["m5.h", "ble_remote.h", "platform.c", "speed_steer.c", "gauge_vals.c", "frame_678.c",
            "func_hold.c", "bodyctl_3e9.c"]),
    ("m7", ["m7.h", "platform.c", "button.c"]),
    ("m8", ["m8.h", "seat.c", "tick.c", "cmd_a7.c", "light.c"]),
    ("m1", ["m1.h", "app_timer.h", "app_timer.c", "epas.c"]),
]
INCLUDE = ["types.h", "tsl6.h", "resident.h"]

# Functions copied from m1/main.c into vendor/m1/main_helpers.c. uid_crc_ok is
# not copied: the port stub returns 0, so the TSL6 scroll tick never starts
# (the board has its own Scroll Ack).
M1_HELPERS = ["fixed_mode_get", "turn_state_get", "epas_torque_mode_get", "bus_age_clear", "bus_alive",
              "range_edge", "car_frames_seen", "platform_get", "frame_slot_copy", "tick_is_on"]

# (file, old, new, count)
PATCHES = [
    # The base header: no WCH MCU header, no QingKe interrupt macro.
    ("include/tsl6.h", '#include "ch32v20x.h"\n', '#include "tsl6_port.h"\n', 1),
    ("m1/m1.h", '#include "ch32v20x.h"\n', '#include "tsl6_port.h"\n', 1),
    ("m1/m1.h", "gpio_init_pin(GPIO_TypeDef *port,", "gpio_init_pin(void *port,", 1),
    # The stock memset returns nothing; use the libc one (same arguments).
    ("m5/m5.h", "void memset(void *dst, uint32_t val, uint32_t len);\n", "#include <string.h>\n", 1),
    # The stock BLE ROM API is a table of function pointers at 0x40000. The
    # port fills its own table (gen/resident_slots.c).
    ("include/resident.h", "#define RESIDENT_SLOT(off) (*(void *const *)(0x40000u + (off)))",
     "extern void *const tsl6_resident[];\n#define RESIDENT_SLOT(off) (tsl6_resident[(off) >> 2])", 1),
    # Register a0 only mattered to the differential test against the stock
    # code. On Xtensa, a0 is the return address register.
    ("m3/m3.h", 'register uint32_t a0_ __asm__("a0") = (uint32_t)(v);    \\\n        __asm__ volatile("" : : "r"(a0_));                      \\\n',
     "(void)(v);                                              \\\n", 1),
    ("m5/m5.h", 'register uint32_t a0_ __asm__("a0") = (uint32_t)(v);     \\\n        __asm__ volatile("" : : "r"(a0_));                       \\\n',
     "(void)(v);                                               \\\n", 1),
    # The 0x370 EPAS torque echo faulted the car (traction and VDC). Never send it.
    ("m1/epas.c", "        c[10] = frame_checksum_last(c);\n        return can_tx(c);\n",
     "        c[10] = frame_checksum_last(c);\n        return 0; /* port: the 0x370 echo is never sent */\n", 1),
    # Key 2, ten clicks: the vendor activation (UID CRC, boot flag). Not ported.
    ("m4/keys.c", "                if (key == 2) {\n                    uid_crc();\n                    bootflag_set_crc();\n                    r = tune_scale(0);\n                }\n",
     "                if (key == 2)\n                    r = tune_scale(0); /* port: no vendor activation */\n", 1),
]

# m8/light.c event_fire pins registers (s0-s5) for the stock differential
# test. The port uses this plain C body (same logic).
EVENT_FIRE_C = '''uint32_t event_fire(uint32_t code)
{
    /* port: plain C. The stock version pins s0-s5 for the differential test. */
    uint32_t r = code;
    uint8_t *e;

    for (uint32_t i = 0; i != 254; i += 2) {
        e = g_evt_actions + i;
        if (e[0] == code)
            r = button_action(e[1]);
    }
    for (uint32_t i = 1, off = REC_LEN; i != 36; i = (i + 1) & 0xff, off += REC_LEN) {
        e = g_rfl_recs + off;
        if ((((e[1] & 1u) << 8) | e[0]) == code && (e[1] & 2))
            r = light_rec_apply(0, i);
        e = g_light_recs + off;
        if (code == (((e[1] & 1u) << 8) | e[0]) && (e[1] & 2))
            r = light_rec_apply(1, i);
    }
    return r;
}
'''

# Regions of the stock RAM that the feature code uses. Each keeps the stock
# offsets inside it (memsets over neighbouring globals, frame cache aliases).
REGIONS = [
    (0x20004000, 0x20004200),  # .data and module state
    (0x2000BE00, 0x2000C100),  # module state, app timers
    (0x2000C1D4, 0x2000C970),  # frame cache, config block, AB/BA/remote/RGB tables, RF state, light queue, seat
]


def run(cmd, cwd=None):
    return subprocess.run(cmd, cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def extract_function(src, name):
    m = re.search(r"^[^\n;]*\b" + re.escape(name) + r"\([^)]*\)\n\{", src, re.M)
    if not m:
        sys.exit(f"main.c: function {name} not found")
    start = src.rfind("\n\n", 0, m.start()) + 2  # with its comment
    depth, i = 0, src.index("{", m.start())
    while True:
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[start:i + 1] + "\n"
        i += 1


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    repo = Path(sys.argv[1]).expanduser().resolve()
    fw = repo / "fw"
    src = fw / "src"
    commit = run(["git", "rev-parse", "--short", "HEAD"], cwd=repo)
    dirty = run(["git", "status", "--porcelain", "--", "fw/src", "fw/include"], cwd=repo)

    vendor = COMP / "vendor"
    gen = COMP / "gen"
    shutil.rmtree(vendor, ignore_errors=True)
    shutil.rmtree(gen, ignore_errors=True)
    (vendor / "include").mkdir(parents=True)
    gen.mkdir(parents=True)

    for f in INCLUDE:
        shutil.copy(fw / "include" / f, vendor / "include" / f)
    for mod, files in COPY:
        (vendor / mod).mkdir()
        names = sorted(p.name for p in (src / mod).iterdir() if p.suffix in (".c", ".h")) if files == "*" else files
        for f in names:
            shutil.copy(src / mod / f, vendor / mod / f)

    main_c = (src / "m1" / "main.c").read_text()
    helpers = '/* Copied from m1/main.c by tools/tsl6-import.py. */\n#include "tsl6.h"\n#include "m1.h"\n\n'
    helpers += "\n".join(extract_function(main_c, n) for n in M1_HELPERS)
    (vendor / "m1" / "main_helpers.c").write_text(helpers)

    for path, old, new, count in PATCHES:
        p = vendor / path
        text = p.read_text()
        n = text.count(old)
        if n != count:
            sys.exit(f"patch {path}: found {n} times, want {count}:\n{old}")
        p.write_text(text.replace(old, new))

    light = vendor / "m8" / "light.c"
    text = light.read_text()
    m = re.search(r"#define KEEP\(\.\.\.\).*?\nuint32_t event_fire\(uint32_t code\)\n\{.*?\n\}\n", text, re.S)
    if not m:
        sys.exit("m8/light.c: event_fire not found")
    light.write_text(text[:m.start()] + EVENT_FIRE_C + text[m.end():])

    # Globals: every g_ name in the copied sources, at its stock address.
    provide = {}
    for line in (fw / "build" / "image" / "ram.ld").read_text().splitlines():
        m = re.match(r"PROVIDE\((\w+) = (0x[0-9a-f]+)\);", line)
        if m:
            provide[m.group(1)] = int(m.group(2), 16)
    used = set()
    for p in vendor.rglob("*.[ch]"):
        used |= set(re.findall(r"\bg_\w+", p.read_text()))
    used |= {"g_cfg_block", "g_ba_table", "g_remote_table", "g_ab_table", "g_rgb_zone", "g_rgb_car",
             "g_app_timers", "g_app_timer_task"}
    rows, missing, outside = [], [], []
    for name in sorted(used):
        addr = provide.get(name)
        if addr is None or not (0x20000000 <= addr < 0x20010000):
            missing.append(name)
            continue
        reg = next((i for i, (a, b) in enumerate(REGIONS) if a <= addr < b), None)
        if reg is None:
            outside.append(f"{name}={addr:#x}")
            continue
        rows.append((reg, addr, name))
    if outside:
        sys.exit("globals outside the regions: " + ", ".join(outside))
    asm = ["/* Generated by tools/tsl6-import.py. The TSL6 globals at their stock",
           "   offsets inside 4 regions. Do not edit. */"]
    for i, (a, b) in enumerate(REGIONS):
        asm += [f'.section .bss.tsl6_r{i},"aw",@nobits', ".balign 8", f".globl tsl6_r{i}",
                f"tsl6_r{i}: .zero {b - a:#x}  /* stock {a:#x}..{b:#x} */"]
    for reg, addr, name in sorted(rows):
        asm += [f".globl {name}", f".set {name}, tsl6_r{reg} + {addr - REGIONS[reg][0]:#x}"]
    (gen / "ram_regions.S").write_text("\n".join(asm) + "\n")
    (gen / "ram_regions.h").write_text(
        "/* Generated by tools/tsl6-import.py. */\n#pragma once\n#include <stdint.h>\n"
        + "".join(f"extern uint8_t tsl6_r{i}[{b - a:#x}]; /* stock {a:#x} */\n" for i, (a, b) in enumerate(REGIONS))
        )

    # Resident slot table: each slot of resident.h that the port implements
    # points to it, every other slot to a trap that logs the slot.
    res = (vendor / "include" / "resident.h").read_text()
    slots = re.findall(r"/\* slot (0x[0-9a-f]+): [^\n]*\*/\n(?:#define (\w+)|static inline [^(]*?\b(\w+)\()", res)
    impl = {"tmos_rand", "tmos_memcmp", "tmos_isbufset", "tmos_memset", "tmos_memcpy", "tmos_set_event",
            "tmos_clear_event", "tmos_start_task", "tmos_stop_task", "tmos_get_task_timer", "tmos_msg_deallocate",
            "tmos_msg_allocate", "TMOS_ProcessEventRegister", "TMOS_GetSystemClock", "tmos_start_reload_task",
            "tmos_msg_receive", "tmos_msg_send"}
    lines = ["/* Generated by tools/tsl6-import.py: the stock BLE ROM API slot table. */",
             '#include "tsl6_port.h"', ""]
    table = {}
    for off, mac, fn in slots:
        name = fn or mac
        idx = int(off, 16) >> 2
        if name in impl:
            table[idx] = f"(void *)port_{name}"
    # Slots that module code calls by number (RES32, msg_alloc32, m2.h, m8.h).
    for off, name in ((0x48, "tmos_memset"), (0x4C, "tmos_memcpy"), (0x58, "tmos_start_task"),
                      (0x5C, "tmos_stop_task"), (0x6C, "tmos_msg_receive"), (0x70, "tmos_msg_allocate"),
                      (0x80, "TMOS_ProcessEventRegister"), (0x22C, "tmos_start_reload_task")):
        table[off >> 2] = f"(void *)port_{name}"
    n = max(table) + 64
    lines.append(f"void *const tsl6_resident[{n}] = {{")
    for i in range(n):
        lines.append(f"    {table.get(i, f'(void *)port_resident_trap_{i:03x}')},")
    lines.append("};")
    traps = ["", "/* A call to a slot that the port does not implement logs it and returns 0. */"]
    for i in range(n):
        if i not in table:
            traps.append(f"static uint32_t port_resident_trap_{i:03x}(void) {{ return port_resident_trap({i * 4:#x}); }}")
    head = lines[:3] + traps + [""] + lines[3:]
    (gen / "resident_slots.c").write_text("\n".join(head) + "\n")

    (COMP / "IMPORT.md").write_text(
        f"# TSL6 feature code import\n\nSource: tsl6-firmware `{commit}`{' (with local changes)' if dirty else ''}, "
        f"`fw/src` and `fw/include`.\nMade by `tools/tsl6-import.py`. Do not edit `vendor/` or `gen/` by hand.\n\n"
        f"Globals: {len(rows)} names in {len(REGIONS)} regions, "
        f"{sum(b - a for a, b in REGIONS)} bytes. Names without a stock RAM address (functions or not used): "
        f"{len(missing)}.\n\nPatches:\n" + "".join(f"- `{p}`: {o.strip().splitlines()[0][:70]}\n" for p, o, _, _ in PATCHES)
        + "- `m8/light.c`: `event_fire` in plain C (no pinned registers)\n"
        + "- `m1/main_helpers.c`: " + ", ".join(M1_HELPERS) + " from `m1/main.c`\n")
    print(f"imported tsl6-firmware {commit}: {len(rows)} globals, {len(table)} resident slots implemented")


if __name__ == "__main__":
    main()
