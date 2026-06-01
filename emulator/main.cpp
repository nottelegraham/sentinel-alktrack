#include "Arduino.h"
#include "EEPROM.h"

// ESP32 define so dacWrite path is taken
#define ESP32

// Enable web server in emulator
#define WEB_ENABLED 1

#include "WebServer.h"

// Forward declare the web command handler (defined after .ino include)
void handleWebCommand(const char* cmd);

// Include the firmware directly
#include "../alkalinity_monitor_v3_0.ino"

static void syncWebState() {
  g_webState.anchor_dKH = cal.referenceAlk_dKH;
  g_webState.mode       = (int)cal.mode;
  g_webState.acidNorm   = cal.acidNormality;
  g_webState.sampleML   = cal.sampleML;
  g_webState.doseMl     = cal.doseMl;
  g_webState.sampleSPM  = cal.sampleStepsPerML;
  g_webState.acidSPM    = cal.acidStepsPerML;
}

void handleWebCommand(const char* cmd) {
  std::string line(cmd);
  handleSerialCommands_str(line);
  syncWebState();
}

int main() {
  webServer_begin();
  setup();
  syncWebState();
  while (true) {
    loop();
  }
  return 0;
}
