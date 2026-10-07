#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# CHR hardware repo - "are all the code copies still pointing at ONE source?"
#
# The rover and the pump used to exist as several full copies of the same
# firmware (root sketch.ino, src/main.cpp, arduino-ide/<board>/*.ino), which
# drifted apart. There are two rules now, and this script checks both:
#
#   * Wokwi / PlatformIO sketches (sketch.ino) are SHIMS that include
#     "src/main.cpp" - one firmware source, nothing copied.
#   * Arduino IDE sketch folders (arduino-ide/<board>/) must be SELF-CONTAINED:
#     the IDE can only compile files inside the folder, so main.cpp (a copy of
#     src/main.cpp), the headers and a generated config.h live in there.
#     Those copies are byte-compared against their sources, so they cannot
#     drift - run scripts/sync-arduino-ide.sh to refresh them.
#
# Run this after ANY firmware edit (and before flashing a real board):
#     ./scripts/check-code-copies.sh
#
# It needs no PlatformIO - it only reads the files.
# ---------------------------------------------------------------------------
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1

FAIL=0
ok()   { printf '  ok    %s\n' "$*"; }
bad()  { printf '  FAIL  %s\n' "$*"; FAIL=1; }
warn() { printf '  WARN  %s\n' "$*"; }
inf()  { printf '  --    %s\n' "$*"; }
skip() { printf '  skip  %s\n' "$*"; }

echo "== 1. sketch entry points ==============================================="
# Every .ino either includes the shared firmware source, or is a genuinely
# separate device (then it must carry its own setup()/loop()).
entries=0
while IFS= read -r ino; do
    entries=$((entries + 1))
    inc="$(grep -m1 -E '^[[:space:]]*#include[[:space:]]+"[^"]*main\.cpp"' "$ino" | sed -E 's/.*"([^"]*)".*/\1/')"
    if [ -n "$inc" ]; then
        target="$(python3 -c 'import os,sys; print(os.path.normpath(os.path.join(os.path.dirname(sys.argv[1]), sys.argv[2])))' "$ino" "$inc")"
        if [ ! -f "$target" ]; then
            bad "$ino includes $inc -> $target, which does not exist"
            continue
        fi
        if ! grep -qE 'void[[:space:]]+(setup|loop)[[:space:]]*\(' "$target"; then
            bad "$ino -> $inc does not look like the firmware source"
            continue
        fi
        if grep -qE 'void[[:space:]]+(setup|loop)[[:space:]]*\(' "$ino"; then
            bad "$ino carries firmware code of its own - a shim must only include"
            continue
        fi
        ok "$ino -> $inc"
    elif grep -qE 'void[[:space:]]+(setup|loop)[[:space:]]*\(' "$ino"; then
        skip "$ino is a standalone sketch (separate device)"
    elif [ -f "$(dirname "$ino")/main.cpp" ]; then
        # The self-contained Arduino IDE layout: the folder holds main.cpp (the
        # firmware copy) and the tab is documentation only. That is correct -
        # and it is exactly what keeps a second firmware copy out of the tab.
        ok "$ino (self-contained folder: main.cpp next to it)"
    else
        bad "$ino includes no firmware source, defines no setup()/loop(), and has no main.cpp in its folder"
    fi
done < <(find wokwi-esp32-project wokwi-water-pump-c3 -name '*.ino' -not -path '*/.pio/*' | sort)
[ "$entries" -gt 0 ] || bad "no .ino sketches found at all"

echo
echo "== 1b. Arduino IDE folders are self-contained =========================="
# No file in an arduino-ide sketch folder may include anything outside it, the
# firmware copy must equal src/main.cpp byte for byte, and every header the
# firmware includes from the folder must exist there.
while IFS= read -r sketch; do
    dir="$(dirname "$sketch")"
    # (a) nothing may reach outside the folder (commented-out lines do not count:
    # the generated files explain the old layout in their comments)
    outside="$(grep -rnE '^[[:space:]]*#include[[:space:]]*"\.\./' "$dir" 2>/dev/null || true)"
    if [ -n "$outside" ]; then
        bad "$dir still includes a path outside the folder:"
        printf '%s\n' "$outside" | sed 's/^/          /'
    else
        ok "$(basename "$dir"): no include leaves the sketch folder"
    fi
    # (b) every file the firmware includes from the folder must BE in the folder
    missing=""
    while IFS= read -r inc; do
        [ -f "$dir/$inc" ] || missing="$missing $inc"
    done < <(grep -rhE '^[[:space:]]*#include[[:space:]]*"' "$dir" 2>/dev/null \
                | sed -E 's/.*"([^"]*)".*/\1/' | sort -u \
                | grep -v '^config\.local\.h$' || true)
    if [ -z "$missing" ]; then
        ok "$(basename "$dir"): the local includes the code needs are all present"
    else
        # not a failure by itself: quoted library headers (DHT.h, ...) are not files,
        # but a firmware header with no copy in the folder IS the IDE's problem
        real_missing=""
        for m in $missing; do
            case "$m" in
                DHT.h|TinyGPSPlus.h|esp_camera.h|img_converters.h|config.local.h) ;;
                *) real_missing="$real_missing $m" ;;
            esac
        done
        if [ -z "$real_missing" ]; then
            ok "$(basename "$dir"): only library headers are not local (fine)"
        else
            bad "$(basename "$dir"): includes $real_missing, which is not in the folder"
        fi
    fi
    # The generated Arduino IDE board folders are the ones with config.machine.h.
    # A hand-written standalone test sketch has neither main.cpp nor config.h.
    if [ ! -f "$dir/config.machine.h" ]; then
        inf "$(basename "$dir") is a standalone test sketch (no generated layout to check)"
        continue
    fi
    # (c) the firmware copy is identical to the one PlatformIO compiles
    src="$(dirname "$(dirname "$dir")")/src/main.cpp"
    if [ -f "$dir/main.cpp" ] && [ -f "$src" ]; then
        if cmp -s "$dir/main.cpp" "$src"; then
            ok "$(basename "$dir")/main.cpp == $src (byte for byte)"
        else
            bad "$(basename "$dir")/main.cpp differs from $src - run scripts/sync-arduino-ide.sh"
        fi
    else
        bad "$(basename "$dir")/main.cpp or $src is missing"
    fi
    # (d) exactly one setup()/loop() definition in the compiled set of the folder
    defs="$(grep -lE 'void[[:space:]]+(setup|loop)[[:space:]]*\(' "$dir"/*.cpp "$dir"/*.ino 2>/dev/null | wc -l | tr -d ' ')"
    if [ "$defs" = "1" ]; then
        ok "$(basename "$dir"): setup()/loop() defined in exactly one file (no duplicate symbols)"
    else
        bad "$(basename "$dir"): setup()/loop() found in $defs files - exactly 1 is required"
    fi
    # (e) config.h must BE the config (machine + shared), not a pointer outside
    if grep -q 'ECHO_FORWARD_PIN\|DEVICE_ROLE\|MAX_SPEED\|RELAY_ACTIVE_LOW' "$dir/config.h" 2>/dev/null; then
        ok "$(basename "$dir")/config.h carries the real configuration"
    else
        bad "$(basename "$dir")/config.h does not contain the shared configuration - run scripts/sync-arduino-ide.sh"
    fi
done < <(find wokwi-esp32-project/arduino-ide wokwi-water-pump-c3/arduino-ide -name '*.ino' -not -path '*/.pio/*' | sort)

echo
echo "== 1c. preprocessor conditionals are balanced =========================="
# A generated config.h is built by cutting blocks out of include/config.h, so a
# mistake there can leave an "#if/#ifdef" without its "#endif" - the Arduino IDE
# then fails with "error: unterminated #ifdef". Count them, nesting included.
pp_balance() { # prints a reason on failure, nothing on success
    awk '
        /^[[:space:]]*#[[:space:]]*(if|ifdef|ifndef)([[:space:]]|$)/ { d++; next }
        /^[[:space:]]*#[[:space:]]*endif([[:space:]]|$)/ { d--; if (d < 0) { print "an #endif without an #if"; exit } }
        END { if (d > 0) printf "%d unclosed #if/#ifdef\n", d }
    ' "$1"
}
while IFS= read -r f; do
    reason="$(pp_balance "$f" || true)"
    if [ -z "$reason" ]; then
        ok "$f: #if/#endif balanced"
    else
        bad "$f: $reason"
    fi
done < <(find wokwi-esp32-project/arduino-ide wokwi-water-pump-c3/arduino-ide \
            \( -name '*.h' -o -name '*.cpp' \) -not -path '*/.pio/*' | sort)

echo
echo "== 1d. generated copies match their sources ============================"
while IFS= read -r hdr; do
    base="$(basename "$hdr")"
    srcfile="$(dirname "$(dirname "$(dirname "$hdr")")")/include/$base"
    if [ -f "$srcfile" ]; then
        if cmp -s "$hdr" "$srcfile"; then
            ok "$hdr == $srcfile"
        else
            bad "$hdr differs from $srcfile - run scripts/sync-arduino-ide.sh"
        fi
    fi
done < <(find wokwi-esp32-project/arduino-ide wokwi-water-pump-c3/arduino-ide \( -name 'arc_math.h' -o -name 'logo_bitmap.h' \) | sort)

echo
echo "== 1e. OTA identities (one target per board, never shared) ============="
# Over-the-air updates pick the image by FW_TARGET. Two boards sharing a target
# would mean a board can be handed an image built for the other one (the C3 and
# the DevKit have different pins), so every Arduino IDE folder must declare its
# own target, and no target may be used twice.
ota_targets=""
ota_ok=1
while IFS= read -r mach; do
    folder="$(basename "$(dirname "$mach")")"
    target="$(sed -nE 's/^[[:space:]]*#define[[:space:]]+FW_TARGET[[:space:]]+"([^"]+)".*/\1/p' "$mach" | head -1)"
    if [ -z "$target" ]; then
        bad "$mach does not define FW_TARGET - the server cannot tell which image belongs to $folder"
        ota_ok=0
        continue
    fi
    case " $ota_targets " in
        *" $target "*)
            bad "FW_TARGET \"$target\" is used by more than one board folder - an image could reach the wrong board"
            ota_ok=0
            ;;
        *) ota_targets="$ota_targets $target"; ok "$folder -> FW_TARGET \"$target\"" ;;
    esac
done < <(find wokwi-esp32-project/arduino-ide wokwi-water-pump-c3/arduino-ide -name config.machine.h | sort)
# The shared configs must offer a default, and the projects must ship OTA code.
for cfg in wokwi-esp32-project/include/config.h wokwi-water-pump-c3/include/config.h; do
    if grep -qE '^[[:space:]]*#ifndef[[:space:]]+FW_TARGET' "$cfg" && grep -qE '^[[:space:]]*#ifndef[[:space:]]+OTA_ENABLED' "$cfg"; then
        ok "$cfg offers FW_TARGET + OTA_ENABLED defaults"
    else
        bad "$cfg must define FW_TARGET and OTA_ENABLED defaults"
        ota_ok=0
    fi
done
if grep -q "action, \"ota\"" wokwi-esp32-project/src/main.cpp && grep -q 'action == "ota"' wokwi-water-pump-c3/src/main.cpp; then
    ok "both firmwares handle the 'ota' command"
else
    bad "a firmware does not handle the 'ota' command"
fi

echo
echo "== 2. the shared firmware sources ======================================"
while IFS= read -r main; do
    lines=$(wc -l < "$main")
    if [ "$lines" -lt 200 ]; then
        bad "$main is only $lines lines - the real firmware should be here"
    else
        ok "$main ($lines lines) - the only firmware code for this board"
    fi
done < <(find wokwi-esp32-project wokwi-water-pump-c3 -path '*/src/main.cpp' -not -path '*/.pio/*' | sort)

echo
echo "== 3. config chains ===================================================="
# The single source of truth is <project>/include/config.h. Every other config.h
# is either a shim that includes it (Wokwi root copy) or a GENERATED copy of it
# (Arduino IDE sketch folder, where includes cannot leave the folder).
while IFS= read -r cfg; do
    case "$cfg" in
        */arduino-ide/*/config.h)
            if grep -q 'GENERATED FILE - DO NOT EDIT THIS COPY' "$cfg" && \
               grep -q 'copy of .*include/config.h' "$cfg"; then
                ok "$cfg is a generated copy of include/config.h (self-contained sketch folder)"
            else
                bad "$cfg is not the generated copy - run scripts/sync-arduino-ide.sh"
            fi
            ;;
        *)
            if grep -qE '#include[[:space:]]+"[^"]*include/config\.h"' "$cfg"; then
                ok "$cfg -> include/config.h (shim)"
            elif grep -qE 'DEVICE_ROLE|ECHO_FORWARD_PIN|RELAY_PIN|MOTION_CRUISE_PWM' "$cfg"; then
                bad "$cfg duplicates firmware configuration - it must include the shared config instead"
            else
                inf "$cfg (no shared include, no duplicated firmware config)"
            fi
            ;;
    esac
done < <(find wokwi-esp32-project wokwi-water-pump-c3 -name config.h -not -path '*/.pio/*' -not -path '*/include/config.h' | sort)

# the machine-value files of each Arduino IDE board folder (the part you own)
while IFS= read -r mach; do
    if grep -qE 'WIFI_SSID|RELAY_PIN|CHRH_FORCE_GATEWAY_MODE' "$mach"; then
        ok "$mach holds this board's machine values (Wi-Fi / pins)"
    else
        inf "$mach (no machine values - the board uses the shared defaults)"
    fi
done < <(find wokwi-esp32-project/arduino-ide wokwi-water-pump-c3/arduino-ide -name config.machine.h | sort)

echo
echo "== 4. secrets stay in the central config ==============================="
for pat in ROBOT_TOKEN PUMP_TOKEN; do
    # A generated Arduino IDE config.h is a byte-compared copy of the central
    # config (checked in section 3), so it is not a second source of truth.
    hits=$(grep -rl "define[[:space:]]*$pat" --include='*.h' --include='*.ino' --include='*.cpp' . 2>/dev/null \
             | grep -v '/\.pio/' \
             | while read -r f; do grep -q 'GENERATED FILE - DO NOT EDIT THIS COPY' "$f" 2>/dev/null || printf '%s\n' "$f"; done \
             | sort | tr '\n' ' ')
    count=$(printf '%s' "$hits" | wc -w)
    if [ "$count" -le 1 ]; then
        ok "$pat is defined in exactly one place"
    elif printf '%s' "$hits" | grep -q 'config\.local\.h'; then
        ok "$pat: central config + a local override"
    else
        bad "$pat appears in $count files: $hits"
    fi
done
# Per-board Wi-Fi is intentional (each machine joins its own network), but it
# may only live in include/config.h or next to the sketch that uses it.
mapfile -t wifi_hits < <(grep -rl -E "define[[:space:]]*WIFI_(SSID|PASSWORD)" --include='*.h' --include='*.ino' --include='*.cpp' . 2>/dev/null | grep -v '/\.pio/' | sort)
wifi_bad=""
for f in "${wifi_hits[@]:-}"; do
    [ -z "${f:-}" ] && continue
    case "$f" in
        ./wokwi-esp32-project/include/config.h|./wokwi-water-pump-c3/include/config.h) ;;
        ./wokwi-*/arduino-ide/*/config.h|./wokwi-*/arduino-ide/*/config.machine.h|./wokwi-*/config.local.h) ;;
        *) wifi_bad="$wifi_bad $f" ;;
    esac
done
if [ -z "$wifi_bad" ]; then
    ok "Wi-Fi credentials only in the central config or a board sketch folder (${#wifi_hits[@]} files)"
else
    bad "Wi-Fi credentials found in unexpected files:$wifi_bad"
fi

echo
echo "== 5. versions + firmware binaries ====================================="
grep -h -E '^#define (FW_VERSION|PUMP_FW_VERSION)' wokwi-esp32-project/include/config.h wokwi-water-pump-c3/include/config.h | sed 's/^/  --    /'
while IFS= read -r bin; do
    src="$(dirname "$(dirname "$bin")")/src/main.cpp"
    if [ "$src" -nt "$bin" ]; then
        warn "$bin is OLDER than $src - Wokwi loads the committed firmware.bin, so run: ./scripts/refresh-firmware.sh all"
    else
        ok "$bin is newer than the firmware source"
    fi
done < <(find wokwi-esp32-project wokwi-water-pump-c3 -path '*/firmware/firmware.bin' -not -path '*/.pio/*' | sort)

echo
if [ "$FAIL" -eq 0 ]; then
    echo "All code copies are consistent: one firmware source, one config per board."
else
    echo "PROBLEMS FOUND - fix them before building or flashing."
fi
exit "$FAIL"
