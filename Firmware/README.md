# Sensor module firmware

How the firmware runs, and the lookup pages, are in [docs/home.md](docs/home.md).

# Getting Started 

```pio run -e fuses_bootloader -t fuses```
Burn the fuses

```pio run -e Upload_ISP -t upload```
Upload

# AVR Dude

### Burning Fuses

#### Clock Speed

Use this command to read the current fuses.
```avrdude -C "%USERPROFILE%\.platformio\packages\tool-avrdude\avrdude.conf" -p atmega328pb -P COM3 -b 19200 -c stk500v1 -v -U lfuse:r:-:h -U hfuse:r:-:h -U efuse:r:-:h```

Use this command to write the fuses
```avrdude -C "%USERPROFILE%\.platformio\packages\tool-avrdude\avrdude.conf" -p atmega328pb -P COM3 -b 19200 -c stk500v1 -U lfuse:w:0xE2:m```

0xE2 Should be 11100010, which should set flag 7 (CKDIV8) to 0, stopping the device dividing the system block by 8. This may need to be modified based on what is read, hence the read command first.

#### Brown Out Detection

Use this command to read the fuse.
```avrdude -c stk500v1 -P COM3 -b 19200 -p m328pb -U efuse:r:-:h```

Use this command to write the fuse to 3.3V board (2.7V Brown Out Detection).
```avrdude -c stk500v1 -P COM3 -b 19200 -p m328pb -U efuse:w:0xFD:m```

Needs to be read in firmware and cleared as required. Holds the device in reset if it runs out of power.