#!/usr/bin/env python3
"""Build the USB flash site for the BLE board firmware.

    tools/make-flash-site.py            after tools/build-boards.sh

Reads boards/<build-id>.defaults (target), flash-site/boards.json (text),
build/out/scrl-<build-id>-full.bin (the image at offset 0, USB) and
build/out/scrl-<build-id>.bin (the app image, Bluetooth update). Writes
build/flash-site/: index.html, scrl.js, ble-update.js, builds.json and the
images in firmware/.

flash-site/scrl.js is a copy of the protocol codec of the control app. The
script stops when its UUIDs, commands or OTA values differ from the firmware
headers.

Every build in boards/ must have an image and a text entry, else the script
stops: the site never shows a board without its image.
"""
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build" / "flash-site"
CHIP_FAMILY = {"esp32": "ESP32", "esp32s3": "ESP32-S3", "esp32h2": "ESP32-H2"}


def project_version() -> str:
    m = re.search(r'set\(PROJECT_VER "([^"]+)"\)', (ROOT / "CMakeLists.txt").read_text())
    if not m:
        sys.exit("PROJECT_VER not found in CMakeLists.txt")
    return m.group(1)


def git_commit() -> str:
    out = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"],
                         capture_output=True, text=True, check=True).stdout.strip()
    dirty = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain", "--", "main", "boards",
                            "sdkconfig.defaults", "CMakeLists.txt", "partitions.csv"],
                           capture_output=True, text=True, check=True).stdout.strip()
    return out + ("-dirty" if dirty else "")


def image_version(app: Path) -> str:
    """Version in esp_app_desc_t of an app image: magic 0xABCD5432 at offset 32, version at offset 48."""
    b = app.read_bytes()[:96]
    if len(b) < 80 or int.from_bytes(b[32:36], "little") != 0xABCD5432:
        return ""
    return b[48:80].split(b"\0", 1)[0].decode("ascii", "replace")


def defines(path: Path, prefix: str) -> dict:
    """#define <prefix>NAME value, for plain integer values and (a * b * c)."""
    out = {}
    for m in re.finditer(rf"^#define {prefix}(\w+)\s+(.+?)\s*(?://.*)?$", path.read_text(), re.M):
        expr = m.group(2).replace("u", "")
        if re.fullmatch(r"[0-9xA-Fa-f()*\s]+", expr):
            out[m.group(1)] = eval(expr)  # only digits, x, parentheses and *
    return out


def check_codec() -> list:
    """Compare flash-site/scrl.js with main/scrl_ble.h and main/scrl_ota.h."""
    js = (ROOT / "flash-site" / "scrl.js").read_text()
    errors = []

    def js_obj(name: str) -> dict:
        m = re.search(rf"^const {name} = \{{(.*?)\}};", js, re.M | re.S)
        if not m:
            errors.append(f"flash-site/scrl.js: no const {name}")
            return {}
        return {k: int(v, 0) for k, v in re.findall(r"(\w+): (0x[0-9A-Fa-f]+|\d+)", m.group(1))}

    ble_h = ROOT / "main" / "scrl_ble.h"
    if not re.search(r"e591xxxx-51ba-4d0b-8422-5cb9dc778552", ble_h.read_text()) or \
            "-51ba-4d0b-8422-5cb9dc778552" not in js:
        errors.append("flash-site/scrl.js: the UUID base differs from main/scrl_ble.h")
    # The page sends only CONFIRM_IMAGE. Newer firmware commands may be missing from the copy, but a command
    # that the copy has must have the firmware value.
    fw_cmd, js_cmd = defines(ble_h, "SCRL_CMD_"), js_obj("CMD")
    if "CONFIRM_IMAGE" not in js_cmd:
        errors.append("flash-site/scrl.js: no CMD.CONFIRM_IMAGE")
    for name, got in js_cmd.items():
        if fw_cmd.get(name) != got:
            errors.append(f"flash-site/scrl.js: CMD.{name} is {got}, main/scrl_ble.h says {fw_cmd.get(name)}")
    ota = defines(ROOT / "main" / "scrl_ota.h", "SCRL_OTA_")
    js_ota, js_err = js_obj("OTA"), js_obj("OTA_ERR")
    for name, want in ota.items():
        if name.startswith("ERR_"):
            got = js_err.get(name[4:])
        elif name == "OK":
            got = js_err.get("OK")
        else:
            got = js_ota.get(name)
        if got != want:
            errors.append(f"flash-site/scrl.js: SCRL_OTA_{name} is {got}, the firmware says {want} (main/scrl_ota.h)")
    return errors


def main() -> None:
    text = json.loads((ROOT / "flash-site" / "boards.json").read_text())["builds"]
    commit = git_commit()
    builds = []
    errors = check_codec()
    # Site order = the order in boards.json. Every build in boards/ must be there.
    known = {d.stem for d in (ROOT / "boards").glob("*.defaults")}
    errors += [f"{bid}: in boards/ but not in flash-site/boards.json" for bid in sorted(known - set(text))]
    for bid in [b for b in text if b in known]:
        defaults = ROOT / "boards" / f"{bid}.defaults"
        m = re.search(r'^CONFIG_IDF_TARGET="(\w+)"', defaults.read_text(), re.M)
        target = m.group(1) if m else ""
        image = ROOT / "build" / "out" / f"scrl-{bid}-full.bin"
        if not image.is_file():
            errors.append(f"{bid}: no {image.relative_to(ROOT)} (run tools/build-boards.sh)")
        if target not in CHIP_FAMILY:
            errors.append(f"{bid}: unknown target {target!r}")
        if not image.is_file() or target not in CHIP_FAMILY:
            continue
        sdk = ROOT / "build" / bid / "sdkconfig"
        m = re.search(r'^CONFIG_SCRL_BOARD_ID="([\w-]+)"', sdk.read_text(), re.M) if sdk.is_file() else None
        if not m:
            errors.append(f"{bid}: no CONFIG_SCRL_BOARD_ID in {sdk.relative_to(ROOT)}")
            continue
        data = image.read_bytes()
        app = ROOT / "build" / "out" / f"scrl-{bid}.bin"
        if not app.is_file():
            errors.append(f"{bid}: no {app.relative_to(ROOT)} (run tools/build-boards.sh)")
            continue
        ver = image_version(app)
        if not ver:
            errors.append(f"{bid}: no app description in {app.relative_to(ROOT)}")
        builds.append({"id": bid, "version": ver, "board": m.group(1), "target": target, "chip": CHIP_FAMILY[target],
                       "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                       "app": app.name, "app_sha256": hashlib.sha256(app.read_bytes()).hexdigest(), **text[bid]})
    extra = sorted(set(text) - known)
    errors += [f"{bid}: in boards.json but not in boards/" for bid in extra]
    # The site shows one version: the version inside the images, not PROJECT_VER, which can be ahead of them.
    versions = sorted({b["version"] for b in builds})
    if len(versions) > 1:
        errors.append("the images have different versions: " + ", ".join(
            f"{v} ({', '.join(b['id'] for b in builds if b['version'] == v)})" for v in versions))
    elif versions and versions[0] != project_version():
        print(f"note: the images are {versions[0]}, CMakeLists.txt says {project_version()}. Run tools/build-boards.sh "
              "for a site with the new version.", file=sys.stderr)
    version = versions[0] if versions else project_version()
    if errors:
        sys.exit("\n".join(errors))

    if OUT.exists():
        shutil.rmtree(OUT)
    (OUT / "firmware").mkdir(parents=True)
    for b in builds:
        shutil.copy(ROOT / "build" / "out" / f"scrl-{b['id']}-full.bin", OUT / "firmware" / f"scrl-{b['id']}-full.bin")
        shutil.copy(ROOT / "build" / "out" / b["app"], OUT / "firmware" / b["app"])
    (OUT / "builds.json").write_text(json.dumps({"version": version, "commit": commit, "builds": builds}, indent=1) + "\n")
    for name in ("index.html", "scrl.js", "ble-update.js"):
        shutil.copy(ROOT / "flash-site" / name, OUT / name)
    print(f"wrote {OUT.relative_to(ROOT)}: {len(builds)} builds, firmware {version} ({commit})")


if __name__ == "__main__":
    main()
