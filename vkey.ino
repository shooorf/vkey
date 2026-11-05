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
   - Removed deprecated hid->startServices() call (for NimBLE) and unused <driver/adc.h> header
   - Fixed a bug in Mouse.cpp where Mouse_::releaseAll() called release() instead of releaseMouse()
   - Added include guards and relative "BleCombo.h" includes in BleKeyboard.h and BleMouse.h

Usage:
* On first boot (or after an NVS reset), the board generates a random 12-character Access Point password (lowercase letters and digits, excluding ambiguous characters) and stores it in NVS.
* If no home Wi-Fi network is configured or the saved network is unreachable, the board starts a WPA2 Access Point named "vkeyxxxx" (where xxxx are the last 4 hex characters of its MAC address).
* To get the current IP address and Access Point password: pair the board via Bluetooth ("Ugreen BT") with the target computer, focus any text input field (e.g., Notepad), and short-press the BOOT button on the board — it will type out:
  ip 192.168.4.1 ap vkeya1b2 pass xxxxxxxxxxxx
* Connect to the "vkeyxxxx" Wi-Fi network from a phone or PC using that password — a Captive Portal will open (or navigate to http://192.168.4.1), select your home Wi-Fi network, enter the password, and click Save Wi-Fi & Reboot.
* Once connected to your home Wi-Fi, the web interface is accessible at http://vkey.local or via the board's IP address (shown in your router's DHCP leases as vkeyxxxx).
* Onboard BOOT button (GPIO 9):
  - Short press — types the current IP address, AP SSID, and AP password via BLE keyboard.
  - Long press (hold for 3 seconds after power-on) — erases all NVS settings (Wi-Fi credentials, AP password, Jiggler settings, BLE bonds) and reboots.
    (Note: Do not hold BOOT while plugging in USB power, as GPIO 9 is the ESP32-C3 hardware bootloader strapping pin.)
* Properly supports RDP sessions.
* Target machine must use the US English keyboard layout with focus in a plain text input field (non-ASCII characters and smart auto-closing editors like VS Code or Jupyter are not supported).
* For debugging: connect via USB cable to a PC with Arduino IDE, select the board and COM port, and open Serial Monitor.

*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_mac.h>
#include <esp_random.h>
// Local embedded copy of the library (in src/ESP32_BLE_HID_Combo).
// When switching to an external library, comment out these 2 lines and uncomment the next 2:
#include "src/ESP32_BLE_HID_Combo/BleKeyboard.h"
#include "src/ESP32_BLE_HID_Combo/BleMouse.h"
// #include <BleKeyboard.h>
// #include <BleMouse.h>
#include <nvs_flash.h>
#include <Preferences.h>

#ifdef BOOT_BUILTIN
#define BOOT_BUTTON_PIN BOOT_BUILTIN
#else
#define BOOT_BUTTON_PIN 9
#endif
#define BOOT_LONG_PRESS_MS 3000
#define WIFI_CONNECT_TIMEOUT_MS 15000

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

String myHostname = "vkey";
String apPass = "";
String savedSsid = "";
String savedPass = "";
bool apMode = false;

// BOOT button state
bool bootBtnPressed = false;
unsigned long bootBtnPressTime = 0;

// Variables for asynchronous (non-blocking) text typing
String pendingText = "";
int printIndex = 0;
bool isPrinting = false;
unsigned long lastCharTime = 0;
unsigned long currentDelay = 0;

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

// Store HTML in Flash memory using PROGMEM and Raw String Literal (R"=====( ... )=====")
const char htmlPage[] PROGMEM = R"=====(
<!DOCTYPE html><html><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 BLE KeyBoard</title>
<style>
  body { font-family: sans-serif; padding: 20px; max-width: 600px; margin: auto; }
  textarea { width: 100%; padding: 10px; box-sizing: border-box; }
  input[type="text"], input[type="password"], select { width: 100%; padding: 8px; box-sizing: border-box; margin-top: 4px; margin-bottom: 10px; }
  .btn { padding: 12px 24px; font-size: 16px; font-weight: bold; cursor: pointer; margin-right: 10px; margin-bottom: 10px; }
  .btn-sm { padding: 8px 16px; font-size: 14px; cursor: pointer; margin-bottom: 10px; }
  #status { font-weight: bold; color: #333; margin-top: 10px; margin-bottom: 10px; }
  #netInfo { font-size: 14px; color: #555; margin-bottom: 15px; padding: 10px; background: #f5f5f5; border-radius: 6px; }
  .ap-banner { background: #fff3cd; border: 1px solid #ffeeba; color: #856404; padding: 12px; border-radius: 6px; margin-bottom: 15px; display: none; }
</style>
</head>
<body>
  <h2>ESP32 BLE KeyBoard</h2>
  <div id="apBanner" class="ap-banner">
    <strong>Wi-Fi Setup Mode (Access Point)</strong><br>
    Select your home Wi-Fi network in the Wi-Fi Settings section below to connect the device to your router.
  </div>
  <div id="netInfo">Loading device info...</div>
  <div id="status">Status: Ready</div>

  <form id="typeForm" action="/type" method="POST">
    <textarea name="payload" id="payloadText" rows="12" placeholder="Enter text..."></textarea><br><br>
    <input type="submit" id="printBtn" class="btn" value="Print">
    <button type="button" id="cancelBtn" class="btn" onclick="fetch('/cancel').then(()=>updateStatus())">Cancel</button>
  </form>

  <br><hr><br>
  <h3>Jiggler Settings</h3>
  <form id="settingsForm">
    <label><input type="checkbox" name="enabled" id="set_enabled"> Enable Jiggler</label><br><br>
    Min Interval (sec): <input type="number" name="int_min" id="set_int_min" style="width:60px;">
    Max Interval (sec): <input type="number" name="int_max" id="set_int_max" style="width:60px;"><br><br>
    Min Shift (px): <input type="number" name="shift_min" id="set_shift_min" style="width:60px;">
    Max Shift (px): <input type="number" name="shift_max" id="set_shift_max" style="width:60px;"><br><br>
    Min Return (ms): <input type="number" name="ret_min" id="set_ret_min" style="width:60px;">
    Max Return (ms): <input type="number" name="ret_max" id="set_ret_max" style="width:60px;"><br><br>
    <button type="submit" class="btn">Save Settings</button>
  </form>

  <br><hr><br>
  <h3>Wi-Fi Settings</h3>
  <form id="wifiForm">
    <button type="button" id="scanBtn" class="btn-sm" onclick="scanWifi()">Scan Wi-Fi Networks</button>
    <select id="ssidSelect" style="display:none;" onchange="if(this.value) document.getElementById('wifi_ssid').value=this.value;">
      <option value="">-- Select scanned network --</option>
    </select>
    <label>SSID (Network Name):</label>
    <input type="text" name="ssid" id="wifi_ssid" placeholder="Enter Wi-Fi SSID" required>
    <label>Password:</label>
    <input type="password" name="pass" id="wifi_pass" placeholder="Enter Wi-Fi password (leave empty if open)">
    <button type="submit" class="btn">Save Wi-Fi &amp; Reboot</button>
  </form>

  <br><hr><br>
  <button type="button" class="btn" style="background-color: #ffcccc;" onclick="if(confirm('Erase all settings (Wi-Fi, Jiggler, BLE bonds) and Reboot?')) fetch('/reset_nvs');">Reset NVS (Full Reset)</button>

  <script>
    const statusDiv = document.getElementById('status');
    const printBtn = document.getElementById('printBtn');
    const netInfoDiv = document.getElementById('netInfo');
    const apBanner = document.getElementById('apBanner');
    let initialLoadDone = false;

    function escHtml(s) {
      return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
    }

    function updateStatus() {
      fetch('/status').then(r => r.json()).then(data => {
        if (!data.connected) {
          statusDiv.innerHTML = "<span style='color:red;'>BLE Not Connected</span>";
          printBtn.disabled = true;
        } else if (data.printing) {
          let perc = data.total > 0 ? Math.round((data.progress / data.total) * 100) : 0;
          statusDiv.innerHTML = `<span style='color:blue;'>Printing... ${perc}% (${data.progress}/${data.total})</span>`;
          printBtn.disabled = true;
        } else {
          statusDiv.innerHTML = "<span style='color:green;'>Ready to Print</span>";
          printBtn.disabled = false;
        }

        apBanner.style.display = data.ap_mode ? 'block' : 'none';
        let modeText = data.ap_mode ? `AP Mode (${escHtml(data.hostname)})` : `Connected to "${escHtml(data.ssid)}"`;
        let freeKb = (data.free_heap / 1024).toFixed(1);
        let maxBlockKb = (data.max_alloc / 1024).toFixed(1);
        netInfoDiv.innerHTML = `Host/AP: <b>${escHtml(data.hostname)}</b> | AP Pass: <b>${escHtml(data.ap_pass)}</b> | mDNS: <b>http://vkey.local</b> | IP: <b>${escHtml(data.ip)}</b> | Wi-Fi: <b>${modeText}</b><br>Free RAM: <b>${freeKb} KB</b> (Max contiguous block: <b>${maxBlockKb} KB</b>)`;

        if (!initialLoadDone) {
          initialLoadDone = true;
          if (data.ssid) {
            document.getElementById('wifi_ssid').value = data.ssid;
          }
          if (data.ap_mode && !data.printing) {
            scanWifi();
          }
        }
      }).catch(() => {});
    }

    function scanWifi() {
      const scanBtn = document.getElementById('scanBtn');
      const select = document.getElementById('ssidSelect');
      scanBtn.disabled = true;
      scanBtn.textContent = 'Scanning...';
      fetch('/scan').then(r => r.json()).then(list => {
        select.innerHTML = '<option value="">-- Select scanned network --</option>';
        list.forEach(item => {
          const opt = document.createElement('option');
          opt.value = item.ssid;
          opt.textContent = `${item.ssid} (${item.rssi} dBm)${item.secure ? ' 🔒' : ''}`;
          select.appendChild(opt);
        });
        select.style.display = 'block';
        scanBtn.disabled = false;
        scanBtn.textContent = 'Scan Wi-Fi Networks';
      }).catch(() => {
        scanBtn.disabled = false;
        scanBtn.textContent = 'Scan Wi-Fi Networks';
      });
    }

    document.getElementById('typeForm').onsubmit = function(e) {
      e.preventDefault();
      const text = document.getElementById('payloadText').value;
      fetch('/type', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain;charset=UTF-8' },
        body: text
      }).then(() => {
        updateStatus();
      });
    };

    function loadSettings() {
      fetch('/settings').then(r => r.json()).then(d => {
        document.getElementById('set_enabled').checked = d.enabled;
        document.getElementById('set_int_min').value = d.int_min;
        document.getElementById('set_int_max').value = d.int_max;
        document.getElementById('set_shift_min').value = d.shift_min;
        document.getElementById('set_shift_max').value = d.shift_max;
        document.getElementById('set_ret_min').value = d.ret_min;
        document.getElementById('set_ret_max').value = d.ret_max;
      });
    }

    document.getElementById('settingsForm').onsubmit = function(e) {
      e.preventDefault();
      let data = new URLSearchParams(new FormData(e.target));
      if (!document.getElementById('set_enabled').checked) data.append('enabled', 'false');
      else data.set('enabled', 'true');

      fetch('/settings', { method: 'POST', body: data })
        .then(() => alert('Settings saved successfully!'));
    };

    document.getElementById('wifiForm').onsubmit = function(e) {
      e.preventDefault();
      let data = new URLSearchParams(new FormData(e.target));
      fetch('/wifi', { method: 'POST', body: data })
        .then(r => r.text())
        .then(msg => alert(msg));
    };

    loadSettings();
    setInterval(updateStatus, 1000); // Poll status every second
    updateStatus();
  </script>
</body></html>
)=====";

String buildDeviceHostname() {
  uint8_t mac[6] = {0};
  esp_efuse_mac_get_default(mac);
  char buf[16];
  snprintf(buf, sizeof(buf), "vkey%02x%02x", mac[4], mac[5]);
  return String(buf);
}

String generateApPassword() {
  // 12 characters: lowercase Latin letters and digits excluding ambiguous characters (0, o, 1, l, i)
  const char charset[] = "abcdefghjkmnpqrstuvwxyz23456789";
  const size_t charsetLen = sizeof(charset) - 1;
  char buf[13];
  for (int i = 0; i < 12; i++) {
    buf[i] = charset[esp_random() % charsetLen];
  }
  buf[12] = '\0';
  return String(buf);
}

String jsonEscape(const String& s) {
  String out = "";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '\"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if ((unsigned char)c >= 32) out += c;
  }
  return out;
}

void eraseAllAndReboot() {
  Serial.println("\nErasing NVS and rebooting...");
  delay(200);
  nvs_flash_erase();
  nvs_flash_init();
  ESP.restart();
}

void startPrintText(const String& text) {
  if (!bleDevice.isConnected() || isPrinting || text.length() == 0) {
    return;
  }
  pendingText = text;
  printIndex = 0;
  isPrinting = true;
  currentDelay = random(80, 130);
  lastCharTime = millis();
  Serial.println("Print started");
}

void handleBootButton() {
  bool isLow = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  unsigned long now = millis();

  if (isLow) {
    if (!bootBtnPressed) {
      bootBtnPressed = true;
      bootBtnPressTime = now;
    } else if (now - bootBtnPressTime >= BOOT_LONG_PRESS_MS) {
      // Long press on BOOT button — full NVS reset and reboot
      eraseAllAndReboot();
    }
  } else {
    if (bootBtnPressed) {
      unsigned long heldMs = now - bootBtnPressTime;
      bootBtnPressed = false;
      if (heldMs >= 50 && heldMs < BOOT_LONG_PRESS_MS) {
        // Short press on BOOT button — type current IP address, AP SSID, and AP password
        String ipStr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
        String infoStr = "ip " + ipStr + " ap " + myHostname + " pass " + apPass;
        Serial.print("BOOT short press -> typing: ");
        Serial.println(infoStr);
        startPrintText(infoStr);
      }
    }
  }
}

bool needsShift(char c) {
  // Uppercase letters
  if (c >= 'A' && c <= 'Z') return true;
  
  // Special characters that require Shift on a US English keyboard layout
  const char* shifted = "~!@#$%^&*()_+{}|:\"<>?";
  for (int i = 0; shifted[i] != '\0'; i++) {
    if (c == shifted[i]) return true;
  }
  return false;
}

void startAccessPointMode() {
  apMode = true;
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAP(myHostname.c_str(), apPass.c_str());

  IPAddress apIp = WiFi.softAPIP();
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIp);

  Serial.println("\n--- Started Wi-Fi Access Point Mode ---");
  Serial.print("AP SSID: ");
  Serial.println(myHostname);
  Serial.print("AP Pass: ");
  Serial.println(apPass);
  Serial.print("AP IP:   ");
  Serial.println(apIp);
}

void setup() {
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  randomSeed(esp_random());

  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- Run ESP32 BLE Keyboard ---");

  myHostname = buildDeviceHostname();
  Serial.print("Device Hostname / AP SSID: ");
  Serial.println(myHostname);

  // Start BLE immediately on boot so keyboard and BOOT button work even before Wi-Fi connects
  Serial.println("Starting BLE KeyBoard");
  bleDevice.setName("Ugreen BT");
  bleDevice.setBatteryLevel(96);
  Keyboard.begin();
  Mouse.begin();

  // Initialize settings from NVS (Preferences)
  preferences.begin("jiggler", false);
  jiggleEnabled = preferences.getBool("enabled", true);
  jiggleIntervalMinSec = preferences.getInt("int_min", 80);
  jiggleIntervalMaxSec = preferences.getInt("int_max", 120);
  jiggleShiftMinPx = preferences.getInt("shift_min", 2);
  jiggleShiftMaxPx = preferences.getInt("shift_max", 5);
  jiggleReturnMinMs = preferences.getInt("ret_min", 20);
  jiggleReturnMaxMs = preferences.getInt("ret_max", 40);

  savedSsid = preferences.getString("wifi_ssid", "");
  savedPass = preferences.getString("wifi_pass", "");
  apPass = preferences.getString("ap_pass", "");
  if (apPass.length() < 8) {
    apPass = generateApPassword();
    preferences.putString("ap_pass", apPass);
    Serial.print("Generated new AP password: ");
    Serial.println(apPass);
  }

  nextJiggleDelay = random(jiggleIntervalMinSec, jiggleIntervalMaxSec + 1) * 1000UL;
  lastJiggleActionTime = millis();

  // Connect to saved Wi-Fi or fall back to Access Point setup mode
  if (savedSsid.length() > 0) {
    WiFi.setHostname(myHostname.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.begin(savedSsid.c_str(), savedPass.c_str());
    WiFi.setSleep(false);

    Serial.print("Connecting to Wi-Fi: ");
    Serial.print(savedSsid);
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startAttempt < WIFI_CONNECT_TIMEOUT_MS)) {
      handleBootButton();
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

  server.on("/", []() {
    server.send_P(200, "text/html", htmlPage);
  });

  server.on("/type", HTTP_POST, []() {
    if (!bleDevice.isConnected()) {
      server.send(400, "text/plain", "BLE not connected");
      return;
    }
    if (isPrinting) {
      server.send(400, "text/plain", "Already printing");
      return;
    }
    String body = server.hasArg("plain") ? server.arg("plain") : server.arg("payload");
    if (body.length() > 0) {
      startPrintText(body);
      server.send(200, "text/plain", "Started");
    } else {
      server.send(400, "text/plain", "No text");
    }
  });

  server.on("/cancel", []() {
    isPrinting = false;
    pendingText = String();
    server.send(200, "text/plain", "Cancelled");
    Serial.println("Print cancelled");
  });

  server.on("/status", []() {
    String ipStr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    String json = "{";
    json += "\"connected\":" + String(bleDevice.isConnected() ? "true" : "false") + ",";
    json += "\"printing\":" + String(isPrinting ? "true" : "false") + ",";
    json += "\"progress\":" + String(printIndex) + ",";
    json += "\"total\":" + String(pendingText.length()) + ",";
    json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
    json += "\"max_alloc\":" + String(ESP.getMaxAllocHeap()) + ",";
    json += "\"ap_mode\":" + String(apMode ? "true" : "false") + ",";
    json += "\"hostname\":\"" + jsonEscape(myHostname) + "\",";
    json += "\"ap_pass\":\"" + jsonEscape(apPass) + "\",";
    json += "\"ip\":\"" + jsonEscape(ipStr) + "\",";
    json += "\"ssid\":\"" + jsonEscape(savedSsid) + "\"";
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/scan", HTTP_GET, []() {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; i++) {
      if (i > 0) json += ",";
      json += "{";
      json += "\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
      json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
      json += "\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false");
      json += "}";
    }
    json += "]";
    WiFi.scanDelete();
    server.send(200, "application/json", json);
  });

  server.on("/wifi", HTTP_POST, []() {
    if (!server.hasArg("ssid") || server.arg("ssid").length() == 0) {
      server.send(400, "text/plain", "SSID is required");
      return;
    }
    String newSsid = server.arg("ssid");
    String newPass = server.hasArg("pass") ? server.arg("pass") : "";

    preferences.putString("wifi_ssid", newSsid);
    preferences.putString("wifi_pass", newPass);

    server.send(200, "text/plain", "Wi-Fi credentials saved! Rebooting...");
    delay(500);
    ESP.restart();
  });

  server.on("/settings", HTTP_GET, []() {
    String json = "{";
    json += "\"enabled\":" + String(jiggleEnabled ? "true" : "false") + ",";
    json += "\"int_min\":" + String(jiggleIntervalMinSec) + ",";
    json += "\"int_max\":" + String(jiggleIntervalMaxSec) + ",";
    json += "\"shift_min\":" + String(jiggleShiftMinPx) + ",";
    json += "\"shift_max\":" + String(jiggleShiftMaxPx) + ",";
    json += "\"ret_min\":" + String(jiggleReturnMinMs) + ",";
    json += "\"ret_max\":" + String(jiggleReturnMaxMs);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/settings", HTTP_POST, []() {
    if (server.hasArg("enabled")) {
      jiggleEnabled = (server.arg("enabled") == "true" || server.arg("enabled") == "on");
      preferences.putBool("enabled", jiggleEnabled);
    }
    if (server.hasArg("int_min")) { jiggleIntervalMinSec = server.arg("int_min").toInt(); preferences.putInt("int_min", jiggleIntervalMinSec); }
    if (server.hasArg("int_max")) { jiggleIntervalMaxSec = server.arg("int_max").toInt(); preferences.putInt("int_max", jiggleIntervalMaxSec); }
    if (server.hasArg("shift_min")) { jiggleShiftMinPx = server.arg("shift_min").toInt(); preferences.putInt("shift_min", jiggleShiftMinPx); }
    if (server.hasArg("shift_max")) { jiggleShiftMaxPx = server.arg("shift_max").toInt(); preferences.putInt("shift_max", jiggleShiftMaxPx); }
    if (server.hasArg("ret_min")) { jiggleReturnMinMs = server.arg("ret_min").toInt(); preferences.putInt("ret_min", jiggleReturnMinMs); }
    if (server.hasArg("ret_max")) { jiggleReturnMaxMs = server.arg("ret_max").toInt(); preferences.putInt("ret_max", jiggleReturnMaxMs); }
    
    // Reset jiggler timer
    jiggleShifted = false;
    nextJiggleDelay = random(jiggleIntervalMinSec, jiggleIntervalMaxSec + 1) * 1000UL;
    lastJiggleActionTime = millis();
    
    server.send(200, "text/plain", "OK");
  });

  server.on("/reset_nvs", []() {
    server.send(200, "text/plain", "NVS Erased. Rebooting...");
    eraseAllAndReboot();
  });

  // Redirect all unknown requests to the root page for Captive Portal support in AP mode
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
  handleBootButton();

  // Asynchronous (non-blocking) text typing
  if (isPrinting && bleDevice.isConnected()) {
    if (millis() - lastCharTime >= currentDelay) {
      char c = pendingText[printIndex];
      printIndex++;

      // Filter out non-ASCII UTF-8 characters (keep only ASCII 0-127)
      if ((unsigned char)c < 128) {
        // Ignore \r to prevent double line breaks
        if (c != '\r') {
          Serial.print(c);
          if (needsShift(c)) {
            // Emulate human keystrokes for RDP compatibility:
            Keyboard.press(KEY_LEFT_SHIFT); // 1. Press and hold Shift first
            delay(random(60, 80));          // 2. Wait for RDP to register Shift state
            Keyboard.press(c);              // 3. Press the target key
            delay(random(60, 80));          // 4. Hold keys down
            Keyboard.releaseAll();          // 5. Release all keys
          } else {
            // Regular keys without Shift
            Keyboard.press(c);
            delay(random(30, 40));
            Keyboard.releaseAll();
          }
          lastCharTime = millis();
          currentDelay = random(60, 80);
        }
      }
      
      if (printIndex >= pendingText.length()) {
        isPrinting = false;
        pendingText = String();
        Serial.println("\nPrinting finished");
      }
    }
  }
  
  // Abort typing if BLE disconnects mid-stream
  if (isPrinting && !bleDevice.isConnected()) {
    isPrinting = false;
    pendingText = String();
    Serial.println("\nBLE disconnected, print aborted");
  }

  // Non-blocking Mouse Jiggler (paused while actively typing text, except returning a shifted cursor)
  if (jiggleEnabled && bleDevice.isConnected() && (!isPrinting || jiggleShifted)) {
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
        
        // Set interval until next random movement
        nextJiggleDelay = random(jiggleIntervalMinSec, jiggleIntervalMaxSec + 1) * 1000UL;
        lastJiggleActionTime = currentMillis;
      }
    }
  }
}
