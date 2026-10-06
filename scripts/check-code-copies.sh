#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# CHR hardware repo - "are all the code copies still pointing at ONE source?"
#
# The rover and the pump used to exist as several full copies of the same
# firmware (root sketch.ino, src/main.cpp, arduino-ide/<board>/*.ino), which
# drifted apart. They are now shims: every sketch includes ../../src/main.cpp
# (or src/main.cpp for the simulator entry), and every board keeps only its own
# machine values (Wi-Fi, pins) in an arduino-ide/<board>/config.h that includes
# ../../include/config.h.
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
    else
        if grep -qE 'void[[:space:]]+(setup|loop)[[:space:]]*\(' "$ino"; then
            skip "$ino is a standalone sketch (separate device)"
        else
            bad "$ino includes no firmware source and defines no setup()/loop()"
        fi
    fi
done < <(find wokwi-esp32-project wokwi-water-pump-c3 -name '*.ino' -not -path '*/.pio/*' | sort)
[ "$entries" -gt 0 ] || bad "no .ino sketches found at all"

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
while IFS= read -r cfg; do
    if grep -qE '#include[[:space:]]+"\.\./\.\./include/config\.h"' "$cfg"; then
        ok "$cfg -> ../../include/config.h"
    elif grep -qE 'DEVICE_ROLE|ECHO_FORWARD_PIN|RELAY_PIN|MOTION_CRUISE_PWM' "$cfg"; then
        bad "$cfg duplicates firmware configuration - it must include ../../include/config.h instead"
    else
        inf "$cfg (no central include, no duplicated firmware config)"
    fi
done < <(find wokwi-esp32-project/arduino-ide wokwi-water-pump-c3/arduino-ide -name config.h -not -path '*/.pio/*' | sort)

echo
echo "== 4. secrets stay in the central config ==============================="
for pat in ROBOT_TOKEN PUMP_TOKEN; do
    hits=$(grep -rl "define[[:space:]]*$pat" --include='*.h' --include='*.ino' --include='*.cpp' . 2>/dev/null | grep -v '/\.pio/' | sort | tr '\n' ' ')
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
        ./wokwi-*/arduino-ide/*/config.h|./wokwi-*/config.local.h) ;;
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
