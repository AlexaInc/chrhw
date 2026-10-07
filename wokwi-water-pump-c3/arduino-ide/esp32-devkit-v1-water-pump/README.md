# esp32-devkit-v1-water-pump - Arduino IDE sketch folder

CHR water-pump controller - esp32-devkit-v1-water-pump.

**This folder is self-contained on purpose.** The Arduino IDE can only compile files
that live inside the sketch folder, so the firmware and its headers are copied in
here instead of being included from `../../` (which the IDE cannot follow):

| file | what it is |
|---|---|
| `esp32-devkit-v1-water-pump.ino` | sketch tab - board settings and notes, no code |
| `main.cpp` | a byte-identical copy of `src/main.cpp` (the IDE compiles it) |
| `config.h` | GENERATED: `config.machine.h` + a copy of `include/config.h` |
| `config.machine.h` | **yours**: Wi-Fi, pins, firmware version of this board |
| `config.local.h` | optional, git-ignored, beats everything (not required) |

To use it: open this folder in the Arduino IDE, pick the board from the sketch
header, install the libraries listed there, plug the board in and press Upload.

After a firmware change in the repository:

```bash
bash scripts/sync-arduino-ide.sh      # refresh this folder
bash scripts/check-code-copies.sh     # prove nothing drifted
```
