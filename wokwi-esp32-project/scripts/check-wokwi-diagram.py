#!/usr/bin/env python3
"""Check a Wokwi project folder before starting the simulator.

Usage:  python3 scripts/check-wokwi-diagram.py [project-folder ...]
        (default: this project and the water pump project)

Checks
  1. every `chip-xxx` part in diagram.json has xxx.chip.json and xxx.chip.wasm
  2. every chip used by the diagram has a [[chip]] block in wokwi.toml
  3. every [[chip]] block in wokwi.toml has its files
  4. every connection endpoint names a part that exists and a pin that exists
     (pin tables for the built-in parts; custom chips from their .chip.json)
  5. duplicate part ids
"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))          # .../wokwi-esp32-project/scripts
ROVER = os.path.dirname(HERE)                               # .../wokwi-esp32-project
REPO = os.path.dirname(ROVER)                               # .../chrhw
DEFAULT = [ROVER, os.path.join(REPO, "wokwi-water-pump-c3")]

BUILTIN = {
    "wokwi-resistor": ["1", "2"],
    "wokwi-text": [],
    "wokwi-junction": ["J"],
    "wokwi-esp32-devkit-v1": ["VIN", "3V3", "GND.1", "GND.2", "GND.3", "EN", "VP", "VN",
                              "D2", "D4", "D5", "D12", "D13", "D14", "D15", "D18", "D19",
                              "D21", "D22", "D23", "D25", "D26", "D27", "D32", "D33", "D34",
                              "D35", "RX0", "TX0", "RX2", "TX2"],
    "board-esp32-c3-devkitm-1": ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10",
                                 "TX", "RX", "3V3", "5V", "GND", "3V3.1", "5V.1", "GND.3"],
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
# chips whose .chip.c does not call pin_init() for some declared pins even though
# they simulate fine (pre-existing, do not "fix" without testing the sim)
KNOWN = {"gps", "espcam"}


def check(proj):
    print("== " + proj)
    problems = []
    dpath = os.path.join(proj, "diagram.json")
    if not os.path.exists(dpath):
        print("   no diagram.json - skipped")
        return 1
    doc = json.load(open(dpath, encoding="utf-8"))
    toml = open(os.path.join(proj, "wokwi.toml"), encoding="utf-8").read()
    toml_names = re.findall(r'name\s*=\s*"([^"]+)"', toml)

    ids = {}
    for p in doc["parts"]:
        if p["id"] in ids:
            problems.append("duplicate part id %s" % p["id"])
        ids[p["id"]] = p["type"]

    pins = dict(BUILTIN)
    for pid, t in ids.items():
        if not t.startswith("chip-"):
            continue
        n = t[5:]
        for ext in (".chip.json", ".chip.wasm"):
            if not os.path.exists(os.path.join(proj, n + ext)):
                problems.append("missing %s%s (part %s)" % (n, ext, pid))
        j = os.path.join(proj, n + ".chip.json")
        if os.path.exists(j):
            pins[t] = json.load(open(j, encoding="utf-8")).get("pins", [])
        if n not in toml_names:
            problems.append("wokwi.toml has no [[chip]] for %s" % n)

    for n in toml_names:
        for ext in (".chip.json", ".chip.wasm"):
            if not os.path.exists(os.path.join(proj, n + ext)):
                problems.append("wokwi.toml chip %s has no %s" % (n, ext))
        c = os.path.join(proj, n + ".chip.c")
        j = os.path.join(proj, n + ".chip.json")
        if os.path.exists(c) and os.path.exists(j) and n not in KNOWN:
            inits = set(re.findall(r'pin_init\("([^"]+)"', open(c, encoding="utf-8").read()))
            for pin in json.load(open(j, encoding="utf-8")).get("pins", []):
                if pin and pin not in inits:
                    problems.append("%s.chip.c does not pin_init(%r)" % (n, pin))

    for c in doc["connections"]:
        for ep in c[:2]:
            pid, _, pin = ep.partition(":")
            if pid.startswith("$"):
                continue
            if pid not in ids:
                problems.append("connection references missing part %s" % ep)
                continue
            t = ids[pid]
            if t in pins and pin not in pins[t]:
                problems.append("part %s (%s) has no pin %r" % (pid, t, pin))

    print("   %d parts / %d connections / %d chip types" % (len(doc["parts"]), len(doc["connections"]),
                                                             len({t for t in ids.values() if t.startswith("chip-")})))
    if problems:
        for p in problems:
            print("   [PROBLEM] " + p)
        return 1
    print("   OK - diagram, chip files and wokwi.toml agree")
    return 0


if __name__ == "__main__":
    targets = sys.argv[1:] or DEFAULT
    rc = 0
    ran = 0
    for t in targets:
        if os.path.isdir(t):
            rc |= check(t)
            ran += 1
    if not ran:
        print("no project folder found - pass one, e.g. python3 check-wokwi-diagram.py ../wokwi-esp32-project")
        rc = 2
    sys.exit(rc)
