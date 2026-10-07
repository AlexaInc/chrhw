// ---------------------------------------------------------------------------
// CHR water-pump controller - Arduino IDE sketch.
//
// SELF-CONTAINED SKETCH FOLDER (Arduino IDE).
// Everything this sketch needs is inside this folder - the Arduino IDE only
// compiles files that live in the sketch folder, so the firmware source is a
// copy of the real one instead of an include that would leave the folder:
//
//   main.cpp            the firmware (a copy - the IDE compiles it for you)
//   config.h            GENERATED: config.machine.h + a copy of wokwi-water-pump-c3/include/config.h
//   config.machine.h    Wi-Fi / pins / version of THIS board  <- edit this one
//
// Editing the firmware? Change src/main.cpp in the repository root and run
//     bash scripts/sync-arduino-ide.sh
// so this folder is refreshed (check-code-copies.sh compares them for you).
//
// Board settings (Arduino IDE):
//   Board            : "ESP32C3 Dev Module"   (ESP32-C3 Super Mini)
//   USB CDC On Boot  : Enabled (Serial over USB)
//   Partition Scheme : "Default 4MB with spiffs"
//   Upload Speed     : 921600
// ---------------------------------------------------------------------------
// No code lives in this tab on purpose: Arduino IDE compiles main.cpp next to
// it, and the firmware defines setup()/loop() there. Keeping this tab empty of
// code is what stops a second copy of the firmware from ever appearing here.
// ---------------------------------------------------------------------------
