$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")

# PlatformIO IDE keeps its CLI in this virtual environment on Windows. Use it
# directly when `pio`/`platformio` has not been added to PATH.
$PioCommand = Get-Command pio -ErrorAction SilentlyContinue
if (-not $PioCommand) { $PioCommand = Get-Command platformio -ErrorAction SilentlyContinue }
if ($PioCommand) {
  $PioExe = $PioCommand.Source
} else {
  $PioExe = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\platformio.exe"
}
if (-not (Test-Path $PioExe) -and -not $PioCommand) {
  throw "PlatformIO was not found. Install 'PlatformIO IDE' in VS Code, wait for installation to finish, then run 'Developer: Reload Window'."
}

$WokwiCommand = Get-Command wokwi-cli -ErrorAction SilentlyContinue
if (-not $WokwiCommand) {
  throw "wokwi-cli is not in PATH. Install it only when rebuilding custom chips; the supplied dist/*.wasm files are already compiled."
}

New-Item -ItemType Directory -Force -Path dist, firmware | Out-Null
foreach ($chip in @("r", "gps", "l98nmotorcontrl", "espcam", "cell3v7", "bms3s", "buck5v")) {
  & $WokwiCommand.Source chip compile "$chip.chip.c" -o "$chip.chip.wasm"
  if ($LASTEXITCODE -ne 0) { throw "Custom chip compilation failed: $chip" }
  Copy-Item "$chip.chip.wasm" "dist/$chip.chip.wasm" -Force
  Copy-Item "$chip.chip.json" "dist/$chip.chip.json" -Force
}

& $PioExe run
if ($LASTEXITCODE -ne 0) { throw "PlatformIO firmware build failed." }
Copy-Item ".pio/build/esp32dev/firmware.bin" "firmware/firmware.bin" -Force
Copy-Item ".pio/build/esp32dev/firmware.elf" "firmware/firmware.elf" -Force
Write-Host "Full build complete. Run 'Wokwi: Start Simulator' in VS Code."
