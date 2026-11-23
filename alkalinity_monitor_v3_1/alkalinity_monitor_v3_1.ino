/*
 * ============================================================
 *  Sentinel AlkTrack — DIY Ratiometric Alkalinity Monitor — v3.1
 *  TelegrahamTested
 * ============================================================
 *
 *  WHAT IS NEW IN v3.1 (safety and accuracy release):
 *
 *  SAFETY
 *    - Builds: required #include lines added.
 *    - Cleanup always runs: a failed or aborted cycle now drains or
 *      returns chamber water. In v3.0 the abort flag blocked the
 *      drain, leaving acid in the chamber for the next fill.
 *    - Every cycle starts by draining the chamber to waste, so a
 *      reboot or crash mid-cycle can never stack a second fill on top.
 *    - High-level (overfill) switch on the chamber. Every fill stops
 *      the moment it trips. Wired fail-safe: a cut wire reads as a fault.
 *    - Software volume ledger: a fill is refused if it would exceed
 *      the chamber volume set with CAL CHAMBER.
 *    - Stepper drivers are disabled between moves (shared ENABLE pin),
 *      so drivers and motors are not energised 24/7.
 *    - PUMPS_OFF is a latched emergency stop. No automatic cycle runs
 *      until RESUME. Settings cannot be changed while a cycle runs.
 *
 *  TITRATION MODE
 *    - Rinse with tank water before every sample (2 passes), so no
 *      leftover endpoint water dilutes the next sample.
 *    - Separate steps/mL calibration for every pump (A, B, C, D), with
 *      a guided CALPUMP routine.
 *    - Titration continues past the endpoint to pH 3.0 so a real Gran
 *      fit can be made on the post-equivalence region (pH 3.0-3.8).
 *      The Gran result does not depend on a pH offset error.
 *    - Fixed pH 4.5 endpoint is interpolated between the two bracketing
 *      points (v3.0 always overshot by up to one dose).
 *    - Coarse doses while pH > 5.8, fine doses after. The 10-minute
 *      pre-aeration is removed (it removed almost no CO2 at pH 8, and
 *      the Gran region is not affected by CO2).
 *    - Unsettled readings are counted and reported.
 *    - Titration refuses to run until the EZO-pH has a 2- or 3-point
 *      calibration.
 *
 *  pH PROBE
 *    - PHCAL commands drive EZO-pH calibration (mid/low/high/clear).
 *    - PHCHECK <buffer> verifies the probe in a buffer (pH 4.00 packet
 *      recommended) and reports pass/fail. PHSLOPE reports slope.
 *
 *  pH DIFFERENTIAL MODE
 *    - Bracketed reference: REF -> TANK -> REF. The tank reading is
 *      compared with the average of both references. If the two
 *      references disagree by more than REF_BRACKET_MAX_DPH (room CO2
 *      or probe moved during the cycle), the result is rejected.
 *    - Temperature-dependent constants (K0 Weiss 1974, K1/K2 Lueker 2000,
 *      KB Dickson 1990) at the measured temperature of each sample.
 *    - Reference is no longer contaminated by tank water: after the
 *      tank sample, the chamber is rinsed with reference to waste
 *      before the second reference fill.
 *    - Equilibrium check: pH drift (slope) over the read window. If
 *      still drifting, aeration is extended (up to 2 retries).
 *    - Will not run without a reference anchor (CAL <dKH>).
 *
 *  CARRIED FORWARD:
 *    EZO-pH UART with temperature compensation, DS18B20, DAC trend
 *    output on GPIO 25, EEPROM settings (v3.0 settings are migrated).
 *
 *  LIBRARY REQUIREMENTS:
 *    SIMULATION_MODE true  — no external libraries needed
 *    SIMULATION_MODE false — install via Arduino Library Manager:
 *      "OneWire"           by Paul Stoffregen
 *      "DallasTemperature" by Miles Burton
 *
 *  NEW WIRING IN v3.1:
 *    GPIO 13 -> ENABLE pin of all four stepper drivers (tied together).
 *               LOW = enabled. 10k pull-up to 3.3V recommended so the
 *               drivers stay off during boot.
 *    GPIO 32 -> Chamber high-level float switch, NORMALLY CLOSED to GND.
 *               Internal pull-up is used. Closed (LOW) = OK.
 *               Open (HIGH) = water at max level OR wire broken = fault.
 *               Set HAS_LEVEL_SWITCH to false ONLY for bench testing.
 *               Mount it at the chamber's max safe level, then measure the
 *               volume up to it and enter CAL CHAMBER <mL>.
 *
 *  STEPPER DRIVER WIRING:
 *    Each A4988 / DRV8825: STEP and DIR to ESP32 GPIO, ENABLE to GPIO 13.
 *    MS1/MS2/MS3 jumpers set microstepping resolution.
 *    Recommended: 1/16 step for water pumps, 1/32 for acid pump.
 *    VMOT: 12V supply. GND: shared with ESP32 GND.
 *
 * ============================================================
 */
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// Copyright (c) 2025-2026 TelegrahamTested. Sentinel AlkTrack.
// Free for personal, hobby and other noncommercial use. Selling requires
// written permission from TelegrahamTested. See LICENSE.

#define SIMULATION_MODE  true
#define HAS_LEVEL_SWITCH true

#include <Arduino.h>
#include <EEPROM.h>
#include <math.h>
#if !SIMULATION_MODE
  #include <OneWire.h>
  #include <DallasTemperature.h>
#endif

const char* FW_VERSION = "3.1";

// ── Operating mode ───────────────────────────────────────────
enum OperatingMode : uint8_t { MODE_PH_DIFF = 0, MODE_TITRATION = 1 };

// ── Pins ─────────────────────────────────────────────────────
// NEVER use GPIO 6-11 (internal SPI flash). Avoid 0, 2, 12, 15 (strapping).
const int PUMP_A_STEP = 23;   // Pump A — reference reservoir (reversible)
const int PUMP_A_DIR  = 22;
const int PUMP_B_STEP = 21;   // Pump B — tank/sump water (reversible)
const int PUMP_B_DIR  = 19;
const int PUMP_C_STEP = 18;   // Pump C — waste drain (forward only)
const int PUMP_C_DIR  = 5;
const int PUMP_D_STEP = 27;   // Pump D — acid injection (forward only, slow)
const int PUMP_D_DIR  = 26;
const int DRV_ENABLE  = 13;   // All driver ENABLE pins, active LOW
const int LEVEL_HIGH  = 32;   // NC float switch to GND, LOW = OK
const int AIR_PUMP      = 4;  // Air pump — relay or MOSFET, on/off
const int PIN_DS18B20   = 14;
const int DAC_TREND_OUT = 25;
#define EZO_SERIAL  Serial2
#define EZO_BAUD    9600
#define EZO_RX_PIN  16
#define EZO_TX_PIN  17

// ── Pump identifiers ─────────────────────────────────────────
enum PumpId : uint8_t { PUMP_A = 0, PUMP_B = 1, PUMP_C = 2, PUMP_D = 3 };
const int  PUMP_STEP_PIN[4] = { PUMP_A_STEP, PUMP_B_STEP, PUMP_C_STEP, PUMP_D_STEP };
const int  PUMP_DIR_PIN[4]  = { PUMP_A_DIR,  PUMP_B_DIR,  PUMP_C_DIR,  PUMP_D_DIR  };
const char PUMP_NAME[4]     = { 'A', 'B', 'C', 'D' };

// Move kinds:
//   MOVE_FILL    — adds liquid to the chamber. Checks level switch,
//                  volume ledger, soft abort and E-stop.
//   MOVE_CLEANUP — removes liquid (return/drain). Runs even after a soft
//                  abort so the chamber is always left empty. Only the
//                  E-stop stops it.
enum MoveKind : uint8_t { MOVE_FILL = 0, MOVE_CLEANUP = 1 };

// ── Stepper speed ────────────────────────────────────────────
const float STEP_SPEED_WATER = 800.0f;   // steps/sec — Pumps A, B, C
const float STEP_SPEED_ACID  = 200.0f;   // steps/sec — Pump D (slow = precise)

// ── Chamber ──────────────────────────────────────────────────
const float CHAMBER_DEFAULT_ML = 40.0f; // default usable volume below the level switch (CAL CHAMBER)
const float CHAMBER_MARGIN_ML = 5.0f;  // headroom always kept free
const float DRAIN_FACTOR     = 2.0f;   // drain pumps 2x the ledger volume
const float DRAIN_MIN_ML     = 20.0f;  // minimum drain volume (unknown contents)
const float RETURN_FACTOR    = 1.2f;   // reversible returns pump 1.2x fill

// ── Timing ───────────────────────────────────────────────────
const unsigned long AERATE_TIME_MS   = 600000UL;   // 10 min CO2 equilibration (pH-diff)
const unsigned long AERATE_EXTEND_MS = 180000UL;   // +3 min if still drifting
const int           AERATE_MAX_RETRIES = 2;
const unsigned long SETTLE_TIME_MS   = 5000UL;
const unsigned long TEST_INTERVAL_MS = 14400000UL; // 4-hour auto cycle
const unsigned long READ_WINDOW_MS   = 60000UL;
const unsigned long READ_PERIOD_MS   = 2000UL;
const float         RINSE_FILL_ML    = 15.0f;      // tank-water rinse per pass
const int           RINSE_PASSES     = 2;
const float         REF_RINSE_ML     = 5.0f;       // reference rinse to waste per pass
const int           REF_RINSE_PASSES = 2;

// ── Measurement quality (pH-diff) ────────────────────────────
const float PH_SD_WARN_THRESHOLD = 0.010f;  // scatter warning
const float PH_DRIFT_MAX_PER_MIN = 0.004f;  // |slope| above this = not equilibrated
const float REF_BRACKET_MAX_DPH  = 0.015f;  // REF1 vs REF2 limit (~3.5% pCO2 change)
const float TEMP_MATCH_WARN_C    = 1.5f;    // warn if ref/tank temps differ more

// ── Titration ────────────────────────────────────────────────
const float          ENDPOINT_PH       = 4.50f;  // interpolated fixed endpoint
const float          COARSE_UNTIL_PH   = 5.80f;  // coarse doses above this pH
const float          COARSE_MULTIPLIER = 5.0f;   // coarse dose = 5 x fine dose
const float          GRAN_PH_HIGH      = 3.80f;  // Gran fit window (post-equivalence)
const float          GRAN_PH_LOW       = 3.00f;
const float          GRAN_STOP_PH      = 3.00f;  // stop dosing at/below this
const float          ABORT_PH          = 2.70f;  // hard abort below this
const int            GRAN_MIN_POINTS   = 5;
const float          GRAN_MIN_R2       = 0.995f;
const float          GRAN_INTERP_WARN  = 0.10f;  // Gran vs 4.5 disagreement flag (10%)
const unsigned long  MIX_PULSE_MS      = 1500UL; // air-pump mixing burst after each dose
const unsigned long  PRE_MIX_MS        = 30000UL;// mix after fill, before first read
const unsigned long  STABILIZE_MS      = 20000UL;// max wait for pH to stabilise
const float          PH_STABLE_DELTA   = 0.003f; // pH "stable" threshold
const float          ACID_EXCESS_ML    = 0.60f;  // acid allowed past MAX_VALID_DKH Ve

// ── pH probe checks ──────────────────────────────────────────
const float PHCHECK_TOLERANCE  = 0.05f;  // pass/fail band for buffer check
const float SLOPE_MIN_PCT      = 95.0f;
const float SLOPE_MAX_PCT      = 105.0f;

// ── Validity limits ──────────────────────────────────────────
const float MIN_VALID_PH     =  6.50f;
const float MAX_VALID_PH     =  9.20f;
const float MIN_VALID_TEMP_C = 15.0f;
const float MAX_VALID_TEMP_C = 35.0f;
const float MIN_VALID_DKH    =  3.0f;
const float MAX_VALID_DKH    = 20.0f;

// ── BNC/DAC trend output mapping ────────────────────────────
// GPIO 25 outputs about 1.03 V (6 dKH) to 2.59 V (12 dKH), linear.
// This is NOT a pH-electrode-level signal (tens of mV). Feeding a
// controller pH input needs an isolated divider/offset stage built for
// that controller. Read the voltage directly with an analog input instead.
const float MAP_DKH_LOW      =  6.0f;
const float MAP_DKH_HIGH     = 12.0f;
const float MAP_FAKE_PH_LOW  =  7.60f;
const float MAP_FAKE_PH_HIGH =  8.30f;

// ── EEPROM ───────────────────────────────────────────────────
const uint32_t EEPROM_SIGNATURE    = 0xA11C0004u; // v3.1
const uint32_t EEPROM_SIGNATURE_V30 = 0xA11C0003u; // v3.0 (migrated)
const int      EEPROM_ADDR         = 0;

struct CalibrationData {
  uint32_t      signature;
  float         referenceAlk_dKH;   // pH-diff anchor
  float         acidNormality;      // eq/L (= mol/L for HCl)
  float         stepsPerML[4];      // A, B, C, D
  float         sampleML;           // sample volume in mL
  float         doseMl;             // fine acid increment in mL
  float         salinity;           // PSU, used by pH-diff constants
  float         chamberMaxML;       // usable chamber volume below the level switch
  OperatingMode mode;
  uint8_t       _pad[3];
};

// v3.0 layout, only used to migrate settings
struct CalibrationDataV30 {
  uint32_t      signature;
  float         referenceAlk_dKH;
  float         acidNormality;
  float         sampleStepsPerML;
  float         acidStepsPerML;
  float         sampleML;
  float         doseMl;
  OperatingMode mode;
  uint8_t       _pad[3];
};

// ── Readings ─────────────────────────────────────────────────
struct SampleReading {
  float pH; float tempC; float pHStdDev; float driftPerMin; bool ok; bool settled;
};
struct StableRead { float pH; bool stable; };

// ── Titration data ───────────────────────────────────────────
struct TitrPoint { float acidML; float pH; bool stable; };
const int MAX_TITR_POINTS = 120;
static TitrPoint titrData[MAX_TITR_POINTS];
static int       titrCount = 0;

// ── Globals ──────────────────────────────────────────────────
static CalibrationData cal;
static volatile bool g_abortCycle = false;  // soft abort: stop fills, run cleanup
static bool  g_estop        = false;        // latched hard stop (PUMPS_OFF)
static bool  g_forceRun     = false;
static bool  g_busy         = false;        // cycle or service routine running
static float g_chamberML    = 1000.0f;   // ledger; unknown at boot = assume full (set in setup)
static char  g_lastFault[96] = "none";

#if SIMULATION_MODE
  enum SimContent : uint8_t { SIM_EMPTY, SIM_REF, SIM_TANK, SIM_TITR, SIM_BUFFER };
  static SimContent g_simContent = SIM_EMPTY;
  static float g_simAcidML = 0.0f;
  static float g_simTankAlk_dKH = 8.20f;
#else
  static OneWire           g_oneWire(PIN_DS18B20);
  static DallasTemperature g_ds18b20(&g_oneWire);
#endif

// ── Forward declarations ─────────────────────────────────────
void          driversEnable(bool on);
bool          levelHigh();
bool          pumpML(PumpId p, float mL, bool forward, MoveKind kind, float stepsPerSec = STEP_SPEED_WATER);
bool          stepMotor(PumpId p, long steps, bool forward, MoveKind kind, float stepsPerSec);
void          drainChamber(float minML);
bool          runPhDiffCycle();
bool          rinseWithTank();
bool          rinseWithReference();
SampleReading measureReference(const char* label);
SampleReading measureTank();
SampleReading aerateAndRead(const char* label);
float         runTitrationCycle();
StableRead    waitForPHStability();
float         interpolateEndpoint(float targetPH);
bool          fitGran(float& ve, float& r2, int& n);
void          runAeration(unsigned long ms);
bool          runDelay(unsigned long ms);
void          sleepUntilNextCycle();
void          failCycle(const char* message);
void          emergencyStop();
SampleReading readAveragedSample(const char* label);
double        lnK0(double T, double S);
double        K1_Lueker(double T, double S);
double        K2_Lueker(double T, double S);
double        KB_Dickson(double T, double S);
double        carbonateTerm(double H, double tempC, double S);
double        borateAlk(double H, double tempC, double S);
float         calculateAlkalinity_dKH(float pH_ref, float t_ref, float pH_tank, float t_tank);
void          updateTrendOutput(float alk_dKH);
float         mapFloat(float x, float inMin, float inMax, float outMin, float outMax);
void          loadCalibration();
bool          calibrationIsValid();
void          initDefaultCalibration();
void          saveCalibration();
void          handleSerialCommands();
void          printStatus();
void          printHelp();
void          primeLines();
void          calibratePump(PumpId p, float nominalML);
void          phCheck(float bufferPH);
int           ezoCalPoints();
String        ezoQuery(const char* cmd, unsigned long waitMs);
void          ezo_setTempComp(float tempC);
float         read_pH_Probe();
float         read_Temp_C();
float         maxAcidML();

// ============================================================
//  SETUP
// ============================================================

void setup() {
  // Drivers off first so pumps cannot twitch during boot
  pinMode(DRV_ENABLE, OUTPUT);
  digitalWrite(DRV_ENABLE, HIGH);

  Serial.begin(115200);
  Serial.setTimeout(100);

  for (int i = 0; i < 4; i++) {
    pinMode(PUMP_STEP_PIN[i], OUTPUT); digitalWrite(PUMP_STEP_PIN[i], LOW);
    pinMode(PUMP_DIR_PIN[i],  OUTPUT); digitalWrite(PUMP_DIR_PIN[i],  LOW);
  }
  pinMode(AIR_PUMP, OUTPUT);      digitalWrite(AIR_PUMP, LOW);
  pinMode(DAC_TREND_OUT, OUTPUT); digitalWrite(DAC_TREND_OUT, LOW);
  pinMode(LEVEL_HIGH, INPUT_PULLUP);

#if !SIMULATION_MODE
  g_ds18b20.begin();
  EZO_SERIAL.begin(EZO_BAUD, SERIAL_8N1, EZO_RX_PIN, EZO_TX_PIN);
  delay(300);
  EZO_SERIAL.print("C,0\r");      // make sure continuous mode is off
  delay(400);
  while (EZO_SERIAL.available()) EZO_SERIAL.read();
#endif

  EEPROM.begin(512);
  loadCalibration();
  g_chamberML = cal.chamberMaxML;   // contents unknown after boot: assume full

  Serial.println();
  Serial.print("Sentinel AlkTrack v"); Serial.println(FW_VERSION);
  Serial.print("Mode: ");
  Serial.println(cal.mode == MODE_PH_DIFF ? "pH Differential" : "Acid Titration");
  if (levelHigh()) {
    Serial.println("WARNING: level switch reads HIGH/OPEN at boot (chamber full or wire broken).");
  }
  printHelp();
}

// ============================================================
//  MAIN LOOP
// ============================================================

void loop() {
  if (g_estop) {
    Serial.println("E-STOP latched. Inspect the machine, then send RESUME.");
    sleepUntilNextCycle();
    return;
  }

  g_abortCycle = false;
  g_busy = true;

  // Always start from an empty chamber. Covers reboot, crash and any
  // earlier failure. Pumping air through the drain is harmless.
  Serial.println("[START] Pre-drain to waste...");
  drainChamber(DRAIN_MIN_ML);

  if (levelHigh()) {
    failCycle("Level switch still HIGH/OPEN after pre-drain. Check drain pump C and switch wiring.");
  } else if (cal.mode == MODE_PH_DIFF) {
    runPhDiffCycle();
  } else {
    float r = runTitrationCycle();
    if (isfinite(r)) updateTrendOutput(r);
  }

  g_busy = false;
  sleepUntilNextCycle();
}

// ============================================================
//  STEPPER / PUMP CONTROL
// ============================================================

void driversEnable(bool on) {
  digitalWrite(DRV_ENABLE, on ? LOW : HIGH);
  if (on) delayMicroseconds(10);
}

bool levelHigh() {
#if HAS_LEVEL_SWITCH && !SIMULATION_MODE
  return digitalRead(LEVEL_HIGH) == HIGH;   // open = full or broken = fault
#elif HAS_LEVEL_SWITCH && SIMULATION_MODE
  return g_chamberML > cal.chamberMaxML;    // simulated switch
#else
  return false;
#endif
}

// Returns true if the full move completed.
bool stepMotor(PumpId p, long steps, bool forward, MoveKind kind, float stepsPerSec) {
  if (steps <= 0) return true;
  if (g_estop) return false;
  if (kind == MOVE_FILL && g_abortCycle) return false;

#if SIMULATION_MODE
  unsigned long simMs = (unsigned long)(steps * 1000.0f / stepsPerSec);
  if (simMs > 3000) simMs = 3000;     // keep bench tests quick
  unsigned long t0 = millis();
  while (millis() - t0 < simMs) {
    handleSerialCommands();
    if (g_estop) return false;
    if (kind == MOVE_FILL && (g_abortCycle || levelHigh())) return false;
    delay(20);
  }
  return true;
#else
  const int stepPin = PUMP_STEP_PIN[p];
  digitalWrite(PUMP_DIR_PIN[p], forward ? HIGH : LOW);
  driversEnable(true);
  delayMicroseconds(5);
  unsigned long halfUs = (unsigned long)(500000.0f / stepsPerSec);
  if (halfUs < 100) halfUs = 100;
  bool completed = true;
  for (long i = 0; i < steps; i++) {
    if ((i & 63) == 0) {
      handleSerialCommands();
      if (g_estop) { completed = false; break; }
      if (kind == MOVE_FILL) {
        if (g_abortCycle) { completed = false; break; }
        if (levelHigh()) {
          completed = false;
          strncpy(g_lastFault, "Level switch tripped during fill", sizeof(g_lastFault) - 1);
          g_abortCycle = true;
          Serial.print("  LEVEL SWITCH TRIPPED — pump "); Serial.print(PUMP_NAME[p]);
          Serial.println(" stopped.");
          break;
        }
      }
    }
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(halfUs);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(halfUs);
  }
  driversEnable(false);
  return completed;
#endif
}

// Moves a volume. Fills are checked against the volume ledger.
bool pumpML(PumpId p, float mL, bool forward, MoveKind kind, float stepsPerSec) {
  if (mL <= 0) return true;
  if (kind == MOVE_FILL) {
    if (g_chamberML + mL > cal.chamberMaxML - CHAMBER_MARGIN_ML + 0.001f) {
      Serial.print("  REFUSED: fill of "); Serial.print(mL, 1);
      Serial.print(" mL would exceed chamber limit (ledger ");
      Serial.print(g_chamberML, 1); Serial.println(" mL).");
      strncpy(g_lastFault, "Fill refused by chamber volume ledger", sizeof(g_lastFault) - 1);
      g_abortCycle = true;
      return false;
    }
  }
  long steps = (long)(mL * cal.stepsPerML[p] + 0.5f);
  bool ok = stepMotor(p, steps, forward, kind, stepsPerSec);
  if (kind == MOVE_FILL) {
    // Count the full volume even on a partial move: overestimating is safe.
    g_chamberML += mL;
#if SIMULATION_MODE
    if (p == PUMP_D) g_simAcidML += mL;
#endif
  }
  return ok;
}

// Drains the chamber to waste with Pump C. Always runs unless E-stop.
void drainChamber(float minML) {
  float vol = g_chamberML * DRAIN_FACTOR;
  if (vol < minML) vol = minML;
  pumpML(PUMP_C, vol, true, MOVE_CLEANUP);
  if (!g_estop) {
    g_chamberML = 0.0f;
#if SIMULATION_MODE
    g_simContent = SIM_EMPTY; g_simAcidML = 0.0f;
#endif
  }
}

// Returns chamber contents through a reversible pump (A or B).
static void returnVia(PumpId p, float filledML) {
  pumpML(p, filledML * RETURN_FACTOR, false, MOVE_CLEANUP);
  if (!g_estop) {
    g_chamberML = 0.0f;
#if SIMULATION_MODE
    g_simContent = SIM_EMPTY;
#endif
  }
}

// ============================================================
//  pH DIFFERENTIAL WORKFLOW  (MODE_PH_DIFF)
//  Order: REF1 -> rinse(tank) -> TANK -> rinse(reference, to waste) -> REF2
// ============================================================

bool runPhDiffCycle() {
  if (cal.referenceAlk_dKH < MIN_VALID_DKH || cal.referenceAlk_dKH > MAX_VALID_DKH) {
    failCycle("No reference anchor set. Send CAL <dKH> (e.g. CAL 8.50).");
    return false;
  }

  SampleReading ref1 = measureReference("REF1");
  if (!ref1.ok || g_abortCycle) { failCycle("Reference 1 failed."); return false; }

  if (!rinseWithTank()) { failCycle("Tank rinse failed."); return false; }

  SampleReading tank = measureTank();
  if (!tank.ok || g_abortCycle) { failCycle("Tank sample failed."); return false; }

  // Remove tank residue before reference touches the chamber again,
  // so returned reference water is never mixed with tank water.
  if (!rinseWithReference()) { failCycle("Reference rinse failed."); return false; }

  SampleReading ref2 = measureReference("REF2");
  if (!ref2.ok || g_abortCycle) { failCycle("Reference 2 failed."); return false; }

  float dRef = fabsf(ref2.pH - ref1.pH);
  Serial.print("Reference bracket: REF1="); Serial.print(ref1.pH, 4);
  Serial.print("  REF2="); Serial.print(ref2.pH, 4);
  Serial.print("  dpH="); Serial.println(dRef, 4);
  if (dRef > REF_BRACKET_MAX_DPH) {
    failCycle("References disagree: room CO2 or probe changed during the cycle. Result rejected.");
    return false;
  }

  float refPH = 0.5f * (ref1.pH + ref2.pH);
  float refT  = 0.5f * (ref1.tempC + ref2.tempC);
  if (fabsf(refT - tank.tempC) > TEMP_MATCH_WARN_C) {
    Serial.print("  WARN: ref/tank temperature differ by ");
    Serial.print(fabsf(refT - tank.tempC), 2);
    Serial.println(" C (compensated, but check chamber insulation).");
  }

  float result_dKH = calculateAlkalinity_dKH(refPH, refT, tank.pH, tank.tempC);
  if (!isfinite(result_dKH) || result_dKH < MIN_VALID_DKH || result_dKH > MAX_VALID_DKH) {
    failCycle("Result out of range.");
    return false;
  }

  Serial.println("----------------------------------------");
  Serial.print("RESULT: "); Serial.print(result_dKH, 2); Serial.println(" dKH  [pH-diff]");
  Serial.print("pH ref(avg): "); Serial.print(refPH, 4);
  Serial.print("  pH tank: "); Serial.print(tank.pH, 4);
  Serial.print("  dPH: "); Serial.println(tank.pH - refPH, 4);
  Serial.print("Temp ref: "); Serial.print(refT, 2);
  Serial.print("  Temp tank: "); Serial.println(tank.tempC, 2);
  if (!ref1.settled || !ref2.settled || !tank.settled)
    Serial.println("NOTE: at least one reading was still drifting after max aeration.");
  Serial.println("----------------------------------------");
  updateTrendOutput(result_dKH);
  return true;
}

bool rinseWithTank() {
  for (int i = 0; i < RINSE_PASSES; i++) {
    Serial.print("[RINSE] Tank water pass "); Serial.println(i + 1);
    if (!pumpML(PUMP_B, RINSE_FILL_ML, true, MOVE_FILL)) {
      returnVia(PUMP_B, RINSE_FILL_ML); return false;
    }
    returnVia(PUMP_B, RINSE_FILL_ML);
    if (g_estop) return false;
  }
  return true;
}

bool rinseWithReference() {
  for (int i = 0; i < REF_RINSE_PASSES; i++) {
    Serial.print("[RINSE] Reference pass "); Serial.print(i + 1); Serial.println(" (to waste)");
    if (!pumpML(PUMP_A, REF_RINSE_ML, true, MOVE_FILL)) { drainChamber(DRAIN_MIN_ML); return false; }
#if SIMULATION_MODE
    g_simContent = SIM_REF;
#endif
    drainChamber(REF_RINSE_ML * 2.0f);
    if (g_estop) return false;
  }
  return true;
}

SampleReading measureReference(const char* label) {
  SampleReading bad = {0, 0, 0, 0, false, false};
  Serial.print("["); Serial.print(label); Serial.println("] Filling from reference reservoir...");
  if (!pumpML(PUMP_A, cal.sampleML, true, MOVE_FILL)) {
    returnVia(PUMP_A, cal.sampleML);
    return bad;
  }
#if SIMULATION_MODE
  g_simContent = SIM_REF;
#endif
  SampleReading r = aerateAndRead(label);
  Serial.print("["); Serial.print(label); Serial.println("] Returning to reservoir...");
  returnVia(PUMP_A, cal.sampleML);
  return r;
}

SampleReading measureTank() {
  SampleReading bad = {0, 0, 0, 0, false, false};
  Serial.println("[TANK] Filling from sump...");
  if (!pumpML(PUMP_B, cal.sampleML, true, MOVE_FILL)) {
    returnVia(PUMP_B, cal.sampleML);
    return bad;
  }
#if SIMULATION_MODE
  g_simContent = SIM_TANK;
#endif
  SampleReading r = aerateAndRead("TANK");
  Serial.println("[TANK] Returning water to sump...");
  returnVia(PUMP_B, cal.sampleML);
  return r;
}

// Aerate, settle, read. If pH is still drifting, aerate longer.
SampleReading aerateAndRead(const char* label) {
  SampleReading r = {0, 0, 0, 0, false, false};
  runAeration(AERATE_TIME_MS);
  for (int attempt = 0; attempt <= AERATE_MAX_RETRIES; attempt++) {
    if (g_abortCycle || g_estop) { r.ok = false; return r; }
    if (!runDelay(SETTLE_TIME_MS)) { r.ok = false; return r; }
    r = readAveragedSample(label);
    if (!r.ok) return r;
    if (r.settled) return r;
    if (attempt < AERATE_MAX_RETRIES) {
      Serial.print("  ["); Serial.print(label);
      Serial.println("] still drifting — extending aeration.");
      runAeration(AERATE_EXTEND_MS);
    }
  }
  return r;   // ok but not settled; caller reports it
}

// ============================================================
//  ACID TITRATION WORKFLOW  (MODE_TITRATION)
// ============================================================

float maxAcidML() {
  // Acid needed for MAX_VALID_DKH plus room for the Gran region.
  float veMax = (MAX_VALID_DKH / 2800.0f) * cal.sampleML / cal.acidNormality;
  return veMax + ACID_EXCESS_ML;
}

float runTitrationCycle() {
  int calPts = ezoCalPoints();
  if (calPts < 2) {
    failCycle("[TITR] pH probe needs a 2- or 3-point calibration (PHCAL). Titration blocked.");
    return NAN;
  }
  if (cal.sampleML + maxAcidML() > cal.chamberMaxML - CHAMBER_MARGIN_ML) {
    failCycle("[TITR] Sample volume + max acid exceeds chamber limit. Lower CAL SAMPLEML.");
    return NAN;
  }

  // 1. Rinse with tank water (returned to sump), then take the sample.
  if (!rinseWithTank()) { failCycle("[TITR] Rinse failed."); drainChamber(DRAIN_MIN_ML); return NAN; }

  Serial.print("[TITR] Filling "); Serial.print(cal.sampleML, 2);
  Serial.println(" mL sample...");
  if (!pumpML(PUMP_B, cal.sampleML, true, MOVE_FILL)) {
    failCycle("[TITR] Sample fill failed."); drainChamber(DRAIN_MIN_ML); return NAN;
  }
#if SIMULATION_MODE
  g_simContent = SIM_TITR; g_simAcidML = 0.0f;
#endif

  // 2. Mix, temperature-compensate, read starting pH
  digitalWrite(AIR_PUMP, HIGH);
  bool mixOk = runDelay(PRE_MIX_MS);
  digitalWrite(AIR_PUMP, LOW);
  if (!mixOk) { failCycle("[TITR] Aborted."); drainChamber(DRAIN_MIN_ML); return NAN; }

  float tempC = read_Temp_C();
  if (!isfinite(tempC) || tempC < MIN_VALID_TEMP_C || tempC > MAX_VALID_TEMP_C) {
    failCycle("[TITR] Invalid temperature. Check DS18B20."); drainChamber(DRAIN_MIN_ML); return NAN;
  }
  ezo_setTempComp(tempC);
  StableRead start = waitForPHStability();
  if (!isfinite(start.pH) || start.pH < MIN_VALID_PH || start.pH > MAX_VALID_PH) {
    failCycle("[TITR] Starting pH invalid. Check probe."); drainChamber(DRAIN_MIN_ML); return NAN;
  }
  Serial.print("[TITR] Start pH: "); Serial.print(start.pH, 4);
  Serial.print("  T="); Serial.print(tempC, 1); Serial.println(" C");

  // 3. Titration loop — coarse, then fine, continue to GRAN_STOP_PH
  float acidML = 0.0f;
  float pH     = start.pH;
  float maxAcid = maxAcidML();
  int   unstable = 0;
  titrCount = 0;
  titrData[titrCount++] = { 0.0f, start.pH, start.stable };

  while (true) {
    if (g_abortCycle || g_estop) { failCycle("[TITR] Aborted."); drainChamber(DRAIN_MIN_ML); return NAN; }

    float dose = (pH > COARSE_UNTIL_PH) ? cal.doseMl * COARSE_MULTIPLIER : cal.doseMl;
    if (!pumpML(PUMP_D, dose, true, MOVE_FILL, STEP_SPEED_ACID)) {
      failCycle("[TITR] Acid dose interrupted."); drainChamber(DRAIN_MIN_ML); return NAN;
    }
    acidML += dose;

    digitalWrite(AIR_PUMP, HIGH);
    delay(MIX_PULSE_MS);
    digitalWrite(AIR_PUMP, LOW);

    StableRead sr = waitForPHStability();
    pH = sr.pH;
    if (!isfinite(pH)) {
      if (g_abortCycle || g_estop) { failCycle("[TITR] Aborted."); }
      else failCycle("[TITR] pH probe lost mid-titration. Check wiring and isolation.");
      drainChamber(DRAIN_MIN_ML); return NAN;
    }
    if (!sr.stable) unstable++;
    if (titrCount < MAX_TITR_POINTS) titrData[titrCount++] = { acidML, pH, sr.stable };

    Serial.print("[TITR] +"); Serial.print(acidML, 3);
    Serial.print(" mL   pH="); Serial.print(pH, 4);
    if (!sr.stable) Serial.print("  (unsettled)");
    Serial.println();

    if (pH < ABORT_PH) {
      failCycle("[TITR] pH below abort limit."); drainChamber(DRAIN_MIN_ML); return NAN;
    }
    if (pH <= GRAN_STOP_PH) break;
    if (acidML >= maxAcid) {
      failCycle("[TITR] Max acid volume reached. Check probe calibration, HCl normality, sample volume.");
      drainChamber(DRAIN_MIN_ML); return NAN;
    }
    if (titrCount >= MAX_TITR_POINTS) {
      failCycle("[TITR] Too many dose points. Increase CAL DOSE."); drainChamber(DRAIN_MIN_ML); return NAN;
    }
  }

  // 4. Equivalence volume: Gran (primary) and interpolated 4.5 (cross-check)
  float veInterp = interpolateEndpoint(ENDPOINT_PH);
  float veGran = NAN, r2 = 0; int nGran = 0;
  bool granOk = fitGran(veGran, r2, nGran);

  float veUsed; const char* method;
  if (granOk) { veUsed = veGran; method = "Gran"; }
  else if (isfinite(veInterp)) { veUsed = veInterp; method = "pH 4.5 interpolated"; }
  else { failCycle("[TITR] No valid endpoint."); drainChamber(DRAIN_MIN_ML); return NAN; }

  float ta_dKH = (cal.acidNormality * veUsed / cal.sampleML) * 2800.0f;

  Serial.println("----------------------------------------");
  if (!isfinite(ta_dKH) || ta_dKH < MIN_VALID_DKH || ta_dKH > MAX_VALID_DKH) {
    failCycle("[TITR] Result out of range. Verify acid normality and pump calibration.");
    drainChamber(DRAIN_MIN_ML); return NAN;
  }
  Serial.print("RESULT: "); Serial.print(ta_dKH, 2); Serial.print(" dKH  [titration, ");
  Serial.print(method); Serial.println("]");
  Serial.print("Ve Gran:   ");
  if (granOk) { Serial.print(veGran, 4); Serial.print(" mL  (n="); Serial.print(nGran);
                Serial.print(", r2="); Serial.print(r2, 4); Serial.println(")"); }
  else Serial.println("unavailable");
  Serial.print("Ve pH 4.5: ");
  if (isfinite(veInterp)) { Serial.print(veInterp, 4); Serial.println(" mL"); }
  else Serial.println("unavailable");
  if (granOk && isfinite(veInterp) && fabsf(veGran - veInterp) / veGran > GRAN_INTERP_WARN)
    Serial.println("WARN: Gran and pH 4.5 endpoints disagree by >10%. Check probe calibration (PHCHECK 4.00).");
  if (unstable > 0) { Serial.print("NOTE: "); Serial.print(unstable); Serial.println(" readings did not fully settle."); }
  Serial.print("Sample: "); Serial.print(cal.sampleML, 2);
  Serial.print(" mL   Acid: "); Serial.print(cal.acidNormality, 4); Serial.println(" N");
  Serial.println("----------------------------------------");

  drainChamber(DRAIN_MIN_ML);
  return ta_dKH;
}

// Wait until consecutive pH reads differ by less than PH_STABLE_DELTA,
// or STABILIZE_MS elapses. NAN only on abort or probe failure.
StableRead waitForPHStability() {
  float prevPH = NAN;
  unsigned long t0 = millis();
  while (millis() - t0 < STABILIZE_MS) {
    handleSerialCommands();
    if (g_abortCycle || g_estop) return { NAN, false };
    float pH = read_pH_Probe();
    if (isfinite(pH)) {
      if (isfinite(prevPH) && fabsf(pH - prevPH) < PH_STABLE_DELTA) return { pH, true };
      prevPH = pH;
    }
    delay(1000);
  }
  return { prevPH, false };
}

// Linear interpolation of acid volume at targetPH between the last
// point above it and the first point at/below it.
float interpolateEndpoint(float targetPH) {
  for (int i = 1; i < titrCount; i++) {
    float p0 = titrData[i - 1].pH, p1 = titrData[i].pH;
    if (p0 > targetPH && p1 <= targetPH) {
      float v0 = titrData[i - 1].acidML, v1 = titrData[i].acidML;
      if (fabsf(p0 - p1) < 1e-6f) return v1;
      return v0 + (v1 - v0) * (p0 - targetPH) / (p0 - p1);
    }
  }
  return NAN;
}

// Gran function after equivalence: F = (Vs + Va) * 10^(-pH)
// F is a straight line rising with Va; it crosses zero at Ve.
// A constant pH offset scales F but does not move the zero crossing.
bool fitGran(float& ve, float& r2, int& n) {
  double sx = 0, sy = 0, sxy = 0, sx2 = 0, sy2 = 0, minX = 1e9;
  n = 0;
  for (int i = 0; i < titrCount; i++) {
    float pH = titrData[i].pH;
    if (pH > GRAN_PH_HIGH || pH < GRAN_PH_LOW) continue;
    double x = titrData[i].acidML;
    double y = (cal.sampleML + x) * pow(10.0, -pH);
    sx += x; sy += y; sxy += x * y; sx2 += x * x; sy2 += y * y; n++;
    if (x < minX) minX = x;
  }
  if (n < GRAN_MIN_POINTS) return false;
  double D = n * sx2 - sx * sx;
  if (fabs(D) < 1e-18) return false;
  double slope = (n * sxy - sx * sy) / D;
  double icpt  = (sy - slope * sx) / n;
  if (slope <= 0) return false;
  double ssxy = sxy - sx * sy / n, ssx = sx2 - sx * sx / n, ssy = sy2 - sy * sy / n;
  if (ssx <= 0 || ssy <= 0) return false;
  r2 = (float)((ssxy * ssxy) / (ssx * ssy));
  ve = (float)(-icpt / slope);
  if (r2 < GRAN_MIN_R2) return false;
  if (!(ve > 0)) return false;
  if (ve >= minX) return false;   // Ve must lie before the first post-equivalence point
  return true;
}

// ============================================================
//  SHARED UTILITIES
// ============================================================

void runAeration(unsigned long ms) {
  Serial.print("[AIR] Aerating "); Serial.print(ms / 60000UL); Serial.println(" min...");
  digitalWrite(AIR_PUMP, HIGH);
  unsigned long t0 = millis(), lastPrint = 0;
  while (millis() - t0 < ms) {
    handleSerialCommands();
    if (g_abortCycle || g_estop) break;
    unsigned long elapsed = millis() - t0;
    if (elapsed >= lastPrint + 60000UL) {
      Serial.print("[AIR] "); Serial.print(elapsed / 60000UL);
      Serial.print("/"); Serial.print(ms / 60000UL); Serial.println(" min");
      lastPrint = elapsed;
    }
    delay(100);
  }
  digitalWrite(AIR_PUMP, LOW);
}

// Returns false if aborted.
bool runDelay(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    handleSerialCommands();
    if (g_abortCycle || g_estop) return false;
    delay(100);
  }
  return true;
}

void sleepUntilNextCycle() {
  if (!g_estop) {
    Serial.print("Next cycle in "); Serial.print(TEST_INTERVAL_MS / 3600000.0f, 1);
    Serial.println(" h. Send HELP for commands.");
  }
  unsigned long t0 = millis();
  while (millis() - t0 < TEST_INTERVAL_MS) {
    handleSerialCommands();
    if (g_forceRun && !g_estop) { g_forceRun = false; return; }
    delay(100);
  }
}

// Soft failure: stops further fills; cleanup still runs.
void failCycle(const char* message) {
  g_abortCycle = true;
  digitalWrite(AIR_PUMP, LOW);
  strncpy(g_lastFault, message, sizeof(g_lastFault) - 1);
  g_lastFault[sizeof(g_lastFault) - 1] = '\0';
  Serial.print("CYCLE FAILED: "); Serial.println(message);
}

// Hard stop: everything stops, latched until RESUME.
void emergencyStop() {
  g_estop = true;
  g_abortCycle = true;
  digitalWrite(AIR_PUMP, LOW);
  driversEnable(false);
  strncpy(g_lastFault, "Emergency stop (PUMPS_OFF)", sizeof(g_lastFault) - 1);
}

// ============================================================
//  MEASUREMENT
// ============================================================

SampleReading readAveragedSample(const char* label) {
  SampleReading result = {0.0f, 0.0f, 0.0f, 0.0f, false, false};

  float tempC = read_Temp_C();
  if (!isfinite(tempC) || tempC < MIN_VALID_TEMP_C || tempC > MAX_VALID_TEMP_C) {
    Serial.print("  ERROR: Invalid temperature: "); Serial.println(tempC, 2);
    return result;
  }
  ezo_setTempComp(tempC);

  Serial.print("  ["); Serial.print(label);
  Serial.print("] T="); Serial.print(tempC, 2);
  Serial.print(" C  Averaging pH for "); Serial.print(READ_WINDOW_MS / 1000UL);
  Serial.println(" s...");

  float pHv[40]; float tMin[40]; int n = 0;
  unsigned long start = millis();
  while ((millis() - start) < READ_WINDOW_MS && n < 40) {
    if (g_abortCycle || g_estop) return result;
    float pH = read_pH_Probe();
    if (isfinite(pH) && pH >= MIN_VALID_PH && pH <= MAX_VALID_PH) {
      pHv[n] = pH; tMin[n] = (millis() - start) / 60000.0f; n++;
    } else Serial.println("    WARN: pH out of range, skipping.");
    if (!runDelay(READ_PERIOD_MS)) return result;
  }

  if (n < 5) {
    Serial.print("  ERROR: Only "); Serial.print(n); Serial.println(" valid reads (need 5).");
    return result;
  }

  double sum = 0, st = 0;
  for (int i = 0; i < n; i++) { sum += pHv[i]; st += tMin[i]; }
  double mean = sum / n, tMean = st / n;
  double var = 0, sxy = 0, sxx = 0;
  for (int i = 0; i < n; i++) {
    double d = pHv[i] - mean; var += d * d;
    sxy += (tMin[i] - tMean) * d; sxx += (tMin[i] - tMean) * (tMin[i] - tMean);
  }
  float sd    = (float)sqrt(var / (n - 1));
  float slope = (sxx > 0) ? (float)(sxy / sxx) : 0.0f;   // pH per minute

  if (sd > PH_SD_WARN_THRESHOLD) {
    Serial.print("  WARN: pH SD="); Serial.print(sd, 5); Serial.println(" (noisy probe or bubbles).");
  }

  Serial.print("  pH="); Serial.print((float)mean, 4);
  Serial.print("  SD="); Serial.print(sd, 5);
  Serial.print("  drift="); Serial.print(slope, 4); Serial.print("/min");
  Serial.print("  n="); Serial.println(n);

  result.pH = (float)mean; result.tempC = tempC; result.pHStdDev = sd;
  result.driftPerMin = slope; result.ok = true;
  result.settled = fabsf(slope) <= PH_DRIFT_MAX_PER_MIN;
  return result;
}

// ============================================================
//  ALKALINITY MATH (pH differential mode)
//
//  After aeration both samples sit in the same room air, so
//  CA = K0 * pCO2 * (K1/H + 2*K1*K2/H^2).
//  Taking the ratio cancels pCO2 when it is the same for both
//  readings (the REF bracket enforces this). K0, K1, K2 are
//  evaluated at each sample's own temperature, so a temperature
//  difference between reference and tank is compensated.
//  Constants are total-scale; the EZO reads NBS-calibrated pH.
//  The offset largely cancels in the ratio (trend use).
// ============================================================

double lnK0(double T, double S) {                     // Weiss 1974, mol/kg/atm
  double T100 = T / 100.0;
  return -60.2409 + 93.4517 * (100.0 / T) + 23.3585 * log(T100)
         + S * (0.023517 - 0.023656 * T100 + 0.0047036 * T100 * T100);
}
double K1_Lueker(double T, double S) {                 // Lueker et al. 2000
  double pK1 = 3633.86 / T - 61.2172 + 9.67770 * log(T) - 0.011555 * S + 0.0001152 * S * S;
  return pow(10.0, -pK1);
}
double K2_Lueker(double T, double S) {
  double pK2 = 471.78 / T + 25.9290 - 3.16967 * log(T) - 0.01781 * S + 0.0001122 * S * S;
  return pow(10.0, -pK2);
}
double KB_Dickson(double T, double S) {                // Dickson 1990
  double sS = sqrt(S);
  double lnKB = (-8966.90 - 2890.53 * sS - 77.942 * S + 1.728 * S * sS - 0.0996 * S * S) / T
              + (148.0248 + 137.1942 * sS + 1.62142 * S)
              + (-24.4344 - 25.085 * sS - 0.2474 * S) * log(T)
              + 0.053105 * sS * T;
  return exp(lnKB);
}

// K0 * (K1/H + 2*K1*K2/H^2) at the sample's temperature
double carbonateTerm(double H, double tempC, double S) {
  double T  = tempC + 273.15;
  double K1 = K1_Lueker(T, S), K2 = K2_Lueker(T, S);
  return exp(lnK0(T, S)) * (K1 / H + 2.0 * K1 * K2 / (H * H));
}

double borateAlk(double H, double tempC, double S) {   // eq/kg
  double T  = tempC + 273.15;
  double KB = KB_Dickson(T, S);
  double BT = 0.000416 * S / 35.0;                      // Uppström 1974
  return BT * KB / (KB + H);
}

float calculateAlkalinity_dKH(float pH_ref, float t_ref, float pH_tank, float t_tank) {
  double S      = cal.salinity;
  double H_ref  = pow(10.0, -(double)pH_ref);
  double H_tank = pow(10.0, -(double)pH_tank);
  double TA_ref = cal.referenceAlk_dKH / 2800.0;
  double carbRef = TA_ref - borateAlk(H_ref, t_ref, S);
  double ctRef   = carbonateTerm(H_ref,  t_ref,  S);
  double ctTank  = carbonateTerm(H_tank, t_tank, S);
  if (!isfinite(ctRef) || !isfinite(ctTank) || ctRef <= 0 || carbRef <= 0) {
    Serial.println("MATH ERROR: Degenerate carbonate terms."); return NAN;
  }
  double TA_tank = carbRef * (ctTank / ctRef) + borateAlk(H_tank, t_tank, S);
  return (float)(TA_tank * 2800.0);
}

// ============================================================
//  DAC TREND OUTPUT
// ============================================================

float mapFloat(float x, float inMin, float inMax, float outMin, float outMax) {
  return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

void updateTrendOutput(float alk_dKH) {
  float fakePH = mapFloat(alk_dKH, MAP_DKH_LOW, MAP_DKH_HIGH, MAP_FAKE_PH_LOW, MAP_FAKE_PH_HIGH);
  if (fakePH < MAP_FAKE_PH_LOW)  fakePH = MAP_FAKE_PH_LOW;
  if (fakePH > MAP_FAKE_PH_HIGH) fakePH = MAP_FAKE_PH_HIGH;
  int dacVal = (int)(mapFloat(fakePH, MAP_FAKE_PH_LOW, MAP_FAKE_PH_HIGH, 80.0f, 200.0f) + 0.5f);
  if (dacVal < 0) dacVal = 0;
  if (dacVal > 255) dacVal = 255;
#if defined(ESP32)
  dacWrite(DAC_TREND_OUT, (uint8_t)dacVal);
#else
  analogWrite(DAC_TREND_OUT, dacVal);
#endif
}

// ============================================================
//  CALIBRATION / EEPROM
// ============================================================

void loadCalibration() {
  uint32_t sig = 0;
  EEPROM.get(EEPROM_ADDR, sig);
  if (sig == EEPROM_SIGNATURE_V30) {
    CalibrationDataV30 old;
    EEPROM.get(EEPROM_ADDR, old);
    initDefaultCalibration();
    if (isfinite(old.referenceAlk_dKH)) cal.referenceAlk_dKH = old.referenceAlk_dKH;
    if (isfinite(old.acidNormality) && old.acidNormality > 0) cal.acidNormality = old.acidNormality;
    if (isfinite(old.sampleStepsPerML) && old.sampleStepsPerML > 0)
      cal.stepsPerML[PUMP_A] = cal.stepsPerML[PUMP_B] = cal.stepsPerML[PUMP_C] = old.sampleStepsPerML;
    if (isfinite(old.acidStepsPerML) && old.acidStepsPerML > 0) cal.stepsPerML[PUMP_D] = old.acidStepsPerML;
    if (isfinite(old.sampleML) && old.sampleML > 0) cal.sampleML = old.sampleML;
    if (isfinite(old.doseMl) && old.doseMl > 0) cal.doseMl = old.doseMl;
    cal.mode = old.mode == MODE_TITRATION ? MODE_TITRATION : MODE_PH_DIFF;
    saveCalibration();
    Serial.println("v3.0 settings migrated. Pumps A and C copied Pump B's calibration —");
    Serial.println("run CALPUMP A and CALPUMP C to calibrate them separately.");
    return;
  }
  EEPROM.get(EEPROM_ADDR, cal);
  if (!calibrationIsValid()) {
    initDefaultCalibration();
    Serial.println("First boot or unknown settings — defaults loaded.");
  }
}

bool calibrationIsValid() {
  if (cal.signature != EEPROM_SIGNATURE) return false;
  for (int i = 0; i < 4; i++)
    if (!isfinite(cal.stepsPerML[i]) || cal.stepsPerML[i] <= 0) return false;
  return isfinite(cal.acidNormality) && cal.acidNormality > 0
      && isfinite(cal.sampleML)      && cal.sampleML > 0
      && isfinite(cal.doseMl)        && cal.doseMl > 0
      && isfinite(cal.salinity)      && cal.salinity > 20 && cal.salinity < 45
      && isfinite(cal.chamberMaxML)  && cal.chamberMaxML >= 20 && cal.chamberMaxML <= 200
      && (cal.mode == MODE_PH_DIFF || cal.mode == MODE_TITRATION);
}

void initDefaultCalibration() {
  memset(&cal, 0, sizeof(cal));
  cal.signature        = EEPROM_SIGNATURE;
  cal.referenceAlk_dKH = 0.0f;      // set with: CAL <dKH>
  cal.mode             = MODE_PH_DIFF;
  cal.acidNormality    = 0.1000f;   // CAL ACID <N>
  cal.stepsPerML[PUMP_A] = 3200.0f; // CALPUMP A
  cal.stepsPerML[PUMP_B] = 3200.0f; // CALPUMP B
  cal.stepsPerML[PUMP_C] = 3200.0f; // CALPUMP C
  cal.stepsPerML[PUMP_D] = 6400.0f; // CALPUMP D
  cal.sampleML         = 25.0f;     // CAL SAMPLEML <mL>
  cal.doseMl           = 0.020f;    // CAL DOSE <mL>
  cal.salinity         = 35.0f;     // CAL SAL <psu>
  cal.chamberMaxML     = CHAMBER_DEFAULT_ML; // CAL CHAMBER <mL>
  saveCalibration();
}

void saveCalibration() {
  EEPROM.put(EEPROM_ADDR, cal);
  EEPROM.commit();
}

// ============================================================
//  SERVICE ROUTINES
// ============================================================

void primeLines() {
  g_busy = true; g_abortCycle = false;
  float v = cal.chamberMaxML - CHAMBER_MARGIN_ML - 5.0f;   // leaves room
  Serial.println("PRIME: pre-drain...");
  drainChamber(DRAIN_MIN_ML);
  Serial.print("  1/4 — Pump A: reservoir -> chamber -> reservoir ("); Serial.print(v, 0); Serial.println(" mL)");
  pumpML(PUMP_A, v, true, MOVE_FILL);  returnVia(PUMP_A, v);
  Serial.print("  2/4 — Pump B: sump -> chamber -> sump ("); Serial.print(v, 0); Serial.println(" mL)");
  pumpML(PUMP_B, v, true, MOVE_FILL);  returnVia(PUMP_B, v);
  Serial.println("  3/4 — Pump D: acid line (5 mL, slow)");
  pumpML(PUMP_D, 5.0f, true, MOVE_FILL, STEP_SPEED_ACID);
  Serial.println("  4/4 — Pump C: drain to waste (also primes the waste line)");
  drainChamber(DRAIN_MIN_ML);
  if (g_abortCycle || g_estop) Serial.println("PRIME stopped early. Check the messages above.");
  else Serial.println("PRIME complete. Check for leaks before first RUN.");
  g_busy = false; g_abortCycle = false;
}

// Runs a nominal volume. User measures it, then sends CALPUMP <p> <measured mL>.
void calibratePump(PumpId p, float nominalML) {
  g_busy = true; g_abortCycle = false;
  Serial.print("CALPUMP "); Serial.print(PUMP_NAME[p]);
  Serial.print(": pumping "); Serial.print(nominalML, 2);
  Serial.println(" mL by current calibration.");
  Serial.println("  The pump's OUTLET must be in a graduated cylinder (not the chamber).");
  long steps = (long)(nominalML * cal.stepsPerML[p] + 0.5f);
  // Outlet is outside the chamber: no ledger, but E-stop still applies.
  stepMotor(p, steps, true, MOVE_CLEANUP, p == PUMP_D ? STEP_SPEED_ACID : STEP_SPEED_WATER);
  Serial.print("  Done. Send: CALPUMP "); Serial.print(PUMP_NAME[p]);
  Serial.println(" <measured mL>   e.g. CALPUMP B 9.85");
  g_busy = false;
}

void phCheck(float bufferPH) {
  g_busy = true; g_abortCycle = false;
  Serial.print("PHCHECK: probe in pH "); Serial.print(bufferPH, 2);
  Serial.println(" buffer. Reading for 60 s...");
#if SIMULATION_MODE
  g_simContent = SIM_BUFFER;
#endif
  float tC = read_Temp_C(); if (isfinite(tC)) ezo_setTempComp(tC);
  float sum = 0; int n = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 60000UL && !g_estop) {
    float v = read_pH_Probe();
    if (isfinite(v)) { sum += v; n++; }
    handleSerialCommands();
    delay(1000);
  }
#if SIMULATION_MODE
  g_simContent = SIM_EMPTY;
#endif
  if (n < 10) { Serial.println("  FAIL: not enough readings. Check probe and wiring."); g_busy = false; return; }
  float mean = sum / n, err = mean - bufferPH;
  Serial.print("  Read "); Serial.print(mean, 3); Serial.print("  error "); Serial.print(err, 3);
  if (fabsf(err) <= PHCHECK_TOLERANCE) Serial.println("  PASS");
  else Serial.println("  FAIL — recalibrate with PHCAL.");
  g_busy = false;
}

// ============================================================
//  SERIAL COMMAND HANDLER
// ============================================================

static PumpId parsePump(char c, bool& ok) {
  ok = true;
  switch (c) { case 'A': return PUMP_A; case 'B': return PUMP_B;
               case 'C': return PUMP_C; case 'D': return PUMP_D; }
  ok = false; return PUMP_A;
}

void handleSerialCommands() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim(); line.toUpperCase();
  if (line.length() == 0) return;

  // Always allowed
  if (line == "PUMPS_OFF" || line == "STOP") {
    emergencyStop();
    Serial.println("EMERGENCY STOP — all pumps and air off. Latched.");
    Serial.println("Inspect the chamber, then send RESUME. The next cycle pre-drains automatically.");
    return;
  }
  if (line == "STATUS") { printStatus(); return; }
  if (line == "HELP")   { printHelp();   return; }
  if (line == "ABORT") {
    if (g_busy) { failCycle("Aborted by user."); Serial.println("Cleanup (return/drain) will still run."); }
    else Serial.println("Nothing running.");
    return;
  }

  // Everything below changes the machine or its settings: idle only.
  if (g_busy) {
    Serial.println("Busy — only STATUS, HELP, ABORT and PUMPS_OFF are accepted during a cycle.");
    return;
  }

  if (line == "RESUME") {
    g_estop = false; g_abortCycle = false;
    Serial.println("E-stop cleared. Send RUN to start a cycle now.");
  }
  else if (line == "RUN") {
    if (g_estop) Serial.println("E-stop latched. Send RESUME first.");
    else { g_forceRun = true; Serial.println("RUN queued."); }
  }
  else if (line == "PRIME") {
    if (g_estop) Serial.println("E-stop latched. Send RESUME first.");
    else primeLines();
  }
  else if (line.startsWith("CALPUMP ")) {
    bool ok; PumpId p = parsePump(line.charAt(8), ok);
    if (!ok) { Serial.println("Example: CALPUMP B   or   CALPUMP B 9.85"); return; }
    float nominal = (p == PUMP_D) ? 2.0f : 10.0f;
    String rest = line.substring(9); rest.trim();
    if (rest.length() == 0) { if (!g_estop) calibratePump(p, nominal); else Serial.println("Send RESUME first."); return; }
    float measured = rest.toFloat();
    if (measured <= nominal * 0.5f || measured >= nominal * 1.5f) {
      Serial.println("Measured volume is more than 50% off nominal. Check tubing, then repeat CALPUMP.");
      return;
    }
    float oldSpm = cal.stepsPerML[p];
    cal.stepsPerML[p] = oldSpm * nominal / measured;
    saveCalibration();
    Serial.print("Pump "); Serial.print(PUMP_NAME[p]); Serial.print(" steps/mL: ");
    Serial.print(oldSpm, 1); Serial.print(" -> "); Serial.println(cal.stepsPerML[p], 1);
    float change = fabsf(cal.stepsPerML[p] - oldSpm) / oldSpm * 100.0f;
    if (change > 3.0f) { Serial.print("  Changed "); Serial.print(change, 1);
                         Serial.println("% — inspect tubing for wear."); }
  }
  else if (line.startsWith("CAL ACID ")) {
    float v = line.substring(9).toFloat();
    if (v > 0 && v < 2.0f) { cal.acidNormality = v; saveCalibration();
      Serial.print("Acid normality: "); Serial.print(v, 4); Serial.println(" N saved"); }
    else Serial.println("Example: CAL ACID 0.1000");
  }
  else if (line.startsWith("CAL SAMPLEML ")) {
    float v = line.substring(13).toFloat();
    // sample + acid for MAX_VALID_DKH + Gran excess must fit below the margin
    const float maxS = (cal.chamberMaxML - CHAMBER_MARGIN_ML - ACID_EXCESS_ML)
                       / (1.0f + MAX_VALID_DKH / 2800.0f / cal.acidNormality);
    if (v >= 5.0f && v <= maxS) { cal.sampleML = v; saveCalibration();
      Serial.print("Sample volume: "); Serial.print(v, 2); Serial.println(" mL saved"); }
    else { Serial.print("Range 5 to "); Serial.print(maxS, 1); Serial.println(" mL. Example: CAL SAMPLEML 25.0"); }
  }
  else if (line.startsWith("CAL DOSE ")) {
    float v = line.substring(9).toFloat();
    if (v >= 0.005f && v <= 0.1f) { cal.doseMl = v; saveCalibration();
      Serial.print("Fine dose: "); Serial.print(v, 4); Serial.println(" mL saved"); }
    else Serial.println("Range 0.005-0.1 mL. Example: CAL DOSE 0.020");
  }
  else if (line.startsWith("CAL CHAMBER ")) {
    float v = line.substring(12).toFloat();
    if (v >= 20.0f && v <= 200.0f) {
      cal.chamberMaxML = v;
      float maxS = (v - CHAMBER_MARGIN_ML - ACID_EXCESS_ML) / (1.0f + MAX_VALID_DKH / 2800.0f / cal.acidNormality);
      if (cal.sampleML > maxS) { cal.sampleML = floorf(maxS); Serial.print("Sample volume reduced to ");
                                 Serial.print(cal.sampleML, 1); Serial.println(" mL to fit."); }
      saveCalibration();
      Serial.print("Chamber volume: "); Serial.print(v, 1); Serial.println(" mL saved");
    } else Serial.println("Range 20-200 mL. Measure the volume up to the level switch. Example: CAL CHAMBER 40");
  }
  else if (line.startsWith("CAL SAL ")) {
    float v = line.substring(8).toFloat();
    if (v > 25.0f && v < 40.0f) { cal.salinity = v; saveCalibration();
      Serial.print("Salinity: "); Serial.print(v, 1); Serial.println(" PSU saved"); }
    else Serial.println("Example: CAL SAL 35.0");
  }
  else if (line.startsWith("CAL ")) {
    float v = line.substring(4).toFloat();
    if (v >= MIN_VALID_DKH && v <= MAX_VALID_DKH) {
      cal.referenceAlk_dKH = v; saveCalibration();
      Serial.print("pH-diff anchor saved: "); Serial.print(v, 2); Serial.println(" dKH");
    } else Serial.println("Invalid. Range 3-20 dKH. Example: CAL 8.50");
  }
  else if (line == "MODE PHDI") {
    cal.mode = MODE_PH_DIFF; saveCalibration();
    Serial.println("Mode: pH Differential (ratiometric)");
    if (cal.referenceAlk_dKH < MIN_VALID_DKH) Serial.println("  WARNING: no anchor set — send CAL <dKH>");
  }
  else if (line == "MODE TITR") {
    cal.mode = MODE_TITRATION; saveCalibration();
    Serial.println("Mode: Acid Titration");
    if (ezoCalPoints() < 2) Serial.println("  WARNING: probe needs PHCAL MID 7.00 then PHCAL LOW 4.00");
  }
  else if (line.startsWith("PHCAL ")) {
    String arg = line.substring(6); arg.trim();
    String cmd;
    if      (arg.startsWith("MID "))  cmd = "Cal,mid,"  + String(arg.substring(4).toFloat(), 2);
    else if (arg.startsWith("LOW "))  cmd = "Cal,low,"  + String(arg.substring(4).toFloat(), 2);
    else if (arg.startsWith("HIGH ")) cmd = "Cal,high," + String(arg.substring(5).toFloat(), 2);
    else if (arg == "CLEAR")          cmd = "Cal,clear";
    else { Serial.println("Examples: PHCAL MID 7.00 | PHCAL LOW 4.00 | PHCAL HIGH 10.00 | PHCAL CLEAR"); return; }
    Serial.println("Probe must sit in the buffer and read steady for 1-2 min before this command.");
    String r = ezoQuery(cmd.c_str(), 1600);
    Serial.print("EZO: "); Serial.println(r.length() ? r : String("(no reply)"));
    if (arg.startsWith("MID")) Serial.println("Note: MID clears LOW/HIGH. Do MID first, then LOW (and HIGH).");
    Serial.print("Calibration points now: "); Serial.println(ezoCalPoints());
  }
  else if (line == "PHSLOPE") {
    String r = ezoQuery("Slope,?", 600);
    Serial.print("EZO slope: "); Serial.println(r);
    int c1 = r.indexOf(','), c2 = r.indexOf(',', c1 + 1);
    if (c1 > 0 && c2 > 0) {
      float acid = r.substring(c1 + 1, c2).toFloat(), base = r.substring(c2 + 1).toFloat();
      bool ok = acid >= SLOPE_MIN_PCT && acid <= SLOPE_MAX_PCT;
      if (base > 0) ok = ok && base >= SLOPE_MIN_PCT && base <= SLOPE_MAX_PCT;
      Serial.println(ok ? "Slope OK." : "Slope outside 95-105% — clean or replace the probe.");
    }
  }
  else if (line.startsWith("PHCHECK")) {
    String arg = line.substring(7); arg.trim();
    float b = arg.length() ? arg.toFloat() : 4.00f;
    if (b > 1.0f && b < 13.0f) phCheck(b); else Serial.println("Example: PHCHECK 4.00");
  }
  else printHelp();
}

void printStatus() {
  Serial.print("Sentinel AlkTrack v"); Serial.println(FW_VERSION);
  Serial.print("Mode: ");             Serial.println(cal.mode == MODE_PH_DIFF ? "pH Differential" : "Acid Titration");
  Serial.print("State: ");            Serial.println(g_estop ? "E-STOP LATCHED" : (g_busy ? "running" : "idle"));
  Serial.print("Last fault: ");       Serial.println(g_lastFault);
  Serial.print("Sim mode: ");         Serial.println(SIMULATION_MODE ? "YES" : "NO");
  Serial.print("Level switch: ");     Serial.println(levelHigh() ? "HIGH/OPEN (fault)" : "OK");
  Serial.print("Chamber ledger: ");   Serial.print(g_chamberML, 1); Serial.println(" mL");
  Serial.print("pH-diff anchor: ");   Serial.print(cal.referenceAlk_dKH, 2); Serial.println(" dKH");
  Serial.print("Salinity: ");         Serial.print(cal.salinity, 1); Serial.println(" PSU");
  Serial.print("Chamber volume: ");   Serial.print(cal.chamberMaxML, 1); Serial.println(" mL");
  Serial.print("Acid normality: ");   Serial.print(cal.acidNormality, 4); Serial.println(" N");
  Serial.print("Sample volume: ");    Serial.print(cal.sampleML, 2); Serial.println(" mL");
  Serial.print("Fine dose: ");        Serial.print(cal.doseMl, 4); Serial.println(" mL");
  for (int i = 0; i < 4; i++) {
    Serial.print("Pump "); Serial.print(PUMP_NAME[i]); Serial.print(" steps/mL: ");
    Serial.println(cal.stepsPerML[i], 1);
  }
  if (!g_busy) { Serial.print("pH cal points: "); Serial.println(ezoCalPoints()); }
}

void printHelp() {
  Serial.println("=== Commands ===");
  Serial.println("STATUS               all settings and state");
  Serial.println("RUN                  start a cycle now");
  Serial.println("ABORT                stop the cycle; chamber is still emptied");
  Serial.println("PUMPS_OFF            emergency stop (latched)");
  Serial.println("RESUME               clear emergency stop");
  Serial.println("PRIME                prime all lines");
  Serial.println("MODE PHDI | MODE TITR");
  Serial.println("CAL <dKH>            pH-diff reference anchor (e.g. CAL 8.50)");
  Serial.println("CAL ACID <N>         acid normality (e.g. CAL ACID 0.1000)");
  Serial.println("CAL SAMPLEML <mL>    sample volume");
  Serial.println("CAL DOSE <mL>        fine acid dose (e.g. CAL DOSE 0.020)");
  Serial.println("CAL SAL <psu>        salinity (e.g. CAL SAL 35.0)");
  Serial.println("CAL CHAMBER <mL>     chamber volume up to the level switch");
  Serial.println("CALPUMP <A|B|C|D>    run a calibration volume");
  Serial.println("CALPUMP <p> <mL>     enter the measured volume");
  Serial.println("PHCAL MID|LOW|HIGH <buffer>, PHCAL CLEAR");
  Serial.println("PHCHECK [buffer]     verify probe (default pH 4.00)");
  Serial.println("PHSLOPE              probe slope health");
}

// ============================================================
//  SENSOR IMPLEMENTATIONS
// ============================================================

String ezoQuery(const char* cmd, unsigned long waitMs) {
#if SIMULATION_MODE
  String c(cmd);
  if (c == "Cal,?")   return "?CAL,2";
  if (c == "Slope,?") return "?Slope,99.7,100.3";
  return "*OK";
#else
  while (EZO_SERIAL.available()) EZO_SERIAL.read();
  EZO_SERIAL.print(cmd); EZO_SERIAL.print('\r');
  delay(waitMs);
  String resp = "";
  unsigned long t0 = millis();
  while (millis() - t0 < 600) {
    while (EZO_SERIAL.available()) {
      char c = EZO_SERIAL.read();
      if (c == '\r') { if (resp.length() && resp != "*OK") return resp; resp = ""; continue; }
      resp += c;
    }
    delay(5);
  }
  return resp.length() ? resp : String("*OK");
#endif
}

int ezoCalPoints() {
  String r = ezoQuery("Cal,?", 400);
  int c = r.indexOf(',');
  if (!r.startsWith("?CAL") || c < 0) return 0;
  return r.substring(c + 1).toInt();
}

void ezo_setTempComp(float tempC) {
#if !SIMULATION_MODE
  char cmd[20];
  snprintf(cmd, sizeof(cmd), "T,%.2f", tempC);
  ezoQuery(cmd, 400);
#else
  (void)tempC;
#endif
}

#if SIMULATION_MODE
// Simple bicarbonate model so titration can be bench-tested.
static float simTitrationPH() {
  double V0 = cal.sampleML / 1000.0, Va = g_simAcidML / 1000.0;
  double A0 = g_simTankAlk_dKH / 2800.0;                 // eq/L
  double C  = A0 * 1.02;                                  // DIC, mol/L
  double Vt = V0 + Va;
  double alk = (A0 * V0 - cal.acidNormality * Va) / Vt;   // remaining alkalinity
  double Ct  = C * V0 / Vt;
  double K1 = 1.0e-6;
  double lo = 1e-10, hi = 1.0;                            // bisection on H
  for (int i = 0; i < 80; i++) {
    double H = sqrt(lo * hi);
    double f = Ct * K1 / (H + K1) - H - alk;              // decreasing in H
    if (f > 0) lo = H; else hi = H;
  }
  return (float)(-log10(sqrt(lo * hi)));
}
#endif

float read_pH_Probe() {
#if SIMULATION_MODE
  float noise = ((float)(millis() % 1000) / 1000.0f - 0.5f) * 0.002f;
  delay(50);
  switch (g_simContent) {
    case SIM_REF:    return 8.200f + noise;
    case SIM_TANK:   return 8.180f + noise;
    case SIM_TITR:   return simTitrationPH() + noise;
    case SIM_BUFFER: return 4.010f + noise;
    default:         return NAN;
  }
#else
  String resp = ezoQuery("R", 1000);
  if (resp.length() == 0 || resp[0] == '?' || resp[0] == '*') return NAN;
  float v = resp.toFloat();
  return (v > 1.5f && v < 12.5f) ? v : NAN;
#endif
}

float read_Temp_C() {
#if SIMULATION_MODE
  return 25.0f;
#else
  g_ds18b20.requestTemperatures();
  float t = g_ds18b20.getTempCByIndex(0);
  return (t == DEVICE_DISCONNECTED_C) ? NAN : t;
#endif
}
