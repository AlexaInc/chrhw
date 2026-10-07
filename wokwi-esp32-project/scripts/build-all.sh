#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

command -v wokwi-cli >/dev/null || { echo "ERROR: wokwi-cli is not in PATH"; exit 1; }
command -v pio >/dev/null || { echo "ERROR: PlatformIO CLI (pio) is not in PATH"; exit 1; }

mkdir -p firmware
for chip in r gps l98nmotorcontrl espcam cell3v7 bms3s buck5v dcadapter charger3s cap ecap diode fuse ldo33; do
  wokwi-cli chip compile "$chip.chip.c" -o "$chip.chip.wasm"
done

pio run
cp .pio/build/esp32dev/firmware.bin firmware/firmware.bin
cp .pio/build/esp32dev/firmware.elf firmware/firmware.elf

echo "Full build complete. Run 'Wokwi: Start Simulator' in VS Code."
