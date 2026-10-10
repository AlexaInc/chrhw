#!/usr/bin/env python3
"""Validate a local Wokwi diagram and its custom-chip files.

Usage (from this project folder):
    python3 scripts/check-wokwi-diagram.py .

Checks diagram endpoints, local custom-chip manifests, Wokwi JSON keys and
WebAssembly magic bytes. This is a structural check, not an electrical-safety
or analog-simulation test.
"""
from __future__ import annotations

import json
import os
import re
import sys
from pathlib import Path

ALLOWED_CHIP_JSON_KEYS = {"name", "author", "pins", "controls", "display"}

BUILTIN = {
    "wokwi-resistor": ["1", "2"],
    "wokwi-text": [],
    "wokwi-esp32-devkit-v1": [
        "VIN", "3V3", "GND.1", "GND.2", "EN", "VP", "VN",
        "D2", "D4", "D5", "D12", "D13", "D14", "D15", "D16", "D17",
        "D18", "D19", "D21", "D22", "D23", "D25", "D26", "D27",
        "D32", "D33", "D34", "D35", "RX0", "TX0", "RX2", "TX2",
    ],
    "wokwi-hc-sr04": ["VCC", "TRIG", "ECHO", "GND"],
    "wokwi-dht22": ["VCC", "SDA", "NC", "GND"],
    "board-ssd1306": ["VCC", "GND", "SCL", "SDA"],
    "wokwi-microsd-card": ["CD", "DO", "GND", "SCK", "VCC", "DI", "CS"],
    "wokwi-servo": ["GND", "V+", "PWM"],
    "wokwi-potentiometer": ["GND", "SIG", "VCC"],
    "wokwi-relay-module": ["IN", "COM", "NO", "NC", "VCC", "GND"],
    "wokwi-led": ["A", "C"],
    "wokwi-vcc": ["VCC"],
    "wokwi-gnd": ["GND"],
    "$serialMonitor": ["RX", "TX"],
}
# These existing models intentionally keep some supply/unused pins visual-only.
KNOWN_PIN_INIT_EXCEPTIONS = {"gps", "espcam"}


def parse_chip_blocks(text: str):
    blocks = re.findall(r"(?ms)^\[\[chip\]\]\s*\n(.*?)(?=^\[\[|\Z)", text)
    entries = []
    for block in blocks:
        name = re.search(r'^\s*name\s*=\s*["\']([^"\']+)["\']', block, re.M)
        binary = re.search(r'^\s*binary\s*=\s*["\']([^"\']+)["\']', block, re.M)
        if name and binary:
            entries.append((name.group(1), binary.group(1)))
        else:
            entries.append((None, None))
    return entries


def check(project: str) -> int:
    project = os.path.abspath(project)
    print(f"== {project}")
    problems: list[str] = []
    diagram_path = os.path.join(project, "diagram.json")
    toml_path = os.path.join(project, "wokwi.toml")
    if not os.path.isfile(diagram_path):
        print("   no diagram.json - skipped")
        return 1
    if not os.path.isfile(toml_path):
        print("   no wokwi.toml")
        return 1

    try:
        doc = json.loads(Path(diagram_path).read_text(encoding="utf-8"))
        manifest = parse_chip_blocks(Path(toml_path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"   [PROBLEM] cannot read project config: {exc}")
        return 1

    manifest_map: dict[str, str] = {}
    for name, binary in manifest:
        if name is None or binary is None:
            problems.append("malformed [[chip]] block in wokwi.toml (needs name and binary)")
            continue
        if name in manifest_map:
            problems.append(f"duplicate [[chip]] name {name!r} in wokwi.toml")
        manifest_map[name] = binary
        wasm_path = Path(project, binary)
        json_path = wasm_path.with_suffix(".json")
        c_path = wasm_path.with_suffix(".c")
        if not wasm_path.is_file():
            problems.append(f"wokwi.toml chip {name} binary missing: {binary}")
        else:
            try:
                if wasm_path.read_bytes()[:4] != b"\x00asm":
                    problems.append(f"{binary} is not a WebAssembly binary (expected 00 61 73 6d)")
            except OSError as exc:
                problems.append(f"cannot read {binary}: {exc}")
        if not json_path.is_file():
            problems.append(f"wokwi.toml chip {name} pinout missing next to its binary: {json_path.name}")
        if not c_path.is_file():
            problems.append(f"wokwi.toml chip {name} source missing: {c_path.name}")

    ids: dict[str, str] = {}
    custom_pins: dict[str, list[str]] = {}
    for part in doc.get("parts", []):
        part_id = part.get("id")
        part_type = part.get("type")
        if not part_id or not part_type:
            problems.append(f"part missing id or type: {part}")
            continue
        if part_id in ids:
            problems.append(f"duplicate part id {part_id}")
        ids[part_id] = part_type
        if not part_type.startswith("chip-"):
            continue
        chip_name = part_type[5:]
        if chip_name not in manifest_map:
            problems.append(f"wokwi.toml has no [[chip]] block for diagram type {part_type}")
        if chip_name not in custom_pins:
            binary = manifest_map.get(chip_name, chip_name + ".chip.wasm")
            pinout = Path(project, binary).with_suffix(".json")
            if not pinout.is_file():
                problems.append(f"missing {pinout.name} for part {part_id} ({part_type})")
                custom_pins[part_type] = []
                continue
            try:
                chip_doc = json.loads(pinout.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as exc:
                problems.append(f"invalid {pinout.name}: {exc}")
                custom_pins[part_type] = []
                continue
            unknown = sorted(set(chip_doc) - ALLOWED_CHIP_JSON_KEYS)
            if unknown:
                problems.append(f"{pinout.name} has unsupported JSON key(s): {', '.join(unknown)}")
            pins = chip_doc.get("pins", [])
            if not isinstance(pins, list) or not all(isinstance(pin, str) for pin in pins):
                problems.append(f"{pinout.name} must define pins as an array of strings")
                pins = []
            custom_pins[part_type] = pins

            # Verify the C source initializes every named pin. Blank strings are
            # intentional skipped pins in Wokwi's custom-chip JSON format.
            source = Path(project, binary).with_suffix(".c")
            if source.is_file() and chip_name not in KNOWN_PIN_INIT_EXCEPTIONS:
                c_text = source.read_text(encoding="utf-8", errors="replace")
                initialized = set(re.findall(r'pin_init\("([^"]+)"', c_text))
                for pin in pins:
                    if pin and pin not in initialized:
                        problems.append(f"{source.name} does not pin_init({pin!r})")

    pin_map = dict(BUILTIN)
    pin_map.update(custom_pins)
    for wire in doc.get("connections", []):
        if not isinstance(wire, list) or len(wire) < 2:
            problems.append(f"malformed connection: {wire!r}")
            continue
        for endpoint in wire[:2]:
            if not isinstance(endpoint, str) or ":" not in endpoint:
                problems.append(f"malformed endpoint {endpoint!r}")
                continue
            part_id, pin = endpoint.split(":", 1)
            if part_id.startswith("$"):
                continue
            if part_id not in ids:
                problems.append(f"connection references missing part {endpoint}")
                continue
            part_type = ids[part_id]
            if part_type in pin_map and pin not in pin_map[part_type]:
                problems.append(f"part {part_id} ({part_type}) has no pin {pin!r}")

    count_custom = len({kind for kind in ids.values() if kind.startswith("chip-")})
    print(f"   {len(ids)} parts / {len(doc.get('connections', []))} connections / {count_custom} custom-chip types")
    if problems:
        for item in problems:
            print("   [PROBLEM] " + item)
        return 1
    print("   OK - diagram, chip files, JSON pinouts, WASM headers and wokwi.toml agree")
    return 0


if __name__ == "__main__":
    targets = sys.argv[1:] or ["."]
    rc = 0
    ran = 0
    for target in targets:
        if os.path.isdir(target):
            rc |= check(target)
            ran += 1
    if not ran:
        print("no project folder found - pass one, e.g. python3 scripts/check-wokwi-diagram.py .")
        rc = 2
    sys.exit(rc)
