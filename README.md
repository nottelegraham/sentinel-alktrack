# AlkTrack — DIY Ratiometric Alkalinity Monitor

**TelegrahamTested** — Firmware v3.0 | Fork: rolandmuncie/AlkTrack

AlkTrack is an open, non-commercial DIY alkalinity monitor for reef aquariums. Version 3.0 supports two user-selectable operating modes:

1. **pH differential mode** — reagent-free trend monitoring using a known reference sample and a tank sample measured in the same reaction chamber.
2. **Acid titration mode** — true acid-demand alkalinity testing using a fixed tank-water sample volume and a known-strength acid reagent.

This project is intended for education, experimentation, and hobby use. It is not a commercial product, a laboratory instrument, or a replacement for independent judgment.

---

## What this fork adds

This fork adds a **Mac/Linux emulator** and a **web dashboard** that are not present in the upstream project. They let you compile and run the firmware on a desktop without any hardware, interact with it via a browser, and develop or test changes with a fast feedback loop.

### Emulator

Compiles `alkalinity_monitor_v3_0.ino` directly with clang++/g++ using lightweight Arduino API stubs. Time is accelerated 1000× by default, so a full 4-hour measurement cycle completes in about 4 seconds of real time. EEPROM state persists to a local `eeprom.bin` file across restarts. All serial commands work identically to hardware.

```bash
cd emulator
make run
```

### Web dashboard

Served on `http://localhost:8080` by the emulator, or on the ESP32's WiFi AP address (`http://192.168.4.1`) on hardware. The dashboard provides:

- Live dKH reading with cycle phase indicator
- pH ref/tank readings, temperature, and SD warning
- SVG sparkline trend chart (last 24 readings)
- **Mode toggle** — switch between pH Differential and Acid Titration without Serial Monitor
- **Titration calibration panel** — set acid normality, sample volume, dose increment, and steps/mL for all pumps directly from the browser
- **3-point pH probe calibration wizard** — guided workflow for pH 7.00 → 4.01 → 10.01 buffer calibration, sends EZO-pH `Cal` commands at each confirmed step
- Run Cycle, Prime, and Emergency Stop buttons

The dashboard auto-refreshes every 2 seconds. It is identical between the emulator and hardware builds — same HTML, same JSON API.

### Screenshots

<!-- Add screenshots here -->

---

## What changed in v3.0

Version 3.0 is no longer only a reagent-free pH differential trend monitor. It now includes a second operating mode for traditional acid titration.

Major changes:

- Stepper-driven peristaltic pumps replace relay-timed pump control.
- Volume is controlled by calibrated steps per mL instead of pump runtime.
- User-selectable modes are available from Serial Monitor or the web dashboard:
  - `MODE PHDI` for pH differential/ratiometric mode.
  - `MODE TITR` for acid titration mode.
- Titration mode uses Pump D for acid reagent dosing.
- Titration mode calculates alkalinity from acid normality, endpoint volume, and sample volume.
- EEPROM calibration now stores operating mode, sample volume, acid normality, pump steps/mL, and dose increment.
- The titration dKH calculation uses the corrected conversion:

```text
dKH = (acid_normality × acid_mL / sample_mL) × 2800
```

---

## What it does

### pH differential mode

Draws a known reference sample into a shared reaction chamber, aerates it, records pH and temperature, then removes it. A tank sample is then drawn into the same chamber, aerated under the same general conditions, and measured with the same pH and temperature probes.

The result is a **reference-anchored alkalinity trend estimate**. This mode is reagent-free and best used to watch whether alkalinity is rising, falling, or stable.

### Acid titration mode

Draws a fixed volume of tank water into the reaction chamber, adds known-strength acid in measured increments using a stepper-driven dosing pump, monitors pH, detects an endpoint near the total alkalinity region, and calculates alkalinity from the acid volume consumed.

This mode is a traditional chemical titration. It consumes reagent and produces acidic waste.

---

## Hardware summary

Minimum v3.0 hardware:

- ESP32 DevKit C, 38-pin
- Atlas Scientific EZO-pH circuit on an electrically isolated carrier
- Lab-grade double-junction pH probe with BNC connector
- DS18B20 waterproof temperature probe
- Four stepper-driven peristaltic pumps:
  - Pump A: reference reservoir to/from reaction chamber
  - Pump B: tank/sample fill
  - Pump C: chamber drain/waste or controlled evacuation
  - Pump D: acid reagent dosing for titration mode
- Four A4988 or DRV8825 stepper motor drivers
- Micro air pump and ceramic fine-bubble airstone
- Reference reservoir
- Acid reagent reservoir for titration mode
- Waste collection container for titration mode
- 12V supply rated at 5A minimum
- 5V/USB supply for ESP32
- Optional isolated DAC/BNC output for controller-readable trend logging

See `DIY_Alkalinity_Monitor_Price_Sheet_v3_0.xlsx` for a full component list with estimated costs (~$427–$946 depending on sourcing).

---

## Firmware

Open `alkalinity_monitor_v3_0.ino` in Arduino IDE 2.x or PlatformIO.

Libraries required when `SIMULATION_MODE false`:

- OneWire by Paul Stoffregen
- DallasTemperature by Miles Burton

First run:

1. Upload firmware with `SIMULATION_MODE true` for bench testing, or use the emulator (`cd emulator && make run`).
2. Open Serial Monitor at 115200 baud, or open `http://localhost:8080` if using the emulator.
3. Confirm commands respond with `STATUS`.
4. Set mode: `MODE PHDI` or `MODE TITR`.
5. For pH differential mode, calibrate the pH probe (3-point wizard in the dashboard, or `CAL PH MID/LOW/HIGH` over serial), then set the reference anchor: `CAL 8.50`.
6. For titration mode, set acid normality, sample volume, and pump calibration values.
7. Prime tubing with `PRIME`.
8. Set `SIMULATION_MODE false`, re-upload, and run `RUN`.

---

## Serial commands

| Command | What it does |
|---|---|
| `STATUS` | Show current mode, calibration values, and last result |
| `RUN` | Trigger immediate measurement cycle |
| `MODE PHDI` | Switch to pH differential/ratiometric mode |
| `MODE TITR` | Switch to acid titration mode |
| `CAL 8.50` | Set pH differential reference anchor in dKH |
| `CAL PH MID` | Send Cal,mid,7.00 to EZO-pH (pH probe calibration) |
| `CAL PH LOW` | Send Cal,low,4.01 to EZO-pH |
| `CAL PH HIGH` | Send Cal,high,10.01 to EZO-pH |
| `CAL ACID 0.1000` | Set acid reagent normality for titration mode |
| `CAL SAMPLEML 25.0` | Set titration sample volume in mL |
| `CAL DOSE 0.020` | Set acid dose increment in mL |
| `CAL SAMPLESPM 3200` | Set sample pump calibration in steps/mL |
| `CAL ACIDSPM 6400` | Set acid pump calibration in steps/mL |
| `PRIME` | Prime all fluid lines (all 4 pumps) |
| `PUMPS_OFF` | Emergency stop and cycle abort |

---

## How the two methods work

### pH differential/ratiometric mode

```text
TA_tank = (TA_ref - BA_ref) × carbonate_speciation_ratio + BA_tank
```

Where:

```text
carbonate_speciation_ratio =
[(K1_tank / H_tank) + (2 × K1_tank × K2_tank / H_tank²)] /
[(K1_ref / H_ref) + (2 × K1_ref × K2_ref / H_ref²)]
```

All internal alkalinity math must use compatible units. Inputs and outputs are shown in dKH.

### Acid titration mode

```text
dKH = (N_acid × V_acid_mL / V_sample_mL) × 2800
```

Where `N_acid` is acid normality, `V_acid_mL` is the endpoint acid volume, and `V_sample_mL` is the tank-water sample volume.

---

## Repository layout

```
alkalinity_monitor_v3_0.ino   Main firmware (v3.0)
emulator/                     Mac/Linux emulator and web server
  Makefile                    Build: make run
  main.cpp                    Emulator entry point
  Arduino.h                   Arduino API stubs (time-accelerated)
  EEPROM.h                    File-backed EEPROM stub
  WebServer.h                 Threaded HTTP server (IPv4 + IPv6)
  web_content.h               Dashboard HTML (shared with hardware)
include/
  WebServer_ESP32.h           ESPAsyncWebServer implementation for hardware
  web_content.h               Dashboard HTML (same file, copied)
platformio.ini                PlatformIO config for hardware builds
Version 2/                    Archived v2.1 and v2.2 firmware
```

---

## Documents

| File | Description |
|---|---|
| `DIY_Alkalinity_Monitor_Build_Guide.docx` | Hardware, wiring, assembly, and calibration guide |
| `DIY_Alkalinity_Monitor_User_Guide.docx` | Operating instructions for both modes |
| `DIY_Alkalinity_Monitor_Price_Sheet_v3_0.xlsx` | Component price sheet |
| `DIY_Alkalinity_Monitor_Changelog.docx` | Version history |

---

## License

This project is released under the **Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License (CC BY-NC-SA 4.0)**.

You may fork it, modify it, improve it, share it, and build on it for personal, educational, or hobby use. You may not sell the project files, kits, assembled devices, or commercial products based on this design. Modified versions must be shared under the same license.

Full license:
https://creativecommons.org/licenses/by-nc-sa/4.0/

---

## Disclaimer

This project is provided as-is for educational, experimental, and hobby use. It includes water handling, electrical components, and optional acid reagent handling. Build and use it at your own risk. Verify all results with independent testing before making aquarium dosing decisions.


AlkTrack is an open, non-commercial DIY alkalinity monitor for reef aquariums. Version 3.0 supports two user-selectable operating modes:

1. **pH differential mode** — reagent-free trend monitoring using a known reference sample and a tank sample measured in the same reaction chamber.
2. **Acid titration mode** — true acid-demand alkalinity testing using a fixed tank-water sample volume and a known-strength acid reagent.

This project is intended for education, experimentation, and hobby use. It is not a commercial product, a laboratory instrument, or a replacement for independent judgment.

---

## What changed in v3.0

Version 3.0 is no longer only a reagent-free pH differential trend monitor. It now includes a second operating mode for traditional acid titration.

Major changes:

- Stepper-driven peristaltic pumps replace relay-timed pump control.
- Volume is controlled by calibrated steps per mL instead of pump runtime.
- User-selectable modes are available from Serial Monitor:
  - `MODE PHDI` for pH differential/ratiometric mode.
  - `MODE TITR` for acid titration mode.
- Titration mode uses Pump D for acid reagent dosing.
- Titration mode calculates alkalinity from acid normality, endpoint volume, and sample volume.
- EEPROM calibration now stores operating mode, sample volume, acid normality, pump steps/mL, and dose increment.
- The titration dKH calculation uses the corrected conversion:

```text
dKH = (acid_normality × acid_mL / sample_mL) × 2800
```

---

## What it does

### pH differential mode

Draws a known reference sample into a shared reaction chamber, aerates it, records pH and temperature, then removes it. A tank sample is then drawn into the same chamber, aerated under the same general conditions, and measured with the same pH and temperature probes.

The result is a **reference-anchored alkalinity trend estimate**. This mode is reagent-free and best used to watch whether alkalinity is rising, falling, or stable.

### Acid titration mode

Draws a fixed volume of tank water into the reaction chamber, adds known-strength acid in measured increments using a stepper-driven dosing pump, monitors pH, detects an endpoint near the total alkalinity region, and calculates alkalinity from the acid volume consumed.

This mode is a traditional chemical titration. It consumes reagent and produces acidic waste.

---

## Hardware summary

Minimum v3.0 hardware:

- ESP32 DevKit C, 38-pin
- Atlas Scientific EZO-pH circuit on an electrically isolated carrier
- Lab-grade double-junction pH probe with BNC connector
- DS18B20 waterproof temperature probe
- Four stepper-driven peristaltic pumps:
  - Pump A: reference reservoir to/from reaction chamber
  - Pump B: tank/sample fill
  - Pump C: chamber drain/waste or controlled evacuation
  - Pump D: acid reagent dosing for titration mode
- Four A4988 or DRV8825 stepper motor drivers
- Micro air pump and ceramic fine-bubble airstone
- Reference reservoir
- Acid reagent reservoir for titration mode
- Waste collection container for titration mode
- 12V supply rated at 5A minimum
- 5V/USB supply for ESP32
- Optional isolated DAC/BNC output for controller-readable trend logging

---

## Firmware

Open:

```text
alkalinity_monitor_v3_0/alkalinity_monitor_v3_0.ino
```

Libraries required when `SIMULATION_MODE false`:

- OneWire by Paul Stoffregen
- DallasTemperature by Miles Burton

First run:

1. Upload firmware with `SIMULATION_MODE true` for bench testing.
2. Open Serial Monitor at 115200 baud.
3. Confirm commands respond with `STATUS`.
4. Set mode:
   - `MODE PHDI` for pH differential mode.
   - `MODE TITR` for acid titration mode.
5. For pH differential mode, enter the reference alkalinity anchor, for example: `CAL 8.50`.
6. For titration mode, set acid normality, sample volume, and pump calibration values.
7. Prime tubing with `PRIME`.
8. Set `SIMULATION_MODE false`, re-upload, and run `RUN`.

---

## Serial commands

| Command | What it does |
|---|---|
| `STATUS` | Show current mode, calibration values, and last result |
| `RUN` | Trigger immediate measurement cycle |
| `MODE PHDI` | Switch to pH differential/ratiometric mode |
| `MODE TITR` | Switch to acid titration mode |
| `CAL 8.50` | Set pH differential reference anchor in dKH |
| `CAL ACID 0.1000` | Set acid reagent normality for titration mode |
| `CAL SAMPLEML 25.0` | Set titration sample volume in mL |
| `CAL DOSE 0.020` | Set acid dose increment in mL |
| `CAL SAMPLESPM 3200` | Set sample pump calibration in steps/mL |
| `CAL ACIDSPM 6400` | Set acid pump calibration in steps/mL |
| `PRIME` | Prime fluid lines |
| `PUMPS_OFF` | Emergency stop and cycle abort |

---

## How the two methods work

### pH differential/ratiometric mode

```text
TA_tank = (TA_ref - BA_ref) × carbonate_speciation_ratio + BA_tank
```

Where:

```text
carbonate_speciation_ratio =
[(K1_tank / H_tank) + (2 × K1_tank × K2_tank / H_tank²)] /
[(K1_ref / H_ref) + (2 × K1_ref × K2_ref / H_ref²)]
```

All internal alkalinity math must use compatible units. Inputs and outputs are shown in dKH.

### Acid titration mode

```text
dKH = (N_acid × V_acid_mL / V_sample_mL) × 2800
```

Where `N_acid` is acid normality, `V_acid_mL` is the endpoint acid volume, and `V_sample_mL` is the tank-water sample volume.

---

## Documents

| File | Description |
|---|---|
| `DIY_Alkalinity_Monitor_Build_Guide_v3_0.docx` | Hardware, wiring, assembly, and calibration guide |
| `DIY_Alkalinity_Monitor_User_Guide_v3_0.docx` | Operating instructions for both modes |
| `DIY_Alkalinity_Monitor_Price_Sheet_v3_0.xlsx` | Updated component price sheet |
| `DIY_Alkalinity_Monitor_Changelog_v3_0.docx` | Version history through v3.0 |

---

## License

Free for personal, hobby and other noncommercial use under the [PolyForm Noncommercial License 1.0.0](LICENSE). Selling this software, devices built with it, or any part of it requires written permission from TelegrahamTested.

You may fork, modify and share it for personal, educational or hobby use. You may not sell the project files, kits, assembled devices or commercial products based on this design without written permission.

---

## Disclaimer

This project is provided as-is for educational, experimental, and hobby use. It includes water handling, electrical components, and optional acid reagent handling. Build and use it at your own risk. Verify all results with independent testing before making aquarium dosing decisions.
