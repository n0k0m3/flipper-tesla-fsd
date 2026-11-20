#!/usr/bin/env python3
"""Build the validation site for the BLE board firmware.

    tools/make-validate-site.py

Reads the "## Validation" tables of README.md, validate-site/rules.json, the
SCRL_FLAG_* bits of main/scrl_status.h, the board IDs of main/Kconfig.projbuild
and PROJECT_VER of CMakeLists.txt. Writes build/validate-site/: index.html,
engine.js and checks.json.

It stops when an ID is not unique, a rule names an ID that is not in a table,
a rule names an unknown flag, field or op, or a Board cell names an unknown
board. The site never shows a rule that the README does not have.
"""
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "validate-site"
OUT = ROOT / "build" / "validate-site"

FIELDS = {"info.fw", "info.board", "info.can", "info.chip", "status.ok", "busOff", "op", "rxPin", "up",
          "apState", "handsOn", "handsOn.recent", "sent", "set.enabled", "set.mode", "set.op_mode", "d.sent"}
FIELDS |= {f"rx.{k}" for k in ("all", "x3c2", "x39b", "x370", "x257", "x318", "x129", "dropped")}
FIELDS |= {f"tx.{k}" for k in ("ok", "failed", "refused")}
OPS = {"==", "!=", ">", ">=", "<", "<=", "in"}


def project_version() -> str:
    m = re.search(r'set\(PROJECT_VER "([^"]+)"\)', (ROOT / "CMakeLists.txt").read_text())
    if not m:
        sys.exit("PROJECT_VER not found in CMakeLists.txt")
    return m.group(1)


def git_commit() -> str:
    out = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"],
                         capture_output=True, text=True, check=True).stdout.strip()
    dirty = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain", "--", "README.md", "validate-site"],
                           capture_output=True, text=True, check=True).stdout.strip()
    return out + ("-dirty" if dirty else "")


def flag_bits() -> dict:
    text = (ROOT / "main" / "scrl_status.h").read_text()
    flags = {m.group(1): 1 << int(m.group(2))
             for m in re.finditer(r"^#define SCRL_FLAG_(\w+)\s+\(1u << (\d+)\)", text, re.M)}
    if not flags:
        sys.exit("no SCRL_FLAG_* in main/scrl_status.h")
    return flags


def board_ids() -> list:
    text = (ROOT / "main" / "Kconfig.projbuild").read_text()
    block = re.search(r"config SCRL_BOARD_ID\n(.*?)\n\n", text, re.S)
    ids = re.findall(r'default "([\w-]+)" if', block.group(1)) if block else []
    if not ids:
        sys.exit("no SCRL_BOARD_ID defaults in main/Kconfig.projbuild")
    return ids


def inline(md: str) -> str:
    """Markdown cell text to the small subset the page shows: `code` stays, links lose the target."""
    md = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", md)
    return md.replace("**", "").strip()


def read_checks(errors: list, boards: list) -> list:
    text = (ROOT / "README.md").read_text()
    m = re.search(r"^## Validation\n(.*?)(?=^## )", text, re.M | re.S)
    if not m:
        sys.exit("no '## Validation' section in README.md")
    checks, group, intro = [], None, []
    for line in m.group(1).splitlines():
        if line.startswith("### "):
            group, intro = line[4:].strip(), []
            continue
        if group is None or group == "Validation site":
            continue
        if not line.startswith("|"):
            if line.strip():
                intro.append(inline(line))
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if cells[0] in ("ID", "") or set(cells[0]) <= set("-: "):
            continue
        if len(cells) != 4:
            errors.append(f"README row with {len(cells)} cells, need 4: {line}")
            continue
        cid = cells[0].strip("`")
        if not re.fullmatch(r"[a-z0-9][a-z0-9-]*", cid):
            errors.append(f"README: bad ID {cells[0]!r}")
        board_cell = cells[1]
        names = None if board_cell == "all" else [b.strip() for b in board_cell.split(",")]
        for b in names or []:
            if b not in boards:
                errors.append(f"{cid}: unknown board {b!r} (see SCRL_BOARD_ID in main/Kconfig.projbuild)")
        checks.append({"id": cid, "group": group, "groupIntro": " ".join(intro), "boards": names,
                       "action": inline(cells[2]), "pass": inline(cells[3])})
    return checks


def check_conds(cid: str, key: str, conds, flags: dict, errors: list) -> None:
    if not isinstance(conds, list):
        errors.append(f"{cid}.{key}: not a list")
        return
    for c in conds:
        if isinstance(c, list) and len(c) == 2 and c[0] in ("flag", "!flag"):
            if c[1] not in flags:
                errors.append(f"{cid}.{key}: unknown flag {c[1]!r} (SCRL_FLAG_* in main/scrl_status.h)")
        elif isinstance(c, list) and len(c) == 3:
            if c[0] not in FIELDS:
                errors.append(f"{cid}.{key}: unknown field {c[0]!r}")
            if c[1] not in OPS:
                errors.append(f"{cid}.{key}: unknown op {c[1]!r}")
        else:
            errors.append(f"{cid}.{key}: bad condition {c!r}")


def main() -> None:
    errors = []
    flags, boards = flag_bits(), board_ids()
    checks = read_checks(errors, boards)
    ids = [c["id"] for c in checks]
    errors += [f"README: ID {i!r} is not unique" for i in sorted({i for i in ids if ids.count(i) > 1})]
    rules = json.loads((SRC / "rules.json").read_text())["rules"]
    for cid, rule in rules.items():
        if cid not in ids:
            errors.append(f"rules.json: {cid!r} is not in the README Validation tables")
        unknown = set(rule) - {"pass", "fail", "warn"}
        if unknown:
            errors.append(f"{cid}: unknown keys {sorted(unknown)}")
        for key in ("pass", "fail"):
            if key in rule:
                check_conds(cid, key, rule[key], flags, errors)
        if "warn" in rule:
            w = rule["warn"]
            if not (isinstance(w, list) and len(w) == 2 and isinstance(w[1], str)):
                errors.append(f"{cid}.warn: need [conditions, text]")
            else:
                check_conds(cid, "warn", w[0], flags, errors)
    if errors:
        sys.exit("\n".join(errors))
    for c in checks:
        if c["id"] in rules:
            c["rule"] = rules[c["id"]]

    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)
    data = {"version": project_version(), "commit": git_commit(), "flags": flags, "boards": boards, "checks": checks}
    (OUT / "checks.json").write_text(json.dumps(data, indent=1, ensure_ascii=False) + "\n")
    for name in ("index.html", "engine.js"):
        shutil.copy(SRC / name, OUT / name)
    auto = sum(1 for c in checks if "rule" in c)
    print(f"wrote {OUT.relative_to(ROOT)}: {len(checks)} checks ({auto} from the board status), "
          f"firmware {data['version']} ({data['commit']})")


if __name__ == "__main__":
    main()
