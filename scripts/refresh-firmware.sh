#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Rebuild a Wokwi project and copy the freshly built binary into firmware/.
#
# Wokwi for VS Code runs exactly the files listed in wokwi.toml:
#     firmware = "firmware/firmware.bin"
#     elf      = "firmware/firmware.elf"
# so after ANY firmware change run this script (or these two commands) before
# starting the simulator, otherwise Wokwi keeps running the old binary.
#
# Usage:
#   ./scripts/refresh-firmware.sh rover     # wokwi-esp32-project
#   ./scripts/refresh-firmware.sh pump      # wokwi-water-pump-c3
#   ./scripts/refresh-firmware.sh all
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="$HOME/.local/bin:$PATH"

# Refuse to build "successfully" from a half-updated tree: this checks that every
# sketch still includes the ONE firmware source and that no board config has
# drifted into a second copy of the firmware settings.
if [ -x "$ROOT/scripts/check-code-copies.sh" ]; then
    if ! "$ROOT/scripts/check-code-copies.sh" | tail -8; then
        echo >&2
        echo "refusing to build: the code copies are out of step (see above)." >&2
        echo "fix them, or run ./scripts/check-code-copies.sh to see every problem." >&2
        exit 1
    fi
fi

build() {
    local dir="$1"
    echo "==> Building $dir"
    ( cd "$ROOT/$dir" && pio run )
    cp "$ROOT/$dir/.pio/build/esp32dev/firmware.bin" "$ROOT/$dir/firmware/firmware.bin"
    cp "$ROOT/$dir/.pio/build/esp32dev/firmware.elf" "$ROOT/$dir/firmware/firmware.elf"
    echo "==> firmware/ refreshed for $dir"
}

case "${1:-all}" in
    rover) build wokwi-esp32-project ;;
    pump)  build wokwi-water-pump-c3 ;;
    all)   build wokwi-esp32-project; build wokwi-water-pump-c3 ;;
    *) echo "usage: $0 [rover|pump|all]" >&2; exit 2 ;;
esac
