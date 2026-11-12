/*

Environment:
Board: ESP32-C3 Super Mini
Arduino IDE: 2.3.8
Board Manager: esp32 by Espressif, version 3.3.8 (if missing, add https://espressif.github.io/arduino-esp32/package_esp32_index.json in Preferences -> Additional boards manager URLs)
Selected Board: Tools -> Board -> esp32 -> ESP32C3 Dev Module
Partition Scheme: Huge App
USB CDC On Boot: Enabled
Flash Mode: DIO

Libraries:
1. NimBLE-Arduino by h2zero, version 2.5.0 (install directly from Library Manager)
2. ESP32_BLE_HID_Combo — currently embedded locally inside the project (src/ESP32_BLE_HID_Combo), no separate installation required!
   When a standalone external library is published in the future, simply:
   - delete the src/ESP32_BLE_HID_Combo directory
   - replace the local includes below ("src/ESP32_BLE_HID_Combo/...") with <BleKeyboard.h> and <BleMouse.h>
   Changes made in this library for compatibility with NimBLE 2.5.0 and ESP32 Core 3.x:
   - Hardcoded #define USE_NIMBLE in BleCombo.h
   - Updated callback signatures (onConnect, onDisconnect, onWrite) in BleCombo.cpp and BleCombo.h to include NimBLEConnInfo&
   - Updated NimBLEHIDDevice method calls (e.g., inputReport -> getInputReport, configuration via setters such as setManufacturer)
   - Reordered header includes in BleCombo.cpp to prevent multiple definition conflicts with the standard BLE stack
   - Added pServer->advertiseOnDisconnect(true) to automatically restart advertising after BLE disconnection in NimBLE 2.x
   - Added advertising->setName(deviceName) to broadcast the device name directly in the NimBLE 2.x advertisement packet
   - Added public setters setManufacturer(), setTxPower(), set_vendor_id(), set_product_id(), set_version()
   - Removed deprecated hid->startServices() call (for NimBLE) and unused <driver/adc.h> header
   - Fixed a bug in Mouse.cpp where Mouse_::releaseAll() called release() instead of releaseMouse()
   - Added include guards and relative "BleCombo.h" includes in BleKeyboard.h and BleMouse.h

Usage:
* The board derives a deterministic, unique 14-character Access Point WPA3 password from its internal factory-burned silicon IDs (eFuse Block 2/1 and SPI Flash Unique ID via SHA-256) that remains constant even after an NVS reset.
* If no home Wi-Fi network is configured or the saved network is unreachable, the board starts a WPA3-Only Access Point (limited to 1 client connection) named "vkeyxxxx" (where xxxx are the last 4 hex characters of its MAC address).
* To get the current IP address and Access Point password: pair the board via Bluetooth ("Ugreen BT") with the target computer, focus any text input field (e.g., Notepad), and short-press the BOOT button on the board:
  - In AP setup mode, it will type:
    Please connect to wifi access point vkeya1b2 with password xxxxxxxxxxxxxx, then navigate to captive portal or manually open http://192.168.4.1
  - When connected to a configured home Wi-Fi network, it will type:
    Please connect via configured wifi network to http://192.168.1.xxx (login: admin, password: xxxxxxxxxxxxxx)
* Connect to the "vkeyxxxx" Wi-Fi network from a phone or PC using that password — a Captive Portal will open (or navigate to http://192.168.4.1), where you can use the keyboard/mouse immediately or click "Wi-Fi Settings" (/wifi) to connect to your home Wi-Fi router.
* Once connected to your home Wi-Fi, the web interface is accessible at http://vkey.local or via the board's IP address (shown in your router's DHCP leases as vkeyxxxx).
* Onboard BOOT button (GPIO 9):
  - Short press — types connection instructions (AP SSID/password/URL in AP mode, or the DHCP IP URL when connected to home Wi-Fi) via BLE keyboard.
  - Long press (hold for 3 seconds after power-on) — erases all NVS settings (Wi-Fi credentials, Keyboard/Jiggler/Scroller settings, BLE bonds) and reboots.
    (Note: Do not hold BOOT while plugging in USB power, as GPIO 9 is the ESP32-C3 hardware bootloader strapping pin.)
* Properly supports RDP sessions.
* Target machine must start on the US English keyboard layout (with focus in a plain text input field); Russian Cyrillic letters (А..Я, а..я, Ё, ё) are supported by automatically toggling the host layout via Alt+Shift.
* For debugging: connect via USB cable to a PC with Arduino IDE, select the board and COM port, and open Serial Monitor.

*/

#include "sdkconfig.h"
#if !CONFIG_IDF_TARGET_ESP32C3
#error "vkey supports ESP32-C3 only. Please select 'ESP32C3 Dev Module' in Tools -> Board."
#endif

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_mac.h>
#include <esp_efuse.h>
#include <esp_flash.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
// Local embedded copy of the library (in src/ESP32_BLE_HID_Combo).
// When switching to an external library, comment out these 2 lines and uncomment the next 2:
#include "src/ESP32_BLE_HID_Combo/BleKeyboard.h"
#include "src/ESP32_BLE_HID_Combo/BleMouse.h"
// #include <BleKeyboard.h>
// #include <BleMouse.h>
#include <nvs_flash.h>
#include <Preferences.h>
#include "web_pages.h"
#include "helpers.h"

#ifdef BOOT_BUILTIN
#define BOOT_BUTTON_PIN BOOT_BUILTIN
#else
#define BOOT_BUTTON_PIN 9
#endif
#define BOOT_LONG_PRESS_MS 3000
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define MAX_PRINT_BYTES 32768
#define MAX_AUTH_FAILURES 5
#define SERIAL_LOG_MAX_CHARS 256
#define DEBUG_LOG_KEYS 0
#define CAPS_LOOKAHEAD_WINDOW 16
#define CAPS_MIN_UPPER_RUN 5

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

String myHostname = "vkey";
String apPass = "";
String csrfToken = "";
String savedSsid = "";
String savedPass = "";
String cachedScanJson = "[]";
bool scanCacheValid = false;
bool apMode = false;
bool lastBleConnected = false;

// BLE single-host bonding & link encryption state
bool bleBondLocked = false;
bool bleSessionReady = false;
bool bleRejectSent = false;
unsigned long lastBleSecCheckMs = 0;

// Web authentication brute-force lockout state (RAM until reboot)
int failedAuthCount = 0;
bool authLockedUntilReboot = false;
IPAddress lastFailedIp;

// BOOT button state
bool bootBtnPressed = false;
unsigned long bootBtnPressTime = 0;

// Variables for asynchronous (non-blocking) text typing
String pendingText = "";
int printIndex = 0;
bool isPrinting = false;
bool capsLockActive = false;
bool ruLayoutActive = false;
int nextCapsCheckIndex = 0;
unsigned long lastCharTime = 0;
unsigned long currentDelay = 0;

// Keyboard typing timing settings (ms)
int keyPressMinMs = 30; // How long a key is held down
int keyPressMaxMs = 40;
int keyDelayMinMs = 60; // Delay between keys (including after pressing Shift)
int keyDelayMaxMs = 80;

// Mouse Jiggler settings
bool jiggleEnabled = true;
int jiggleIntervalMinSec = 80;
int jiggleIntervalMaxSec = 120;
int jiggleShiftMinPx = 2;
int jiggleShiftMaxPx = 5;
int jiggleReturnMinMs = 20;
int jiggleReturnMaxMs = 40;

// Mouse Jiggler state variables
unsigned long lastJiggleActionTime = 0;
unsigned long nextJiggleDelay = 0;
bool jiggleShifted = false;
int currentJiggleX = 0;
int currentJiggleY = 0;

// Mouse Scroller settings (independent from Mouse Jiggler)
bool scrollEnabled = false;
int scrollIntervalMinSec = 120;
int scrollIntervalMaxSec = 240;
int scrollBurstMin = 3;
int scrollBurstMax = 5;
int scrollStepDelayMinMs = 60;
int scrollStepDelayMaxMs = 80;
int scrollStepMinTicks = 1;
int scrollStepMaxTicks = 2;
int scrollReturnMinSec = 2;
int scrollReturnMaxSec = 5;

// Mouse Scroller state machine variables
enum ScrollState {
  SCROLL_IDLE = 0,
  SCROLL_BURST_OUT,
  SCROLL_WAIT_RETURN,
  SCROLL_BURST_BACK
};
ScrollState scrollState = SCROLL_IDLE;
unsigned long lastScrollActionTime = 0;
unsigned long nextScrollDelay = 0;
int scrollDirection = 1;
int scrollStepsRemaining = 0;
int scrollAccumulatedTicks = 0;

// Sleep Timer state variables (stored in RAM only, not persisted in NVS)
bool sleepTimerActive = false;
bool sleepTimerExpired = false;
unsigned long sleepTimerStartMs = 0;
unsigned long sleepTimerDurationMs = 8UL * 3600UL * 1000UL;

void updateBleSecurity() {
  if (!bleDevice.isConnected()) {
    if (bleSessionReady || bleRejectSent) {
      bleSessionReady = false;
      bleRejectSent = false;
      bleBondLocked = (NimBLEDevice::getNumBonds() > 0);
    }
    return;
  }
  if (bleRejectSent) {
    return;
  }
  unsigned long now = millis();
  if (bleSessionReady) {
    if (!bleBondLocked && (now - lastBleSecCheckMs >= 500)) {
      lastBleSecCheckMs = now;
      if (NimBLEDevice::getNumBonds() > 0) {
        bleBondLocked = true;
      }
    }
    return;
  }
  if (now - lastBleSecCheckMs < 100) {
    return;
  }
  lastBleSecCheckMs = now;

  NimBLEServer* pSrv = NimBLEDevice::getServer();
  if (!pSrv || pSrv->getConnectedCount() == 0) {
    return;
  }
  NimBLEConnInfo info = pSrv->getPeerInfo((size_t)0);
  if (bleBondLocked && !NimBLEDevice::isBonded(info.getIdAddress())) {
    Serial.println("\nBLE rejected unbonded host");
    bleRejectSent = true;
    pSrv->disconnect(info.getConnHandle());
    return;
  }
  if (info.isEncrypted()) {
    bleSessionReady = true;
    if (NimBLEDevice::getNumBonds() > 0) {
      bleBondLocked = true;
    }
    Serial.println("BLE link encrypted & ready");
  }
}

bool isBleReady() {
  return bleDevice.isConnected() && bleSessionReady;
}

void clampAllSettings() {
  keyPressMinMs = constrain(keyPressMinMs, 1, 1000);
  keyPressMaxMs = constrain(keyPressMaxMs, keyPressMinMs, 1000);
  keyDelayMinMs = constrain(keyDelayMinMs, 1, 1000);
  keyDelayMaxMs = constrain(keyDelayMaxMs, keyDelayMinMs, 1000);

  jiggleIntervalMinSec = constrain(jiggleIntervalMinSec, 1, 86400);
  jiggleIntervalMaxSec = constrain(jiggleIntervalMaxSec, jiggleIntervalMinSec, 86400);
  jiggleShiftMinPx = constrain(jiggleShiftMinPx, 1, 127);
  jiggleShiftMaxPx = constrain(jiggleShiftMaxPx, jiggleShiftMinPx, 127);
  jiggleReturnMinMs = constrain(jiggleReturnMinMs, 1, 5000);
  jiggleReturnMaxMs = constrain(jiggleReturnMaxMs, jiggleReturnMinMs, 5000);

  scrollIntervalMinSec = constrain(scrollIntervalMinSec, 1, 86400);
  scrollIntervalMaxSec = constrain(scrollIntervalMaxSec, scrollIntervalMinSec, 86400);
  scrollBurstMin = constrain(scrollBurstMin, 1, 100);
  scrollBurstMax = constrain(scrollBurstMax, scrollBurstMin, 100);
  scrollStepDelayMinMs = constrain(scrollStepDelayMinMs, 10, 1000);
  scrollStepDelayMaxMs = constrain(scrollStepDelayMaxMs, scrollStepDelayMinMs, 1000);
  scrollStepMinTicks = constrain(scrollStepMinTicks, 1, 127);
  scrollStepMaxTicks = constrain(scrollStepMaxTicks, scrollStepMinTicks, 127);
  scrollReturnMinSec = constrain(scrollReturnMinSec, 1, 3600);
  scrollReturnMaxSec = constrain(scrollReturnMaxSec, scrollReturnMinSec, 3600);
}

void toggleCapsLock(bool logToSerial) {
  Keyboard.press(KEY_CAPS_LOCK);
  delay(random(keyPressMinMs, keyPressMaxMs + 1));
  Keyboard.releaseAll();
  capsLockActive = !capsLockActive;
#if DEBUG_LOG_KEYS
  if (logToSerial) {
    Serial.print(capsLockActive ? "[CAPS:ON]" : "[CAPS:OFF]");
  }
#else
  (void)logToSerial;
#endif
}

void toggleHostLayout(bool logToSerial, bool blockingWait = false) {
  if (capsLockActive) {
    toggleCapsLock(logToSerial);
    delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
  }
  Keyboard.press(KEY_LEFT_ALT);
  delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
  Keyboard.press(KEY_LEFT_SHIFT);
  delay(random(keyPressMinMs, keyPressMaxMs + 1));
  Keyboard.releaseAll();
  ruLayoutActive = !ruLayoutActive;
#if DEBUG_LOG_KEYS
  if (logToSerial) {
    Serial.print(ruLayoutActive ? "[LAYOUT:RU]" : "[LAYOUT:EN]");
  }
#else
  (void)logToSerial;
#endif
  if (blockingWait) {
    delay(150);
  }
}

void eraseAllAndReboot() {
  if (isBleReady()) {
    if (capsLockActive) {
      delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
      toggleCapsLock(true);
    }
    if (ruLayoutActive) {
      delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
      toggleHostLayout(true, true);
    }
    Keyboard.releaseAll();
  }
  Serial.println("\nErasing NVS and rebooting...");
  delay(200);
  nvs_flash_erase();
  nvs_flash_init();
  ESP.restart();
}

void startPrintText(String text) {
  if (!isBleReady() || isPrinting || text.length() == 0) {
    return;
  }
  // Ensure Jiggler cursor is returned and Scroller is stopped before typing starts
  if (jiggleShifted) {
    Mouse.move(-currentJiggleX, -currentJiggleY);
    jiggleShifted = false;
  }
  scrollState = SCROLL_IDLE;
  scrollAccumulatedTicks = 0;

  pendingText = std::move(text);
  printIndex = 0;
  isPrinting = true;
  capsLockActive = false;
  ruLayoutActive = false;
  nextCapsCheckIndex = 0;
  currentDelay = random(keyDelayMinMs, keyDelayMaxMs + 1);
  lastCharTime = millis();

#if DEBUG_LOG_KEYS
  Serial.printf("Print started (%u chars): ", (unsigned int)pendingText.length());
  if (pendingText.length() > SERIAL_LOG_MAX_CHARS) {
    Serial.print(pendingText.substring(0, SERIAL_LOG_MAX_CHARS));
    Serial.println("... [truncated]");
  } else {
    Serial.println(pendingText);
  }
  Serial.print("Keys: ");
#else
  Serial.printf("Print started (%u chars)\n", (unsigned int)pendingText.length());
#endif
}

void handleBootButton(bool allowShortPress = true) {
  bool isLow = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  unsigned long now = millis();

  if (isLow) {
    if (!bootBtnPressed) {
      bootBtnPressed = true;
      bootBtnPressTime = now;
    } else if (now - bootBtnPressTime >= BOOT_LONG_PRESS_MS) {
      // Long press on BOOT button — full NVS reset and reboot
      Serial.println("\nBOOT long press (3s) -> full NVS reset");
      eraseAllAndReboot();
    }
  } else {
    if (bootBtnPressed) {
      unsigned long heldMs = now - bootBtnPressTime;
      bootBtnPressed = false;
      if (heldMs >= 50 && heldMs < BOOT_LONG_PRESS_MS) {
        // Short press on BOOT button — type human-readable connection instructions
        if (!allowShortPress) {
          Serial.println("\nBOOT short press -> ignored (Wi-Fi connecting, IP not ready yet)");
        } else if (!isBleReady()) {
          Serial.println("BOOT short press -> ignored (BLE not connected)");
        } else if (isPrinting) {
          Serial.println("\nBOOT short press -> ignored (already printing)");
        } else {
          String infoStr;
          if (apMode) {
            infoStr = "Please connect to wifi access point " + myHostname +
                      " with password " + apPass +
                      ", then navigate to captive portal or manually open http://" +
                      WiFi.softAPIP().toString();
          } else if (WiFi.status() == WL_CONNECTED) {
            infoStr = "Please connect via configured wifi network to http://" +
                      WiFi.localIP().toString() +
                      " (login: admin, password: " + apPass + ")";
          } else {
            infoStr = "Wi-Fi network " + savedSsid +
                      " is currently disconnected (reconnecting). Check router or reboot device to start Access Point";
          }
          Serial.println("BOOT short press -> typing connection info");
          startPrintText(std::move(infoStr));
        }
      }
    }
  }
}

void performWifiScan() {
  Serial.println("Scanning Wi-Fi networks...");
  int n = WiFi.scanNetworks();
  if (n < 0) {
    delay(200);
    n = WiFi.scanNetworks();
  }
  String json = "[";
  bool first = true;
  int uniqueCount = 0;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue; // Skip hidden networks with empty SSID

    // Deduplicate: skip if this SSID was already processed earlier in the scan list
    bool duplicate = false;
    for (int j = 0; j < i; j++) {
      if (WiFi.SSID(j) == ssid) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;

    // Pick the strongest RSSI among all access points broadcasting this SSID
    int bestRssi = WiFi.RSSI(i);
    bool isSecure = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    for (int k = i + 1; k < n; k++) {
      if (WiFi.SSID(k) == ssid && WiFi.RSSI(k) > bestRssi) {
        bestRssi = WiFi.RSSI(k);
        isSecure = (WiFi.encryptionType(k) != WIFI_AUTH_OPEN);
      }
    }

    if (!first) json += ",";
    first = false;
    uniqueCount++;
    json += "{";
    json += "\"ssid\":\"" + jsonEscape(ssid) + "\",";
    json += "\"rssi\":" + String(bestRssi) + ",";
    json += "\"secure\":" + String(isSecure ? "true" : "false");
    json += "}";
  }
  json += "]";
  WiFi.scanDelete();
  cachedScanJson = json;
  scanCacheValid = true;
  bootBtnPressed = false;
  Serial.printf("Wi-Fi scan complete: %d unique networks found (%d total APs)\n", uniqueCount, max(0, n));
}

void startAccessPointMode() {
  apMode = true;
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);
  delay(100);

  // 1. Scan Wi-Fi networks in pure STA mode BEFORE turning on the Access Point
  // so scanning and AP operation never overlap.
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  performWifiScan();

  // 2. Now switch to pure AP mode and start the WPA3-Only Access Point (max_connection = 1)
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAP(myHostname.c_str(), apPass.c_str(), 1, 0, 1, false, WIFI_AUTH_WPA3_PSK);

  IPAddress apIp = WiFi.softAPIP();
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIp);

  Serial.println("\n--- Started Wi-Fi Access Point Mode (WPA3-Only, max 1 client) ---");
  Serial.print("AP SSID: ");
  Serial.println(myHostname);
#if DEBUG_LOG_KEYS
  Serial.print("AP Pass: ");
  Serial.println(apPass);
#endif
  Serial.print("AP IP:   ");
  Serial.println(apIp);
}

String computeRequestHash() {
  if (server.hasArg("plain")) {
    return sha256Hex128(server.arg("plain"));
  }
  String raw = "";
  int n = server.args();
  for (int i = 0; i < n; i++) {
    if (server.argName(i) != "h") {
      raw += server.argName(i) + "=" + server.arg(i) + "\n";
    }
  }
  return sha256Hex128(raw);
}

bool checkAuth() {
  if (authLockedUntilReboot) {
    server.send(429, "text/plain", "SECURITY LOCKOUT: Too many failed login attempts. Reboot device.");
    return false;
  }

  // 1. In STA mode (home/office Wi-Fi), require RFC 2617 HTTP Digest Authentication
  // (In AP mode, the client is already authenticated via WPA3-SAE with max_connection = 1)
  if (!apMode) {
    if (!server.authenticate("admin", apPass.c_str())) {
      if (server.header("Authorization").length() > 0) {
        lastFailedIp = server.client().remoteIP();
        failedAuthCount++;
        Serial.printf("Auth failed (%d/%d) from %s\n",
                      failedAuthCount, MAX_AUTH_FAILURES, lastFailedIp.toString().c_str());
        if (failedAuthCount >= MAX_AUTH_FAILURES) {
          authLockedUntilReboot = true;
          Serial.println("SECURITY LOCKOUT: Web UI locked until reboot!");
          server.send(429, "text/plain", "SECURITY LOCKOUT: Too many failed login attempts. Reboot device.");
          return false;
        }
      }
      server.requestAuthentication(DIGEST_AUTH, "vkey", "Authentication required");
      return false;
    }
    if (failedAuthCount > 0 && server.client().remoteIP() == lastFailedIp) {
      failedAuthCount = 0;
    }
  }

  // 2. For all POST requests, require X-VKey-Token (CSRF defense) and SHA-256 body hash in ?h=
  if (server.method() == HTTP_POST) {
    if (server.header("X-VKey-Token") != csrfToken) {
      server.send(403, "text/plain", "Invalid or missing CSRF token");
      return false;
    }
    String expectedHash = computeRequestHash();
    if (!server.hasArg("h") || server.arg("h") != expectedHash) {
      server.send(403, "text/plain", "Invalid body hash");
      return false;
    }
    if (!apMode) {
      String expectedUriAttr = "uri=\"" + server.uri() + "?h=" + expectedHash + "\"";
      if (server.header("Authorization").indexOf(expectedUriAttr) < 0) {
        server.send(403, "text/plain", "Digest URI mismatch");
        return false;
      }
    }
  } else if (!apMode) {
    // For GET requests in STA mode, ensure the signed Digest URI matches the requested endpoint path
    String authHdr = server.header("Authorization");
    String exactUri = "uri=\"" + server.uri() + "\"";
    String queryUri = "uri=\"" + server.uri() + "?";
    if (authHdr.indexOf(exactUri) < 0 && authHdr.indexOf(queryUri) < 0) {
      server.send(403, "text/plain", "Digest URI mismatch");
      return false;
    }
  }

  return true;
}

void setup() {
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  randomSeed(esp_random());

  Serial.begin(115200);
  for (int i = 0; i < 10; i++) {
    handleBootButton(false);
    delay(100);
  }
  Serial.println("\n--- Run ESP32 BLE Keyboard ---");

  myHostname = buildDeviceHostname();
  Serial.print("Device Hostname / AP SSID: ");
  Serial.println(myHostname);

  // Start BLE immediately on boot so keyboard and BOOT button work even before Wi-Fi connects
  Serial.println("Starting BLE KeyBoard");
  bleDevice.setName("Ugreen BT");
  bleDevice.setManufacturer("UGREEN");
  bleDevice.set_vendor_id(0x2B89);
  bleDevice.set_product_id(0x6201);
  bleDevice.setTxPower(0);
  bleDevice.setBatteryLevel(96);
  Keyboard.begin();
  Mouse.begin();
  bleBondLocked = (NimBLEDevice::getNumBonds() > 0);

  // Initialize settings from NVS (Preferences)
  preferences.begin("jiggler", false);
  keyPressMinMs = preferences.getInt("kb_prs_min", 30);
  keyPressMaxMs = preferences.getInt("kb_prs_max", 40);
  keyDelayMinMs = preferences.getInt("kb_dly_min", 60);
  keyDelayMaxMs = preferences.getInt("kb_dly_max", 80);

  jiggleEnabled = preferences.getBool("enabled", true);
  jiggleIntervalMinSec = preferences.getInt("int_min", 80);
  jiggleIntervalMaxSec = preferences.getInt("int_max", 120);
  jiggleShiftMinPx = preferences.getInt("shift_min", 2);
  jiggleShiftMaxPx = preferences.getInt("shift_max", 5);
  jiggleReturnMinMs = preferences.getInt("ret_min", 20);
  jiggleReturnMaxMs = preferences.getInt("ret_max", 40);

  scrollEnabled = preferences.getBool("sc_enabled", false);
  scrollIntervalMinSec = preferences.getInt("sc_int_min", 120);
  scrollIntervalMaxSec = preferences.getInt("sc_int_max", 240);
  scrollBurstMin = preferences.getInt("sc_cnt_min", 3);
  scrollBurstMax = preferences.getInt("sc_cnt_max", 5);
  scrollStepDelayMinMs = preferences.getInt("sc_dly_min", 60);
  scrollStepDelayMaxMs = preferences.getInt("sc_dly_max", 80);
  scrollStepMinTicks = preferences.getInt("sc_stp_min", 1);
  scrollStepMaxTicks = preferences.getInt("sc_stp_max", 2);
  scrollReturnMinSec = preferences.getInt("sc_ret_min", 2);
  scrollReturnMaxSec = preferences.getInt("sc_ret_max", 5);
  clampAllSettings();

  savedSsid = preferences.getString("wifi_ssid", "");
  savedPass = preferences.getString("wifi_pass", "");
  apPass = generateApPassword();
  csrfToken = generateCsrfToken();

  nextJiggleDelay = random(jiggleIntervalMinSec, jiggleIntervalMaxSec + 1) * 1000UL;
  lastJiggleActionTime = millis();

  nextScrollDelay = random(scrollIntervalMinSec, scrollIntervalMaxSec + 1) * 1000UL;
  lastScrollActionTime = millis();

  // Connect to saved Wi-Fi or fall back to Access Point setup mode
  if (savedSsid.length() > 0) {
    WiFi.setHostname(myHostname.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setAutoReconnect(true);
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());
    WiFi.setSleep(false);

    Serial.print("Connecting to Wi-Fi: ");
    Serial.print(savedSsid);
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt < WIFI_CONNECT_TIMEOUT_MS)) {
      updateBleSecurity();
      handleBootButton(false); // Allow 3s long-press NVS reset while waiting, ignore short press until IP/AP is ready
      delay(100);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      apMode = false;
      Serial.println("\nWiFi Connected");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\nWiFi connection timeout! Falling back to AP mode.");
      startAccessPointMode();
    }
  } else {
    Serial.println("No saved Wi-Fi credentials. Starting AP mode.");
    startAccessPointMode();
  }

  if (MDNS.begin("vkey")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("mDNS responder started: http://vkey.local");
  }

  // --- Configure Web Server routes ---

  const char* headerKeys[] = {"X-VKey-Token", "Authorization"};
  server.collectHeaders(headerKeys, 2);

  server.on("/", []() {
    if (!checkAuth()) return;
    server.send_P(200, "text/html", htmlPage);
  });

  server.on("/type", HTTP_POST, []() {
    if (!checkAuth()) return;
    if (!isBleReady()) {
      server.send(400, "text/plain", "BLE not connected");
      return;
    }
    if (isPrinting) {
      server.send(400, "text/plain", "Already printing");
      return;
    }
    String body = server.hasArg("plain") ? server.arg("plain") : server.arg("payload");
    if (body.length() == 0) {
      server.send(400, "text/plain", "No text");
      return;
    }
    if (body.length() > MAX_PRINT_BYTES) {
      server.send(413, "text/plain", "Text too large (max 32 KB)");
      return;
    }
    startPrintText(std::move(body));
    server.send(200, "text/plain", "Started");
  });

  server.on("/cancel", HTTP_POST, []() {
    if (!checkAuth()) return;
    if (isBleReady()) {
      if (capsLockActive) {
        delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
        toggleCapsLock(true);
      }
      if (ruLayoutActive) {
        delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
        toggleHostLayout(true, true);
      }
    }
    capsLockActive = false;
    ruLayoutActive = false;
    isPrinting = false;
    pendingText = String();
    server.send(200, "text/plain", "Cancelled");
    Serial.println("\nPrint cancelled");
  });

  server.on("/status", []() {
    if (!checkAuth()) return;
    String ipStr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    unsigned long sleepRemSec = 0;
    if (sleepTimerActive && !sleepTimerExpired) {
      unsigned long elapsed = millis() - sleepTimerStartMs;
      if (elapsed < sleepTimerDurationMs) {
        sleepRemSec = (sleepTimerDurationMs - elapsed + 999UL) / 1000UL;
      }
    }
    String json = "{";
    json += "\"connected\":" + String(isBleReady() ? "true" : "false") + ",";
    json += "\"ble_paired\":" + String(bleBondLocked ? "true" : "false") + ",";
    json += "\"printing\":" + String(isPrinting ? "true" : "false") + ",";
    json += "\"progress\":" + String(printIndex) + ",";
    json += "\"total\":" + String(pendingText.length()) + ",";
    json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"max_alloc\":" + String(ESP.getMaxAllocHeap()) + ",";
    json += "\"ap_mode\":" + String(apMode ? "true" : "false") + ",";
    json += "\"hostname\":\"" + jsonEscape(myHostname) + "\",";
    json += "\"ip\":\"" + jsonEscape(ipStr) + "\",";
    json += "\"ssid\":\"" + jsonEscape(savedSsid) + "\",";
    json += "\"token\":\"" + csrfToken + "\",";
    json += "\"sleep_active\":" + String(sleepTimerActive ? "true" : "false") + ",";
    json += "\"sleep_rem_sec\":" + String(sleepRemSec);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/scan", HTTP_GET, []() {
    if (!checkAuth()) return;
    if (!scanCacheValid) {
      performWifiScan();
    }
    server.send(200, "application/json", cachedScanJson);
  });

  server.on("/scan", HTTP_POST, []() {
    if (!checkAuth()) return;
    performWifiScan();
    server.send(200, "application/json", cachedScanJson);
  });

  server.on("/wifi", HTTP_GET, []() {
    if (!checkAuth()) return;
    server.send_P(200, "text/html", wifiPage);
  });

  server.on("/wifi", HTTP_POST, []() {
    if (!checkAuth()) return;
    if (!server.hasArg("ssid") || server.arg("ssid").length() == 0) {
      server.send(400, "text/plain", "SSID is required");
      return;
    }
    String newSsid = server.arg("ssid");
    String newPass = server.hasArg("pass") ? server.arg("pass") : "";

    preferences.putString("wifi_ssid", newSsid);
    preferences.putString("wifi_pass", newPass);

    if (isBleReady()) {
      if (capsLockActive) {
        delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
        toggleCapsLock(true);
      }
      if (ruLayoutActive) {
        delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
        toggleHostLayout(true, true);
      }
      Keyboard.releaseAll();
    }

    Serial.printf("Wi-Fi credentials saved for SSID '%s'. Rebooting...\n", newSsid.c_str());
    server.send(200, "text/plain", "Wi-Fi credentials saved! Rebooting...");
    delay(500);
    ESP.restart();
  });

  server.on("/settings", HTTP_GET, []() {
    if (!checkAuth()) return;
    String json = "{";
    json += "\"kb_prs_min\":" + String(keyPressMinMs) + ",";
    json += "\"kb_prs_max\":" + String(keyPressMaxMs) + ",";
    json += "\"kb_dly_min\":" + String(keyDelayMinMs) + ",";
    json += "\"kb_dly_max\":" + String(keyDelayMaxMs) + ",";
    json += "\"enabled\":" + String(jiggleEnabled ? "true" : "false") + ",";
    json += "\"int_min\":" + String(jiggleIntervalMinSec) + ",";
    json += "\"int_max\":" + String(jiggleIntervalMaxSec) + ",";
    json += "\"shift_min\":" + String(jiggleShiftMinPx) + ",";
    json += "\"shift_max\":" + String(jiggleShiftMaxPx) + ",";
    json += "\"ret_min\":" + String(jiggleReturnMinMs) + ",";
    json += "\"ret_max\":" + String(jiggleReturnMaxMs) + ",";
    json += "\"sc_enabled\":" + String(scrollEnabled ? "true" : "false") + ",";
    json += "\"sc_int_min\":" + String(scrollIntervalMinSec) + ",";
    json += "\"sc_int_max\":" + String(scrollIntervalMaxSec) + ",";
    json += "\"sc_cnt_min\":" + String(scrollBurstMin) + ",";
    json += "\"sc_cnt_max\":" + String(scrollBurstMax) + ",";
    json += "\"sc_dly_min\":" + String(scrollStepDelayMinMs) + ",";
    json += "\"sc_dly_max\":" + String(scrollStepDelayMaxMs) + ",";
    json += "\"sc_stp_min\":" + String(scrollStepMinTicks) + ",";
    json += "\"sc_stp_max\":" + String(scrollStepMaxTicks) + ",";
    json += "\"sc_ret_min\":" + String(scrollReturnMinSec) + ",";
    json += "\"sc_ret_max\":" + String(scrollReturnMaxSec);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/keyboard", HTTP_POST, []() {
    if (!checkAuth()) return;
    if (server.hasArg("kb_prs_min")) keyPressMinMs = (int)server.arg("kb_prs_min").toInt();
    if (server.hasArg("kb_prs_max")) keyPressMaxMs = (int)server.arg("kb_prs_max").toInt();
    if (server.hasArg("kb_dly_min")) keyDelayMinMs = (int)server.arg("kb_dly_min").toInt();
    if (server.hasArg("kb_dly_max")) keyDelayMaxMs = (int)server.arg("kb_dly_max").toInt();
    clampAllSettings();
    preferences.putInt("kb_prs_min", keyPressMinMs);
    preferences.putInt("kb_prs_max", keyPressMaxMs);
    preferences.putInt("kb_dly_min", keyDelayMinMs);
    preferences.putInt("kb_dly_max", keyDelayMaxMs);
    Serial.printf("Keyboard settings saved: press=%d..%dms, delay=%d..%dms\n", keyPressMinMs, keyPressMaxMs, keyDelayMinMs, keyDelayMaxMs);
    server.send(200, "text/plain", "OK");
  });

  server.on("/settings", HTTP_POST, []() {
    if (!checkAuth()) return;
    if (server.hasArg("enabled")) {
      jiggleEnabled = (server.arg("enabled") == "true" || server.arg("enabled") == "on");
      preferences.putBool("enabled", jiggleEnabled);
    }
    if (server.hasArg("int_min")) jiggleIntervalMinSec = (int)server.arg("int_min").toInt();
    if (server.hasArg("int_max")) jiggleIntervalMaxSec = (int)server.arg("int_max").toInt();
    if (server.hasArg("shift_min")) jiggleShiftMinPx = (int)server.arg("shift_min").toInt();
    if (server.hasArg("shift_max")) jiggleShiftMaxPx = (int)server.arg("shift_max").toInt();
    if (server.hasArg("ret_min")) jiggleReturnMinMs = (int)server.arg("ret_min").toInt();
    if (server.hasArg("ret_max")) jiggleReturnMaxMs = (int)server.arg("ret_max").toInt();
    clampAllSettings();
    preferences.putInt("int_min", jiggleIntervalMinSec);
    preferences.putInt("int_max", jiggleIntervalMaxSec);
    preferences.putInt("shift_min", jiggleShiftMinPx);
    preferences.putInt("shift_max", jiggleShiftMaxPx);
    preferences.putInt("ret_min", jiggleReturnMinMs);
    preferences.putInt("ret_max", jiggleReturnMaxMs);
    
    // Reset jiggler idle timer if not currently shifted
    if (!jiggleShifted) {
      nextJiggleDelay = random(jiggleIntervalMinSec, jiggleIntervalMaxSec + 1) * 1000UL;
      lastJiggleActionTime = millis();
    }
    
    Serial.printf("Jiggler settings saved: enabled=%d, int=%d..%ds, shift=%d..%dpx, ret=%d..%dms\n",
                  jiggleEnabled, jiggleIntervalMinSec, jiggleIntervalMaxSec, jiggleShiftMinPx, jiggleShiftMaxPx, jiggleReturnMinMs, jiggleReturnMaxMs);
    server.send(200, "text/plain", "OK");
  });

  server.on("/scroller", HTTP_POST, []() {
    if (!checkAuth()) return;
    if (server.hasArg("sc_enabled")) {
      scrollEnabled = (server.arg("sc_enabled") == "true" || server.arg("sc_enabled") == "on");
      preferences.putBool("sc_enabled", scrollEnabled);
    }
    if (server.hasArg("sc_int_min")) scrollIntervalMinSec = (int)server.arg("sc_int_min").toInt();
    if (server.hasArg("sc_int_max")) scrollIntervalMaxSec = (int)server.arg("sc_int_max").toInt();
    if (server.hasArg("sc_cnt_min")) scrollBurstMin = (int)server.arg("sc_cnt_min").toInt();
    if (server.hasArg("sc_cnt_max")) scrollBurstMax = (int)server.arg("sc_cnt_max").toInt();
    if (server.hasArg("sc_dly_min")) scrollStepDelayMinMs = (int)server.arg("sc_dly_min").toInt();
    if (server.hasArg("sc_dly_max")) scrollStepDelayMaxMs = (int)server.arg("sc_dly_max").toInt();
    if (server.hasArg("sc_stp_min")) scrollStepMinTicks = (int)server.arg("sc_stp_min").toInt();
    if (server.hasArg("sc_stp_max")) scrollStepMaxTicks = (int)server.arg("sc_stp_max").toInt();
    if (server.hasArg("sc_ret_min")) scrollReturnMinSec = (int)server.arg("sc_ret_min").toInt();
    if (server.hasArg("sc_ret_max")) scrollReturnMaxSec = (int)server.arg("sc_ret_max").toInt();
    clampAllSettings();
    preferences.putInt("sc_int_min", scrollIntervalMinSec);
    preferences.putInt("sc_int_max", scrollIntervalMaxSec);
    preferences.putInt("sc_cnt_min", scrollBurstMin);
    preferences.putInt("sc_cnt_max", scrollBurstMax);
    preferences.putInt("sc_dly_min", scrollStepDelayMinMs);
    preferences.putInt("sc_dly_max", scrollStepDelayMaxMs);
    preferences.putInt("sc_stp_min", scrollStepMinTicks);
    preferences.putInt("sc_stp_max", scrollStepMaxTicks);
    preferences.putInt("sc_ret_min", scrollReturnMinSec);
    preferences.putInt("sc_ret_max", scrollReturnMaxSec);

    // Immediately stop any active scroll cycle and reset idle timer
    scrollState = SCROLL_IDLE;
    scrollAccumulatedTicks = 0;
    nextScrollDelay = random(scrollIntervalMinSec, scrollIntervalMaxSec + 1) * 1000UL;
    lastScrollActionTime = millis();

    Serial.printf("Scroller settings saved: enabled=%d, int=%d..%ds, burst=%d..%d, dly=%d..%dms, step=%d..%dticks, ret=%d..%ds\n",
                  scrollEnabled, scrollIntervalMinSec, scrollIntervalMaxSec, scrollBurstMin, scrollBurstMax,
                  scrollStepDelayMinMs, scrollStepDelayMaxMs, scrollStepMinTicks, scrollStepMaxTicks, scrollReturnMinSec, scrollReturnMaxSec);
    server.send(200, "text/plain", "OK");
  });

  server.on("/sleep", HTTP_POST, []() {
    if (!checkAuth()) return;
    String action = server.hasArg("action") ? server.arg("action") : "start";
    if (action == "stop") {
      sleepTimerActive = false;
      sleepTimerExpired = false;
      lastJiggleActionTime = millis();
      lastScrollActionTime = millis();
      Serial.println("Sleep timer disabled");
      server.send(200, "text/plain", "OK");
    } else {
      float hours = server.hasArg("hours") ? server.arg("hours").toFloat() : 8.0f;
      if (isnan(hours) || isinf(hours) || hours <= 0.0f) {
        server.send(400, "text/plain", "Invalid hours value");
        return;
      }
      if (hours < 0.01f) hours = 0.01f;
      if (hours > 24.0f) hours = 24.0f;
      sleepTimerDurationMs = (unsigned long)(hours * 3600.0f * 1000.0f);
      if (sleepTimerDurationMs < 1000UL) sleepTimerDurationMs = 1000UL;
      sleepTimerStartMs = millis();
      sleepTimerActive = true;
      sleepTimerExpired = false;
      lastJiggleActionTime = millis();
      lastScrollActionTime = millis();
      Serial.printf("Sleep timer started for %.2f hours (%lu sec)\n", hours, sleepTimerDurationMs / 1000UL);
      server.send(200, "text/plain", "OK");
    }
  });

  server.on("/reset_ble", HTTP_POST, []() {
    if (!checkAuth()) return;
    if (isPrinting) {
      isPrinting = false;
      capsLockActive = false;
      ruLayoutActive = false;
      pendingText = String();
    }
    NimBLEServer* pSrv = NimBLEDevice::getServer();
    if (pSrv && pSrv->getConnectedCount() > 0) {
      std::vector<uint16_t> peers = pSrv->getPeerDevices();
      for (uint16_t h : peers) {
        pSrv->disconnect(h);
      }
      unsigned long t0 = millis();
      while (bleDevice.isConnected() && (millis() - t0 < 300)) {
        delay(10);
      }
    }
    NimBLEDevice::deleteAllBonds();
    bleBondLocked = false;
    bleSessionReady = false;
    bleRejectSent = false;
    if (pSrv) {
      pSrv->getAdvertising()->start();
    }
    Serial.println("\nBLE bonds cleared via Web UI (/reset_ble)");
    server.send(200, "text/plain", "BLE pairing cleared. Remove 'Ugreen BT' on your PC and pair again.");
  });

  server.on("/reset_nvs", HTTP_POST, []() {
    if (!checkAuth()) return;
    Serial.println("\nWeb UI /reset_nvs -> full NVS reset");
    server.send(200, "text/plain", "NVS Erased. Rebooting...");
    eraseAllAndReboot();
  });

  // Redirect all unknown requests to the root page (/) for Captive Portal support in AP mode
  server.onNotFound([]() {
    if (apMode) {
      server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "Not found");
    }
  });

  server.begin();
  Serial.println("HTTP running, awaiting commands");
}

void loop() {
  if (apMode) {
    dnsServer.processNextRequest();
  }
  server.handleClient();
  updateBleSecurity();
  handleBootButton();

  bool bleConnected = isBleReady();
  if (bleConnected != lastBleConnected) {
    lastBleConnected = bleConnected;
    Serial.println(bleConnected ? "\nBLE client connected" : "\nBLE client disconnected");
  }

  // Asynchronous (non-blocking) text typing
  if (isPrinting && bleConnected) {
    if (millis() - lastCharTime >= currentDelay) {
      if (printIndex >= pendingText.length()) {
        if (capsLockActive) {
          toggleCapsLock(true);
          lastCharTime = millis();
          currentDelay = random(keyDelayMinMs, keyDelayMaxMs + 1);
        } else if (ruLayoutActive) {
          toggleHostLayout(true, false);
          lastCharTime = millis();
          currentDelay = 150;
        } else {
          isPrinting = false;
          pendingText = String();
          Serial.println(DEBUG_LOG_KEYS ? "\nPrinting finished" : "Printing finished");
        }
      } else {
        int curIdx = printIndex;
        char c = 0;
        bool isCyrillic = false;
        bool cyrillicUpper = false;
        bool validToken = decodeNextPrintToken(pendingText, printIndex, c, isCyrillic, cyrillicUpper);
        bool logChar = (DEBUG_LOG_KEYS && curIdx < SERIAL_LOG_MAX_CHARS);
#if DEBUG_LOG_KEYS
        if (curIdx == SERIAL_LOG_MAX_CHARS) {
          Serial.print("... [truncated]");
        }
#endif

        // Strict whitelist: only printable ASCII (32..126, \n, \t) and Russian Cyrillic letters (А..Я, а..я, Ё, ё)
        if (validToken) {
          bool needLayoutSwitch = (isCyrillic && !ruLayoutActive) ||
                                  (!isCyrillic && ruLayoutActive && !isLayoutNeutralChar(c));
          if (needLayoutSwitch) {
            // Keep printIndex at curIdx so the character is typed on the next tick after the 150ms layout switch delay
            printIndex = curIdx;
            toggleHostLayout(logChar, false);
            lastCharTime = millis();
            currentDelay = 150;
          } else {
            if (isCyrillic) {
              if (cyrillicUpper) {
                if (logChar) Serial.printf("[RU:S+%c]", c);
                Keyboard.press(KEY_LEFT_SHIFT);
                delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
                Keyboard.press(c);
                delay(random(keyPressMinMs, keyPressMaxMs + 1));
                Keyboard.releaseAll();
              } else {
                if (logChar) Serial.printf("[RU:%c]", c);
                Keyboard.press(c);
                delay(random(keyPressMinMs, keyPressMaxMs + 1));
                Keyboard.releaseAll();
              }
            } else if (isUpperLetter(c)) {
              if (!capsLockActive && shouldEnableCapsLock(pendingText, curIdx, nextCapsCheckIndex)) {
                toggleCapsLock(logChar);
                delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
              }
              if (capsLockActive) {
                char lowerKey = c - 'A' + 'a';
                if (logChar) Serial.print(lowerKey);
                Keyboard.press(lowerKey);
                delay(random(keyPressMinMs, keyPressMaxMs + 1));
                Keyboard.releaseAll();
              } else {
                if (logChar) Serial.printf("[S+%c]", c);
                Keyboard.press(KEY_LEFT_SHIFT);
                delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
                Keyboard.press(c);
                delay(random(keyPressMinMs, keyPressMaxMs + 1));
                Keyboard.releaseAll();
              }
            } else if (isLowerLetter(c) || isShiftedSymbol(c)) {
              if (capsLockActive) {
                toggleCapsLock(logChar);
                delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
              }
              if (isShiftedSymbol(c)) {
                if (logChar) Serial.printf("[S+%c]", c);
                Keyboard.press(KEY_LEFT_SHIFT);
                delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
                Keyboard.press(c);
                delay(random(keyPressMinMs, keyPressMaxMs + 1));
                Keyboard.releaseAll();
              } else {
                if (logChar) Serial.print(c);
                Keyboard.press(c);
                delay(random(keyPressMinMs, keyPressMaxMs + 1));
                Keyboard.releaseAll();
              }
            } else {
              // Neutral characters (digits, spaces, punctuation not requiring Shift)
              if (logChar) Serial.print(c);
              Keyboard.press(c);
              delay(random(keyPressMinMs, keyPressMaxMs + 1));
              Keyboard.releaseAll();
            }
            lastCharTime = millis();
            currentDelay = random(keyDelayMinMs, keyDelayMaxMs + 1);
          }
        }

        if (printIndex >= pendingText.length() && !capsLockActive && !ruLayoutActive) {
          isPrinting = false;
          pendingText = String();
          Serial.println(DEBUG_LOG_KEYS ? "\nPrinting finished" : "Printing finished");
        }
      }
    }
  }
  
  // Abort typing if BLE disconnects mid-stream
  if (isPrinting && !bleConnected) {
    isPrinting = false;
    capsLockActive = false;
    ruLayoutActive = false;
    pendingText = String();
    Serial.println("\nBLE disconnected, print aborted");
  }

  // Reset jiggler and scroller states/timers when BLE is disconnected
  if (!bleConnected) {
    jiggleShifted = false;
    lastJiggleActionTime = millis();
    scrollState = SCROLL_IDLE;
    scrollAccumulatedTicks = 0;
    lastScrollActionTime = millis();
  }

  // Check RAM-only Sleep Timer (affects only Mouse Jiggler and Scroller, never Keyboard typing)
  if (sleepTimerActive && !sleepTimerExpired && (millis() - sleepTimerStartMs >= sleepTimerDurationMs)) {
    sleepTimerExpired = true;
    scrollState = SCROLL_IDLE;
    scrollAccumulatedTicks = 0;
    Serial.println("\nSleep timer expired: Jiggler and Scroller paused");
  }

  // Keep idle timers fresh while actively typing text so Jiggler/Scroller count their interval from the end of typing
  if (isPrinting) {
    lastJiggleActionTime = millis();
    lastScrollActionTime = millis();
  }

  // Non-blocking Mouse Jiggler (completely inactive while typing text or when sleep timer has expired, except returning a shifted cursor)
  if (((jiggleEnabled && !sleepTimerExpired) || jiggleShifted) && bleConnected && !isPrinting) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastJiggleActionTime >= nextJiggleDelay) {
      if (!jiggleShifted) {
        // Shift mouse cursor
        currentJiggleX = random(jiggleShiftMinPx, jiggleShiftMaxPx + 1) * (random(2) == 0 ? 1 : -1);
        currentJiggleY = random(jiggleShiftMinPx, jiggleShiftMaxPx + 1) * (random(2) == 0 ? 1 : -1);
        
        Mouse.move(currentJiggleX, currentJiggleY);
        jiggleShifted = true;
        
        // Set delay before returning cursor
        nextJiggleDelay = random(jiggleReturnMinMs, jiggleReturnMaxMs + 1);
        lastJiggleActionTime = currentMillis;
      } else {
        // Return mouse cursor to original position
        Mouse.move(-currentJiggleX, -currentJiggleY);
        jiggleShifted = false;
        Serial.printf("Jiggle: (%+d, %+d) px\n", currentJiggleX, currentJiggleY);
        
        // Set interval until next random movement
        nextJiggleDelay = random(jiggleIntervalMinSec, jiggleIntervalMaxSec + 1) * 1000UL;
        lastJiggleActionTime = currentMillis;
      }
    }
  }

  // Independent Non-blocking Mouse Scroller (completely inactive while typing text or when sleep timer has expired)
  if (scrollEnabled && !sleepTimerExpired && bleConnected && !isPrinting) {
    unsigned long currentMillis = millis();
    if (currentMillis - lastScrollActionTime >= nextScrollDelay) {
      switch (scrollState) {
        case SCROLL_IDLE: {
          scrollDirection = (random(2) == 0) ? 1 : -1;
          scrollStepsRemaining = random(scrollBurstMin, scrollBurstMax + 1);
          scrollAccumulatedTicks = 0;
          scrollState = SCROLL_BURST_OUT;
          nextScrollDelay = 0;
          lastScrollActionTime = currentMillis;
          Serial.printf("Scroll: starting burst (%d steps, dir=%s)\n",
                        scrollStepsRemaining, scrollDirection > 0 ? "UP" : "DOWN");
          break;
        }

        case SCROLL_BURST_OUT: {
          int ticks = constrain((int)random(scrollStepMinTicks, scrollStepMaxTicks + 1), 1, 127);
          Mouse.move(0, 0, (signed char)(ticks * scrollDirection));
          scrollAccumulatedTicks += ticks;
          scrollStepsRemaining--;
          lastScrollActionTime = currentMillis;

          if (scrollStepsRemaining > 0) {
            nextScrollDelay = random(scrollStepDelayMinMs, scrollStepDelayMaxMs + 1);
          } else {
            scrollState = SCROLL_WAIT_RETURN;
            nextScrollDelay = random(scrollReturnMinSec * 1000UL, scrollReturnMaxSec * 1000UL + 1);
            Serial.printf("Scroll: burst complete (%d ticks), waiting %lu ms before return\n",
                          scrollAccumulatedTicks, nextScrollDelay);
          }
          break;
        }

        case SCROLL_WAIT_RETURN: {
          scrollState = SCROLL_BURST_BACK;
          nextScrollDelay = 0;
          lastScrollActionTime = currentMillis;
          break;
        }

        case SCROLL_BURST_BACK: {
          int ticks = constrain((int)random(scrollStepMinTicks, scrollStepMaxTicks + 1), 1, 127);
          if (ticks > scrollAccumulatedTicks) {
            ticks = scrollAccumulatedTicks;
          }
          Mouse.move(0, 0, (signed char)(-ticks * scrollDirection));
          scrollAccumulatedTicks -= ticks;
          lastScrollActionTime = currentMillis;

          if (scrollAccumulatedTicks > 0) {
            nextScrollDelay = random(scrollStepDelayMinMs, scrollStepDelayMaxMs + 1);
          } else {
            scrollState = SCROLL_IDLE;
            nextScrollDelay = random(scrollIntervalMinSec, scrollIntervalMaxSec + 1) * 1000UL;
            Serial.println("Scroll: returned to initial position");
          }
          break;
        }
      }
    }
  }
}
