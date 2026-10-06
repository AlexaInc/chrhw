#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Runs the front-arc decision-table unit test on the PC (no board, no Wokwi).
# The test compiles include/arc_math.h - the exact code the firmware runs - so
# "go around, stop only when there is no gap" is verified before flashing.
#
#     ./scripts/test-arc-math.sh
# ---------------------------------------------------------------------------
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(mktemp -d)/arc-math-test"

if ! command -v g++ >/dev/null 2>&1; then
    echo "g++ is required for this test (install build-essential / Xcode tools)." >&2
    exit 127
fi

g++ -std=c++17 -Wall -Wextra -Wno-unused-parameter \
    -I"$ROOT/wokwi-esp32-project/include" \
    "$ROOT/scripts/test-arc-math.cpp" -o "$OUT"
"$OUT"
