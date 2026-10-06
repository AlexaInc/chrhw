#!/usr/bin/env python3
"""
Adds the GPS *placeholder position* to the rover firmware.

The GPS module is not always fitted/working, and without it the rover used to
report nothing at all (0,0), so the map, the mission planner and the app went
blank. With this change the rover keeps reporting a position: while there is no
valid NMEA fix it sends the field's placeholder coordinates from config.h and
marks the message with `fix:false` / `placeholder:true`, so the app can say
"NO GPS FIX" instead of showing an empty map.

It edits EVERY copy of the firmware it is pointed at:
  * full copies (pre-shim layout): wokwi-esp32-project/sketch.ino,
    wokwi-esp32-project/src/main.cpp, ...
  * shims (post-refactor layout): a copy that only `#include`s the firmware
    source, which is edited once - a shim is skipped on purpose, editing it
    would create the drifted second copy this repo is trying to get rid of.
Same for the config.h copies.

Run from the repository root:  python3 scripts/apply-gps-placeholder.py
(applied by the Task-7 patch; running it twice is harmless - it is idempotent)
"""
import os
import re
import sys
import hashlib

MARK = "GPS_FALLBACK_ENABLED"

CONFIG_HEADER = """// --- GPS placeholder position ----------------------------------------------
// The GPS module can be missing, unpowered or without a sky view, and the
// operator still needs a position on the map. While there is no valid fix the
// rover reports the placeholder position below and marks the message
// `fix:false`, so the app shows "NO GPS FIX" instead of an empty map.
// Change these two numbers to your own field centre, or set
// GPS_FALLBACK_ENABLED to 0 to send nothing at all while there is no fix."""

CONFIG_VALUES = (("GPS_FALLBACK_ENABLED", "1"),
                 ("GPS_FALLBACK_LATITUDE", "7.489087449264883"),
                 ("GPS_FALLBACK_LONGITUDE", "80.36537714662697"))


def config_block(text):
    """Match the style of the config.h copy we are editing: the plain copy uses
    bare #defines, the refactored one guards every setting with #ifndef."""
    guarded = "#ifndef WIFI_SSID" in text or "#ifndef DEVICE_ID" in text
    lines = [CONFIG_HEADER]
    for name, value in CONFIG_VALUES:
        if guarded:
            lines += ["#ifndef %s" % name, "#define %s %s" % (name, value), "#endif"]
        else:
            lines.append("#define %s %s" % (name, value))
    return "\n".join(lines) + "\n"


def config_anchor(text):
    for anchor in ("// WiFi & Server Credentials", "// --- Wi-Fi & server",
                   "#ifndef WIFI_SSID", "#define WIFI_SSID"):
        if anchor in text:
            return anchor
    return None

HELPERS = """
// ---------------------------------------------------------------------------
// GPS placeholder (see config.h) -------------------------------------------
// `gpsHasFix()` is the only question the firmware asks about the module: the
// helpers below answer with the last real fix, or with the placeholder position
// from config.h while the GPS is silent. Navigation never uses the
// placeholder - the rover refuses to drive on a position it did not measure.
// ---------------------------------------------------------------------------
bool gpsHasFix() {
    return gps.location.isValid() && currentLatitude != 0 && currentLongitude != 0;
}

double reportLatitude() {
#if GPS_FALLBACK_ENABLED
    return gpsHasFix() ? currentLatitude : (double)GPS_FALLBACK_LATITUDE;
#else
    return currentLatitude;
#endif
}

double reportLongitude() {
#if GPS_FALLBACK_ENABLED
    return gpsHasFix() ? currentLongitude : (double)GPS_FALLBACK_LONGITUDE;
#else
    return currentLongitude;
#endif
}
"""

OLD_SEND = """void sendLocationData() {
    if (!socketConnected || currentLatitude == 0 || currentLongitude == 0) return;
    DynamicJsonDocument doc(512);
    JsonArray event = doc.to<JsonArray>();
    event.add("message.upsert");
    JsonObject envelope = event.createNestedObject();
    envelope["Type"] = "location";
    JsonObject message = envelope.createNestedObject("Message");
    message["latitude"] = currentLatitude;
    message["longitude"] = currentLongitude;
    message["altitude"] = gps.altitude.isValid() ? gps.altitude.meters() : 0;
    message["satellites"] = gps.satellites.isValid() ? gps.satellites.value() : 0;
    message["deviceId"] = DEVICE_ID;"""

NEW_SEND = """void sendLocationData() {
    if (!socketConnected) return;
    const bool fix = gpsHasFix();
#if !GPS_FALLBACK_ENABLED
    if (!fix) return;   // no fix and the placeholder is switched off: stay silent
#endif
    DynamicJsonDocument doc(768);
    JsonArray event = doc.to<JsonArray>();
    event.add("message.upsert");
    JsonObject envelope = event.createNestedObject();
    envelope["Type"] = "location";
    JsonObject message = envelope.createNestedObject("Message");
    // fix:false = these are the placeholder coordinates from config.h, the GPS
    // module is not answering. The app tells the operator instead of guessing.
    message["latitude"] = reportLatitude();
    message["longitude"] = reportLongitude();
    message["altitude"] = fix && gps.altitude.isValid() ? gps.altitude.meters() : 0;
    message["satellites"] = fix && gps.satellites.isValid() ? gps.satellites.value() : 0;
    message["fix"] = fix;
    message["placeholder"] = !fix;
    message["deviceId"] = DEVICE_ID;"""


def edit_source(text):
    """The same three edits in every full copy of the rover firmware."""
    changed = 0

    if "bool gpsHasFix()" not in text:
        anchor = "unsigned long lastLocationMillis = 0;\n"
        if anchor not in text:
            raise SystemExit("GPS edit: could not find the location globals")
        text = text.replace(anchor, anchor + HELPERS, 1)
        changed += 1

    old_status = "    bool gpsFix = gps.location.isValid() && currentLatitude != 0 && currentLongitude != 0;"
    new_status = ("    // gpsHasFix() also answers false for a 0,0 fix - and the row below says\n"
                  "    // PLACEHOLDER, so the operator can see why the position is not from the sky.\n"
                  "    bool gpsFix = gpsHasFix();")
    if old_status in text:
        text = text.replace(old_status, new_status, 1)
        changed += 1

    old_row = '    else display.print("GPS NO FIX ");'
    new_row = ('#if GPS_FALLBACK_ENABLED\n'
               '    else display.print("GPS NOFIX PLH ");\n'
               '#else\n'
               '    else display.print("GPS NO FIX ");\n'
               '#endif')
    if old_row in text and "GPS NOFIX PLH" not in text:
        text = text.replace(old_row, new_row, 1)
        changed += 1

    if OLD_SEND in text:
        text = text.replace(OLD_SEND, NEW_SEND, 1)
        changed += 1

    old_nav = ("    if (currentLatitude == 0 || currentLongitude == 0 || !gps.location.isValid()) {\n"
               "        controlMotors(\"STOP\");")
    new_nav = ("    // Never drive on the placeholder position: no measured fix, no motion.\n"
               "    if (!gpsHasFix()) {\n"
               "        controlMotors(\"STOP\");")
    if old_nav in text:
        text = text.replace(old_nav, new_nav, 1)
        changed += 1

    if 'addField("latitude", String(currentLatitude, 7));' in text:
        text = text.replace('addField("latitude", String(currentLatitude, 7));',
                            'addField("latitude", String(reportLatitude(), 7));', 1)
        changed += 1
    if 'addField("longitude", String(currentLongitude, 7));' in text:
        text = text.replace('addField("longitude", String(currentLongitude, 7));',
                            'addField("longitude", String(reportLongitude(), 7));', 1)
        changed += 1

    return text, changed


def is_shim(text):
    """A copy that only includes the real firmware source is not a second copy."""
    if "TinyGPSPlus gps;" not in text or "void loop()" not in text:
        return True
    return False


def is_config_shim(text):
    return len(text.splitlines()) < 20


def main():
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    edited, skipped = [], []

    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in (".git", ".pio", "node_modules")]
        if "arduino-ide" in dirpath and "esp32-cam-uart" in dirpath:
            continue
        for name in filenames:
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, root)
            if name.endswith((".ino", ".cpp")):
                text = open(path, encoding="utf-8", errors="surrogateescape").read()
                if "TinyGPSPlus gps;" not in text:
                    continue  # the pump has no GPS
                if is_shim(text):
                    skipped.append(rel)
                    continue
                new_text, changed = edit_source(text)
                if new_text != text:
                    open(path, "w", encoding="utf-8", errors="surrogateescape").write(new_text)
                edited.append((rel, changed))
            elif name == "config.h":
                text = open(path, encoding="utf-8", errors="surrogateescape").read()
                if "SOIL_ANALOG_PIN" not in text:
                    continue  # not the rover config
                if is_config_shim(text):
                    skipped.append(rel)
                    continue
                if MARK in text:
                    edited.append((rel, 0))
                    continue
                anchor = config_anchor(text)
                if anchor is None:
                    raise SystemExit("config edit: %s has no Wi-Fi section to sit above" % rel)
                text = text.replace(anchor, config_block(text) + "\n" + anchor, 1)
                open(path, "w", encoding="utf-8", errors="surrogateescape").write(text)
                edited.append((rel, 1))

    print("edited %d file(s):" % len(edited))
    for rel, changed in sorted(edited):
        print("   %s%s" % (rel, "" if changed else "   (already had the placeholder)"))
    if skipped:
        print("skipped %d include-shim(s) (edited through their source):" % len(skipped))
        for rel in sorted(skipped):
            print("   %s" % rel)

    # Every copy that carries the firmware must now be byte-identical: the repo
    # has been bitten by drifted copies before, so fail loudly if they are not.
    groups = {}

    def key(path):
        rel = os.path.relpath(path, root)
        return "rover" if rel.startswith("wokwi-esp32-project") else "pump"

    for dirpath, dirnames, filenames in os.walk(os.path.join(root, "wokwi-esp32-project")):
        dirnames[:] = [d for d in dirnames if d not in (".git", ".pio")]
        for name in filenames:
            if not name.endswith((".ino", ".cpp")):
                continue
            path = os.path.join(dirpath, name)
            text = open(path, encoding="utf-8", errors="surrogateescape").read()
            if "TinyGPSPlus gps;" not in text or is_shim(text):
                continue
            digest = hashlib.sha256(text.encode("utf-8", "surrogateescape")).hexdigest()
            groups.setdefault(digest, []).append(os.path.relpath(path, root))

    if len(groups) > 1:
        print("the rover firmware copies differ from each other:")
        for digest, files in groups.items():
            print("   %s: %s" % (digest[:12], ", ".join(sorted(files))))
        return 1
    for digest, files in groups.items():
        print("all %d rover firmware copy/copies identical (%s): %s"
              % (len(files), digest[:12], ", ".join(sorted(files))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
