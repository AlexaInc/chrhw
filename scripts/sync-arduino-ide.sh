#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# chrclient/chrhw — make every Arduino IDE sketch folder SELF-CONTAINED.
#
# Why: the Arduino IDE can only compile files that live INSIDE the sketch
# folder. The arduino-ide/<board>/ sketches used to include ../../src/main.cpp,
# ../../include/config.h and ../../include/logo_bitmap.h — paths that leave the
# folder — so opening them in the Arduino IDE failed with
# "No such file or directory" and the board could not be flashed from the IDE.
#
# What this script does: it COPIES the firmware source and every header that
# sketch needs INTO the sketch folder, and rewrites the sketch tab so nothing
# points outside any more:
#
#   arduino-ide/<board>/
#       <board>.ino          generated   board settings + how this folder works
#       main.cpp             generated   copy of src/main.cpp (the real firmware)
#       config.h             generated   config.machine.h + the shared config
#       config.machine.h     YOURS       Wi-Fi, pins, version of THIS board
#       arc_math.h           generated   copy (rover only)
#       logo_bitmap.h        generated   copy (rover only)
#
# The generated files carry a "GENERATED - DO NOT EDIT" banner. Editing them
# directly would drift from src/main.cpp, so:
#
#     edit  src/main.cpp            (the firmware, one place)
#     edit  arduino-ide/*/config.machine.h   (only this board's machine values)
#     run   bash scripts/sync-arduino-ide.sh
#
# Then open the sketch folder in the Arduino IDE and press Upload. Nothing in
# the folder reaches out to another folder any more.
#
# Usage:  bash scripts/sync-arduino-ide.sh [--check]
#         --check   do not write anything, only report what is out of date
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

CHECK_ONLY=0
[ "${1:-}" = "--check" ] && CHECK_ONLY=1

ROVER="wokwi-esp32-project"
PUMP="wokwi-water-pump-c3"

ok()   { printf '  \033[1;32mok\033[0m     %s\n' "$*"; }
wrote(){ printf '  \033[1;36mwrote\033[0m  %s\n' "$*"; }
stale(){ printf '  \033[1;33mstale\033[0m  %s\n' "$*"; }
bad()  { printf '  \033[1;31mFAIL\033[0m   %s\n' "$*" >&2; }

GENERATED_NOTE="// ---------------------------------------------------------------------------
// GENERATED FILE - DO NOT EDIT THIS COPY.
//
// Arduino IDE can only compile files that are INSIDE the sketch folder, so the
// headers are copied here instead of being included from ../../ (which the IDE
// cannot follow).
//
//   firmware source : %s
//   after editing it: bash scripts/sync-arduino-ide.sh
//
// This copy is compared, byte for byte, against that file by
// scripts/check-code-copies.sh - it can never drift silently.
// ---------------------------------------------------------------------------"

# Copy a file into the sketch folder unless it is already identical.
# $1 = source (outside the folder)  $2 = destination (inside the folder)  $3 = "cpp"|"plain"
copy_in() {
    local src="$1" dst="$2" kind="${3:-plain}"
    [ -f "$src" ] || { bad "missing source: $src"; return 1; }
    mkdir -p "$(dirname "$dst")"
    local tmp; tmp="$(mktemp)"
    if [ "$kind" = "cpp" ]; then
        printf '%s\n' "$(printf "$GENERATED_NOTE" "$src")" > "$tmp"
        cat "$src" >> "$tmp"
    else
        cp "$src" "$tmp"
    fi
    if [ -f "$dst" ] && cmp -s "$tmp" "$dst"; then
        ok "$dst (up to date)"
        rm -f "$tmp"
    else
        if [ "$CHECK_ONLY" = "1" ]; then
            stale "$dst differs from $src"
            rm -f "$tmp"
            return 2
        fi
        mv "$tmp" "$dst"
        wrote "$dst   <- $src"
    fi
}

# Build <sketch>/config.h = the board's own machine values + the shared config.
# $1 = sketch dir  $2 = shared config (include/config.h)
write_config_h() {
    local dir="$1" shared="$2" tmp
    local out="$dir/config.h"
    [ -f "$shared" ] || { bad "missing $shared"; return 1; }
    [ -f "$dir/config.machine.h" ] || { bad "$dir/config.machine.h is missing (it holds this board's Wi-Fi/pins)"; return 1; }
    tmp="$(mktemp)"
    {
        printf '// ---------------------------------------------------------------------------\n'
        printf '// GENERATED FILE - DO NOT EDIT THIS COPY.\n'
        printf '//\n'
        printf '// This is what the Arduino IDE compiles: the machine values of THIS board\n'
        printf '// (config.machine.h, which you own and edit) followed by a copy of the shared\n'
        printf '// configuration %s.\n' "$shared"
        printf '//\n'
        printf '//   board values : config.machine.h          (edit this one)\n'
        printf '//   shared config: %s   (edit that one)\n' "$shared"
        printf '//   then run     : bash scripts/sync-arduino-ide.sh\n'
        printf '//\n'
        printf '// Everything in the shared part is #ifndef-guarded, so the board values above\n'
        printf '// always win. A config.local.h next to this file (optional, git-ignored) is\n'
        printf '// read after the board values and before the shared defaults, so it beats both.\n'
        printf '// ---------------------------------------------------------------------------\n'
        printf '#pragma once\n\n'
        cat "$dir/config.machine.h"
        printf '\n'
        # The optional git-ignored config.local.h is read HERE - after the board
        # values (plain #define lines, so a local value must come later to win) and
        # BEFORE the shared defaults (all #ifndef-guarded, so they can never clobber
        # a value that is already defined). Priority stays local > board > shared,
        # exactly as the hand-written sketch configs behaved.
        #
        # The block is spelled out in full on purpose: cutting it out of the shared
        # file with "print until the first #endif" stops inside it (the block nests
        # two levels) and left an #ifdef without its #endif - the Arduino IDE then
        # fails with "error: unterminated #ifdef". The shared copy's own block is
        # neutralised further down instead.
        printf '#ifdef __has_include\n'
        printf '#if __has_include("config.local.h")\n'
        printf '#include "config.local.h"\n'
        printf '#endif\n'
        printf '#endif\n'
        printf '\n// ===========================================================================\n'
        printf '// --- copy of %s (generated, do not edit) -------------------------\n' "$shared"
        printf '// ===========================================================================\n'
        # In the copied body: drop the shared file's own "#pragma once" (a no-op here)
        # and neutralise its config.local.h block - that include already happened at
        # the top of this generated file, and doing it twice is pointless.
        awk '
            /^[[:space:]]*#[[:space:]]*ifdef[[:space:]]+__has_include/ {
                print "// (config.local.h is already included at the top of this generated file)"
                skip=1; d=1; next
            }
            skip {
                if ($0 ~ /^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef)([[:space:]]|$)/) d++
                if ($0 ~ /^[[:space:]]*#[[:space:]]*endif([[:space:]]|$)/) { d--; if (d==0) skip=0 }
                next
            }
            /^[[:space:]]*#pragma once([[:space:]]|$)/ { next }
            { print }
        ' "$shared"
    } > "$tmp"
    if [ -f "$out" ] && cmp -s "$tmp" "$out"; then
        ok "$out (up to date)"
        rm -f "$tmp"
    else
        if [ "$CHECK_ONLY" = "1" ]; then
            stale "$out differs from $shared + config.machine.h"
            rm -f "$tmp"
            return 2
        fi
        mv "$tmp" "$out"
        wrote "$out   <- config.machine.h + $shared"
    fi
}

# The sketch tab itself: documentation only, no include that leaves the folder.
write_ino() { # $1 = sketch dir  $2 = board heading  $3 = board settings block  $4 = shared config path
    local dir="$1" heading="$2" settings="$3" shared="$4" tmp name
    name="$(basename "$dir")"
    tmp="$(mktemp)"
    {
        printf '// ---------------------------------------------------------------------------\n'
        printf '// %s\n' "$heading"
        printf '//\n'
        printf '// SELF-CONTAINED SKETCH FOLDER (Arduino IDE).\n'
        printf '// Everything this sketch needs is inside this folder - the Arduino IDE only\n'
        printf '// compiles files that live in the sketch folder, so the firmware source is a\n'
        printf '// copy of the real one instead of an include that would leave the folder:\n'
        printf '//\n'
        printf '//   main.cpp            the firmware (a copy - the IDE compiles it for you)\n'
        printf '//   config.h            GENERATED: config.machine.h + a copy of %s\n' "$shared"
        printf '//   config.machine.h    Wi-Fi / pins / version of THIS board  <- edit this one\n'
        printf '//\n'
        printf '// Editing the firmware? Change src/main.cpp in the repository root and run\n'
        printf '//     bash scripts/sync-arduino-ide.sh\n'
        printf '// so this folder is refreshed (check-code-copies.sh compares them for you).\n'
        printf '//\n'
        printf '%s\n' "$settings"
        printf '// ---------------------------------------------------------------------------\n'
        printf '// No code lives in this tab on purpose: Arduino IDE compiles main.cpp next to\n'
        printf '// it, and the firmware defines setup()/loop() there. Keeping this tab empty of\n'
        printf '// code is what stops a second copy of the firmware from ever appearing here.\n'
        printf '// ---------------------------------------------------------------------------\n'
    } > "$tmp"
    if [ -f "$dir/$name.ino" ] && cmp -s "$tmp" "$dir/$name.ino"; then
        ok "$dir/$name.ino (up to date)"
        rm -f "$tmp"
    else
        if [ "$CHECK_ONLY" = "1" ]; then
            stale "$dir/$name.ino is out of date"
            rm -f "$tmp"
            return 2
        fi
        mv "$tmp" "$dir/$name.ino"
        wrote "$dir/$name.ino"
    fi
}

# A short README inside each generated folder, so opening the folder in the
# Arduino IDE (or in a file browser) explains itself. Not compiled by the IDE.
write_readme() { # $1 = sketch dir  $2 = board line  $3 = shared config path  $4 = canonical main.cpp
    local dir="$1" board="$2" shared="$3" mainsrc="$4" name tmp
    name="$(basename "$dir")"
    tmp="$(mktemp)"
    {
        printf '# %s - Arduino IDE sketch folder\n\n' "$name"
        printf '%s\n\n' "$board"
        printf '**This folder is self-contained on purpose.** The Arduino IDE can only compile files\n'
        printf 'that live inside the sketch folder, so the firmware and its headers are copied in\n'
        printf 'here instead of being included from `../../` (which the IDE cannot follow):\n\n'
        printf '| file | what it is |\n|---|---|\n'
        printf '| `%s.ino` | sketch tab - board settings and notes, no code |\n' "$name"
        printf '| `main.cpp` | a byte-identical copy of `%s` (the IDE compiles it) |\n' "$mainsrc"
        printf '| `config.h` | GENERATED: `config.machine.h` + a copy of `%s` |\n' "$shared"
        printf '| `config.machine.h` | **yours**: Wi-Fi, pins, firmware version of this board |\n'
        printf '| `config.local.h` | optional, git-ignored, beats everything (not required) |\n\n'
        printf 'To use it: open this folder in the Arduino IDE, pick the board from the sketch\n'
        printf 'header, install the libraries listed there, plug the board in and press Upload.\n\n'
        printf 'After a firmware change in the repository:\n\n'
        printf '```bash\nbash scripts/sync-arduino-ide.sh      # refresh this folder\nbash scripts/check-code-copies.sh     # prove nothing drifted\n```\n'
    } > "$tmp"
    if [ -f "$dir/README.md" ] && cmp -s "$tmp" "$dir/README.md"; then ok "$dir/README.md (up to date)"; rm -f "$tmp"; else
        if [ "$CHECK_ONLY" = "1" ]; then stale "$dir/README.md is out of date"; rm -f "$tmp"; else mv "$tmp" "$dir/README.md"; wrote "$dir/README.md"; fi
    fi
}

echo "== rover: $ROVER/arduino-ide/esp32-dev-controller ========================"
ROVER_SKETCH="$ROVER/arduino-ide/esp32-dev-controller"
copy_in "$ROVER/src/main.cpp"        "$ROVER_SKETCH/main.cpp"
copy_in "$ROVER/include/arc_math.h"  "$ROVER_SKETCH/arc_math.h"
copy_in "$ROVER/include/logo_bitmap.h" "$ROVER_SKETCH/logo_bitmap.h"
write_config_h "$ROVER_SKETCH" "$ROVER/include/config.h"
write_readme "$ROVER_SKETCH" "CHR-01 rover - ESP32 DevKit V1." "include/config.h" "src/main.cpp"
write_ino "$ROVER_SKETCH" "CHR-01 rover - Arduino IDE sketch for the ESP32 DevKit V1." \
"// Board settings (Arduino IDE):
//   Board            : \"ESP32 Dev Module\"   (DevKit V1)
//   Partition Scheme : \"Default 4MB with spiffs\" (or any with >= 1.3MB app)
//   Upload Speed     : 921600      Monitor Speed : 921600
//
// Libraries (Library Manager): WebSockets by Markus Sattler, ArduinoJson 6.x,
// Adafruit GFX, Adafruit SSD1306, DHT sensor library, Adafruit Unified Sensor,
// TinyGPSPlus, ESP32Servo." "$ROVER/include/config.h"

echo
echo "== pump: $PUMP/arduino-ide/* ============================================"
for variant in esp32-c3-water-pump esp32-devkit-v1-water-pump; do
    DIR="$PUMP/arduino-ide/$variant"
    [ -d "$DIR" ] || { bad "$DIR does not exist"; continue; }
    if [ "$variant" = "esp32-c3-water-pump" ]; then
        SETTINGS='// Board settings (Arduino IDE):
//   Board            : "ESP32C3 Dev Module"   (ESP32-C3 Super Mini)
//   USB CDC On Boot  : Enabled (Serial over USB)
//   Partition Scheme : "Default 4MB with spiffs"
//   Upload Speed     : 921600'
    else
        SETTINGS='// Board settings (Arduino IDE):
//   Board            : "ESP32 Dev Module"    (ESP32 DevKit V1)
//   Partition Scheme : "Default 4MB with spiffs"
//   Upload Speed     : 921600'
    fi
    copy_in "$PUMP/src/main.cpp" "$DIR/main.cpp"
    write_config_h "$DIR" "$PUMP/include/config.h"
    write_ino "$DIR" "CHR water-pump controller - Arduino IDE sketch." "$SETTINGS" \
        "$PUMP/include/config.h"
    write_readme "$DIR" "CHR water-pump controller - $variant." "include/config.h" "src/main.cpp"
    echo
done

echo "== done ================================================================"
if [ "$CHECK_ONLY" = "1" ]; then
    echo "run without --check to refresh the folders."
fi
