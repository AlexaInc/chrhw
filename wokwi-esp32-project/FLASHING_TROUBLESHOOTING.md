# ESP32 not flashing / USB-UART bridge may be damaged

This is for a typical classic ESP32 DevKit V1. Check the exact board/module before following GPIO strap advice. If the board is hot, visibly damaged, or smells burnt, disconnect it and stop.

## Safe isolation test

1. Disconnect the battery, buck converters, L298N, servo, ESP32-CAM UART, SD card and every sensor wire from the ESP32. Remove USB too.
2. With the board unpowered, use a multimeter to check for a hard short between `5V/VIN` and `GND`, and between `3V3` and `GND`. Do not inject power into a board that measures as a short.
3. Connect **USB only**, using a known data-capable cable and a known-good computer port. Check whether a serial port appears/disappears in Device Manager (Windows) or `dmesg`/`lsusb` (Linux).
4. If no serial port appears, the cable/driver/USB connector/USB-UART bridge may be the issue. Reinstalling firmware will not repair a burnt USB-UART IC.
5. If the port appears, use the board's BOOT/EN sequence: hold BOOT (GPIO0 low), tap EN/RESET, start upload, and release BOOT when esptool reports it is connecting. Try a conservative upload speed such as 115200.
6. If the port still cannot enter download mode, keep all peripherals disconnected. On a classic ESP32 with 3.3 V flash, GPIO12/MTDI must be low when reset is sampled; this project's pin map no longer uses GPIO12 for L298N IN4. Check the actual module—some variants have different flash-voltage requirements.

## If the onboard USB-UART bridge is burnt

A damaged CP2102/CH340-type bridge is a hardware repair, not a firmware problem. A known-good external USB-to-UART adapter can sometimes flash a still-working ESP32:

- Adapter TX -> ESP32 RX0 / GPIO3
- Adapter RX -> ESP32 TX0 / GPIO1
- Adapter GND -> ESP32 GND
- Leave adapter VCC/5V disconnected. Power the board from only one safe source, and use **3.3 V UART logic levels**.
- Hold GPIO0 low during reset to enter the ROM downloader.

Do not use a 5 V TTL UART adapter. Do not connect USB plus an external 5 V source unless the board's documented power-path circuit prevents back-feeding. If esptool cannot read the target flash with only these connections and correct straps, the ESP32 module or its SPI flash may be damaged; replacing the board/module is usually safer than repeated attempts.

## Before reconnecting the robot

1. Set and measure the buck output with the ESP32 disconnected; target 5.0 V at the correct input pins.
2. Confirm polarity and grounds. Do not connect the 3S battery directly to `VIN/5V`.
3. Reconnect one subsystem at a time, starting with the low-current logic rail. Leave motors, servo and camera disconnected for the first boot.
4. Verify all sensor/GPIO signals are at or below the ESP32 input limit. HC-SR04 ECHO needs its divider; rain AO/DO must not be 5 V.
5. Connect the motor driver last, with a correctly sized fuse, verified flyback diodes and the motor power path separate from USB.

**සිංහලෙන්:** Programmer IC එක ඇත්තටම පිච්චිලා නම් code එකකින් එය repair කරන්න බැහැ. Battery/motors/camera/sensors සියල්ල ඉවත් කර USB එකෙන් පමණක් board එක පරීක්ෂා කරන්න. External USB-TTL එකක් යොදන විට 3.3V logic පමණක් භාවිතා කරන්න; TX/RX හරස් කර GND එක සම්බන්ධ කරන්න; adapter එකේ 5V/VCC wire එක ESP32ට නොදෙන්න. 5V/3V3 rail එක multimeter එකෙන් පරීක්ෂා නොකර නැවත power දෙන්න එපා.
