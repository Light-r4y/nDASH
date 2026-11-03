# uaDASH

## TL,DR

See https://github.com/Light-r4y/uaDASH/releases for official release which include pre-compiled binaries and flashing utility!

## rusEFI open source ultra affordable CAN-bus dashboard

![main_screen](https://github.com/Light-r4y/dash5_esp32s3/blob/v1/media/main_screen.png) 

The device can display engine operating parameters and bench test coils and injectors.

The project can work on 
* GUITION JC8048W550C
* Waveshare ESP32-S3-Touch-LCD-5 800x480
* Waveshare ESP32-S3-Touch-LCD-7 800x480

For change brightness use swipe up/down on main screen (ESP32-S3-Touch-LCD-7 if solder 1 wire, ESP32-S3-Touch-LCD-5 not tested).

Change main/bench/settings screen use swipe left/right.

For save settings after reboot/power cycle need press "Save" button.

Bench test screen:

![bench_screen](https://github.com/Light-r4y/dash5_esp32s3/blob/v1/media/bench_screen.png)

Settings screen:

![bench_screen](https://github.com/Light-r4y/dash5_esp32s3/blob/v1/media/settings_screen.png)

-------------------------------------------------------------------

## Build with Arduino CLI

```
arduino-cli core update-index
arduino-cli lib install lvgl@8.4.0
arduino-cli lib install LovyanGFX@1.2.0
arduino-cli lib install ESP32_IO_Expander@1.1.0
arduino-cli core install esp32:esp32@3.2.1
arduino-cli -j16 compile -b esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,EventsCore=0 --build-property "compiler.cpp.extra_flags=-D<set-you-dislay> -O2" firmware.ino --output-dir ./artifacts -v
arduino-cli upload -b esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,EventsCore=0 --build-path ./artifacts -p <you-display-COMport> -v
```

-------------------------------------------------------------------

## Flash for use ESP Web flasher
https://espressif.github.io/esptool-js/

1. Press "Connect"
2. Select you connected display COM port (maybe can use boot button on board, for into bootloader mode)
3. Press "Erase flash" (optional)
4. Set flash address 0x0 -> add file "firmware.ino.bootloader.bin"
5. Set flash address 0x8000 -> add file "firmware.ino.partitions.bin"
6. Set flash address 0x10000 -> add file "firmware.ino.bin"
7. Press "Program"
8. Wait and watch carefully as the inscriptions appear on the black screen.
9. If you watch "Hard resetting via RTS pin...", press RST button on board and be happy! Else sad and try repeat or check you binary files.



![esp-flasher](https://github.com/Light-r4y/dash5_esp32s3/blob/v1/media/esp-flasher.png)

-------------------------------------------------------------------

## Build settings if use Arduino IDE

(maybe for best performance compile with -O2/O3 flag)

Required library:
- ESP32 Core v3.2.1 (new version is not built now)

- LVGL v8 (graphic lib)

- LovyanGFX (display driver)

- ESP32_IO_Expander@1.1.0 (if use Waveshare board)

![build_settings](https://github.com/Light-r4y/dash5_esp32s3/blob/v1/media/adruino_settings.jpg)


[README-GUITION.md](README-GUITION.md)

[README-ESP32-S3-Touch-LCD-5.md](README-ESP32-S3-Touch-LCD-5.md)

[README-ESP32-S3-Touch-LCD-7.md](README-ESP32-S3-Touch-LCD-7.md)

## FAQ

### Q: what CANbus protocol is used?

A: uaDASH consumes 'rusEFI CAN broadcast' traffic.

### Q: which of three supported screens is best?

A: ESP32-S3-Touch-LCD-5 from Waveshare has both on-board CAN transceiver and on-board 12v power supply, that one you just need to connect to vehicle!

### Q: why only 800x480?

A: With limited software developers availability, we focus on only one resolution. Sorry, no 1024x600.

### Q: To edit the dash, is it just a world of manually editing the ui_mainscreen.c?

A: made with https://squareline.io/

