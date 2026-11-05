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
* To get the current IP address and Access Point password: pair the board via Bluetooth ("Ugreen BT") with the target computer, focus any text input field (e.g., Notepad), and short-press the BOOT button on the board:
  - In AP setup mode, it will type:
    Please connect to wifi access point vkeya1b2 with password xxxxxxxxxxxx, then navigate to captive portal or manually open http://192.168.4.1/wifi
  - When connected to a configured home Wi-Fi network, it will type:
    Please connect via configured wifi network to http://192.168.1.xxx
* Connect to the "vkeyxxxx" Wi-Fi network from a phone or PC using that password — a Captive Portal will open (or navigate to http://192.168.4.1/wifi), select your home Wi-Fi network, enter the password, and click Save Wi-Fi & Reboot.
* Once connected to your home Wi-Fi, the web interface is accessible at http://vkey.local or via the board's IP address (shown in your router's DHCP leases as vkeyxxxx).
* Onboard BOOT button (GPIO 9):
  - Short press — types connection instructions (AP SSID/password/URL in AP mode, or the DHCP IP URL when connected to home Wi-Fi) via BLE keyboard.
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
#define SERIAL_LOG_MAX_CHARS 256
#define CAPS_LOOKAHEAD_WINDOW 16
#define CAPS_MIN_UPPER_RUN 5

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

String myHostname = "vkey";
String apPass = "";
String savedSsid = "";
String savedPass = "";
String cachedScanJson = "[]";
bool scanCacheValid = false;
bool apMode = false;
bool lastBleConnected = false;

// BOOT button state
bool bootBtnPressed = false;
unsigned long bootBtnPressTime = 0;

// Variables for asynchronous (non-blocking) text typing
String pendingText = "";
int printIndex = 0;
bool isPrinting = false;
bool capsLockActive = false;
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
  #status { font-weight: bold; color: #333; margin-top: 10px; margin-bottom: 10px; }
  #netInfo { font-size: 14px; color: #555; margin-bottom: 15px; padding: 10px; background: #f5f5f5; border-radius: 6px; line-height: 1.6; }
  .ap-banner { background: #fff3cd; border: 1px solid #ffeeba; color: #856404; padding: 12px; border-radius: 6px; margin-bottom: 15px; display: none; }
  .nav-link { font-weight: bold; color: #0056b3; text-decoration: none; }
  .nav-link:hover { text-decoration: underline; }
</style>
</head>
<body>
  <h2>ESP32 BLE KeyBoard</h2>
  <div id="apBanner" class="ap-banner">
    <strong>Wi-Fi Setup Mode (Access Point)</strong><br>
    The device is not connected to a home router. <a href="/wifi" class="nav-link">Open Wi-Fi &amp; Network Settings &rarr;</a>
  </div>
  <div id="netInfo">Loading device info...</div>
  <div id="status">Status: Ready</div>

  <form id="typeForm" action="/type" method="POST">
    <textarea name="payload" id="payloadText" rows="12" placeholder="Enter text..."></textarea><br><br>
    <input type="submit" id="printBtn" class="btn" value="Print">
    <button type="button" id="cancelBtn" class="btn" onclick="fetch('/cancel').then(()=>updateStatus())">Cancel</button>
  </form>

  <br><hr><br>
  <h3>Keyboard Settings</h3>
  <form id="keyboardForm">
    Min Key Press Hold (ms): <input type="number" name="kb_prs_min" id="set_kb_prs_min" min="1" required style="width:60px;">
    Max Key Press Hold (ms): <input type="number" name="kb_prs_max" id="set_kb_prs_max" min="1" required style="width:60px;"><br><br>
    Min Delay Between Keys (ms): <input type="number" name="kb_dly_min" id="set_kb_dly_min" min="1" required style="width:60px;">
    Max Delay Between Keys (ms): <input type="number" name="kb_dly_max" id="set_kb_dly_max" min="1" required style="width:60px;"><br><br>
    <button type="submit" class="btn">Save Keyboard Settings</button>
  </form>

  <br><hr><br>
  <h3>Jiggler Settings</h3>
  <form id="settingsForm">
    <label><input type="checkbox" name="enabled" id="set_enabled"> Enable Jiggler</label><br><br>
    Min Interval (sec): <input type="number" name="int_min" id="set_int_min" min="1" required style="width:60px;">
    Max Interval (sec): <input type="number" name="int_max" id="set_int_max" min="1" required style="width:60px;"><br><br>
    Min Shift (px): <input type="number" name="shift_min" id="set_shift_min" min="1" max="127" required style="width:60px;">
    Max Shift (px): <input type="number" name="shift_max" id="set_shift_max" min="1" max="127" required style="width:60px;"><br><br>
    Min Return (ms): <input type="number" name="ret_min" id="set_ret_min" min="1" required style="width:60px;">
    Max Return (ms): <input type="number" name="ret_max" id="set_ret_max" min="1" required style="width:60px;"><br><br>
    <button type="submit" class="btn">Save Jiggler Settings</button>
  </form>

  <br><hr><br>
  <h3>Scroller Settings</h3>
  <form id="scrollerForm">
    <label><input type="checkbox" name="sc_enabled" id="set_sc_enabled"> Enable Random Scroller</label><br><br>
    Min Interval (sec): <input type="number" name="sc_int_min" id="set_sc_int_min" min="1" required style="width:60px;">
    Max Interval (sec): <input type="number" name="sc_int_max" id="set_sc_int_max" min="1" required style="width:60px;"><br><br>
    Min Burst Steps: <input type="number" name="sc_cnt_min" id="set_sc_cnt_min" min="1" required style="width:60px;">
    Max Burst Steps: <input type="number" name="sc_cnt_max" id="set_sc_cnt_max" min="1" required style="width:60px;"><br><br>
    Min Step Delay (ms): <input type="number" name="sc_dly_min" id="set_sc_dly_min" min="10" required style="width:60px;">
    Max Step Delay (ms): <input type="number" name="sc_dly_max" id="set_sc_dly_max" min="10" required style="width:60px;"><br><br>
    Min Step (ticks, 1 &approx; 100px): <input type="number" name="sc_stp_min" id="set_sc_stp_min" min="1" max="127" required style="width:60px;">
    Max Step (ticks): <input type="number" name="sc_stp_max" id="set_sc_stp_max" min="1" max="127" required style="width:60px;"><br><br>
    Min Return Delay (sec): <input type="number" name="sc_ret_min" id="set_sc_ret_min" min="1" required style="width:60px;">
    Max Return Delay (sec): <input type="number" name="sc_ret_max" id="set_sc_ret_max" min="1" required style="width:60px;"><br><br>
    <button type="submit" class="btn">Save Scroller Settings</button>
  </form>

  <br><hr><br>
  <button type="button" class="btn" style="background-color: #ffcccc;" onclick="if(confirm('Erase all settings (Wi-Fi, Jiggler, BLE bonds) and Reboot?')) fetch('/reset_nvs');">Reset NVS (Full Reset)</button>

  <script>
    const statusDiv = document.getElementById('status');
    const printBtn = document.getElementById('printBtn');
    const netInfoDiv = document.getElementById('netInfo');
    const apBanner = document.getElementById('apBanner');

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

        if (data.ap_mode) {
          apBanner.style.display = 'block';
          if (data.ssid) {
            apBanner.innerHTML = `<strong>Wi-Fi Fallback Mode (Access Point)</strong><br>Failed to connect to saved network <b>"${escHtml(data.ssid)}"</b>. <a href="/wifi" class="nav-link">Open Wi-Fi &amp; Network Settings &rarr;</a>`;
          } else {
            apBanner.innerHTML = `<strong>Wi-Fi Setup Mode (Access Point)</strong><br>The device is not connected to a home router. <a href="/wifi" class="nav-link">Open Wi-Fi &amp; Network Settings &rarr;</a>`;
          }
        } else {
          apBanner.style.display = 'none';
        }
        let freeKb = (data.free_heap / 1024).toFixed(1);
        let maxBlockKb = (data.max_alloc / 1024).toFixed(1);
        netInfoDiv.innerHTML = `Free RAM: <b>${freeKb} KB</b> (Max contiguous block: <b>${maxBlockKb} KB</b>) &nbsp;|&nbsp; <a href="/wifi" class="nav-link">Wi-Fi &amp; Network Settings &rarr;</a>`;
      }).catch(() => {});
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
        document.getElementById('set_kb_prs_min').value = d.kb_prs_min;
        document.getElementById('set_kb_prs_max').value = d.kb_prs_max;
        document.getElementById('set_kb_dly_min').value = d.kb_dly_min;
        document.getElementById('set_kb_dly_max').value = d.kb_dly_max;

        document.getElementById('set_enabled').checked = d.enabled;
        document.getElementById('set_int_min').value = d.int_min;
        document.getElementById('set_int_max').value = d.int_max;
        document.getElementById('set_shift_min').value = d.shift_min;
        document.getElementById('set_shift_max').value = d.shift_max;
        document.getElementById('set_ret_min').value = d.ret_min;
        document.getElementById('set_ret_max').value = d.ret_max;

        document.getElementById('set_sc_enabled').checked = d.sc_enabled;
        document.getElementById('set_sc_int_min').value = d.sc_int_min;
        document.getElementById('set_sc_int_max').value = d.sc_int_max;
        document.getElementById('set_sc_cnt_min').value = d.sc_cnt_min;
        document.getElementById('set_sc_cnt_max').value = d.sc_cnt_max;
        document.getElementById('set_sc_dly_min').value = d.sc_dly_min;
        document.getElementById('set_sc_dly_max').value = d.sc_dly_max;
        document.getElementById('set_sc_stp_min').value = d.sc_stp_min;
        document.getElementById('set_sc_stp_max').value = d.sc_stp_max;
        document.getElementById('set_sc_ret_min').value = d.sc_ret_min;
        document.getElementById('set_sc_ret_max').value = d.sc_ret_max;
      });
    }

    document.getElementById('keyboardForm').onsubmit = function(e) {
      e.preventDefault();
      let data = new URLSearchParams(new FormData(e.target));
      fetch('/keyboard', { method: 'POST', body: data })
        .then(() => { loadSettings(); alert('Keyboard settings saved!'); });
    };

    document.getElementById('settingsForm').onsubmit = function(e) {
      e.preventDefault();
      let data = new URLSearchParams(new FormData(e.target));
      if (!document.getElementById('set_enabled').checked) data.append('enabled', 'false');
      else data.set('enabled', 'true');

      fetch('/settings', { method: 'POST', body: data })
        .then(() => { loadSettings(); alert('Jiggler settings saved!'); });
    };

    document.getElementById('scrollerForm').onsubmit = function(e) {
      e.preventDefault();
      let data = new URLSearchParams(new FormData(e.target));
      if (!document.getElementById('set_sc_enabled').checked) data.append('sc_enabled', 'false');
      else data.set('sc_enabled', 'true');

      fetch('/scroller', { method: 'POST', body: data })
        .then(() => { loadSettings(); alert('Scroller settings saved!'); });
    };

    loadSettings();
    setInterval(updateStatus, 1000); // Poll status every second
    updateStatus();
  </script>
</body></html>
)=====";

const char wifiPage[] PROGMEM = R"=====(
<!DOCTYPE html><html><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Wi-Fi &amp; Network Settings - ESP32 BLE KeyBoard</title>
<style>
  body { font-family: sans-serif; padding: 20px; max-width: 600px; margin: auto; color: #222; }
  input[type="text"], input[type="password"], select { width: 100%; padding: 8px; box-sizing: border-box; margin-top: 4px; margin-bottom: 12px; }
  .btn { padding: 12px 24px; font-size: 16px; font-weight: bold; cursor: pointer; margin-right: 10px; margin-bottom: 10px; }
  .btn-sm { padding: 8px 16px; font-size: 14px; cursor: pointer; margin-bottom: 10px; }
  .card { border: 1px solid #ddd; border-radius: 8px; padding: 16px; margin-bottom: 20px; background: #fafafa; }
  .card h3 { margin-top: 0; margin-bottom: 8px; }
  .card-desc { font-size: 13px; color: #666; margin-top: 0; margin-bottom: 12px; }
  .info-row { margin-bottom: 8px; font-size: 14px; }
  .info-label { color: #555; display: inline-block; min-width: 190px; }
  .badge-ap { display: inline-block; padding: 4px 10px; border-radius: 4px; background: #fff3cd; border: 1px solid #ffeeba; color: #856404; font-weight: bold; }
  .badge-sta { display: inline-block; padding: 4px 10px; border-radius: 4px; background: #d4edda; border: 1px solid #c3e6cb; color: #155724; font-weight: bold; }
  .badge-err { display: inline-block; padding: 4px 10px; border-radius: 4px; background: #f8d7da; border: 1px solid #f5c6cb; color: #721c24; font-weight: bold; }
  .nav-link { font-weight: bold; color: #0056b3; text-decoration: none; display: inline-block; margin-bottom: 15px; }
  .nav-link:hover { text-decoration: underline; }
  code { background: #eee; padding: 2px 6px; border-radius: 4px; font-size: 14px; }
</style>
</head>
<body>
  <a href="/" class="nav-link">&larr; Back to Main Page (Keyboard &amp; Mouse)</a>
  <h2>Wi-Fi &amp; Network Settings</h2>

  <div class="card">
    <h3>1. Current Network Status</h3>
    <p class="card-desc">Shows how the ESP32 is currently connected and reachable right now.</p>
    <div class="info-row"><span class="info-label">Current Operating Mode:</span> <span id="curMode">Loading...</span></div>
    <div class="info-row"><span class="info-label">Active Network (SSID):</span> <b id="curNetwork">-</b></div>
    <div class="info-row"><span class="info-label">Current IP Address:</span> <code id="curIp">-</code></div>
    <div class="info-row"><span class="info-label">mDNS Address:</span> <code>http://vkey.local</code></div>
    <div class="info-row"><span class="info-label">Device Hostname:</span> <code id="curHostname">-</code></div>
  </div>

  <div class="card">
    <h3>2. Connect to Home Wi-Fi Router (Client Mode)</h3>
    <p class="card-desc">Select your external Wi-Fi router below so the ESP32 connects to your local network on boot.</p>
    <div class="info-row"><span class="info-label">Saved Home Wi-Fi SSID:</span> <b id="savedSsidText">-</b></div>
    <div class="info-row"><span class="info-label">Home Wi-Fi Status:</span> <span id="homeWifiStatus">-</span></div>
    <hr style="border:0; border-top:1px solid #ddd; margin: 12px 0;">
    <form id="wifiForm">
      <button type="button" id="scanBtn" class="btn-sm" onclick="scanWifi(true)">Rescan Wi-Fi Networks</button>
      <label for="ssidSelect">Available Wi-Fi Networks:</label>
      <select id="ssidSelect" onchange="if(this.value) document.getElementById('wifi_ssid').value=this.value;">
        <option value="">-- Scanning networks... --</option>
      </select>
      <label for="wifi_ssid">Home Wi-Fi SSID (Network Name):</label>
      <input type="text" name="ssid" id="wifi_ssid" placeholder="Enter your home Wi-Fi SSID" autocorrect="off" autocapitalize="none" spellcheck="false" required>
      <label for="wifi_pass">Home Wi-Fi Password:</label>
      <input type="password" name="pass" id="wifi_pass" placeholder="Enter your home Wi-Fi password (leave empty if open)" autocorrect="off" autocapitalize="none" spellcheck="false">
      <button type="submit" class="btn">Save Wi-Fi &amp; Reboot</button>
    </form>
  </div>

  <div class="card">
    <h3>3. ESP32 Built-in Access Point (Fallback Hotspot)</h3>
    <p class="card-desc">These are the credentials of the ESP32's own built-in Wi-Fi hotspot. It turns on automatically only when no home Wi-Fi is configured or when your home router is unreachable.</p>
    <div class="info-row"><span class="info-label">Built-in Hotspot State:</span> <b id="apState">-</b></div>
    <div class="info-row"><span class="info-label">ESP32 AP Name (SSID):</span> <code id="apSsid">-</code></div>
    <div class="info-row"><span class="info-label">ESP32 AP Password (WPA2):</span> <code id="apPass">-</code></div>
    <div class="info-row"><span class="info-label">ESP32 AP Default IP:</span> <code>http://192.168.4.1</code></div>
  </div>

  <a href="/" class="nav-link">&larr; Back to Main Page (Keyboard &amp; Mouse)</a>

  <script>
    function loadNetworkInfo() {
      fetch('/status').then(r => r.json()).then(data => {
        const curMode = document.getElementById('curMode');
        const curNetwork = document.getElementById('curNetwork');
        const apState = document.getElementById('apState');
        const homeWifiStatus = document.getElementById('homeWifiStatus');

        if (data.ap_mode) {
          curNetwork.textContent = data.hostname + ' (ESP32 Built-in Hotspot)';
          if (data.ssid) {
            curMode.innerHTML = '<span class="badge-ap">ESP32 Access Point (Fallback Mode)</span>';
            homeWifiStatus.innerHTML = '<span class="badge-err">Failed to connect (network unreachable or wrong password)</span>';
            apState.textContent = 'Active now (fallback because Home Wi-Fi connection failed)';
          } else {
            curMode.innerHTML = '<span class="badge-ap">ESP32 Access Point (Initial Setup Mode)</span>';
            homeWifiStatus.textContent = 'Not configured yet';
            apState.textContent = 'Active now (waiting for Home Wi-Fi setup)';
          }
        } else {
          curMode.innerHTML = '<span class="badge-sta">Connected to Home Wi-Fi (Client Mode)</span>';
          curNetwork.textContent = data.ssid;
          homeWifiStatus.innerHTML = '<span class="badge-sta">Connected</span>';
          apState.textContent = 'Standby (inactive while connected to Home Wi-Fi)';
        }

        document.getElementById('curIp').textContent = 'http://' + data.ip;
        document.getElementById('curHostname').textContent = data.hostname;
        document.getElementById('savedSsidText').textContent = data.ssid ? data.ssid : 'Not configured';
        document.getElementById('apSsid').textContent = data.hostname;
        document.getElementById('apPass').textContent = data.ap_pass;

        if (data.ssid) {
          document.getElementById('wifi_ssid').value = data.ssid;
        }
      }).catch(() => {});
    }

    function scanWifi(refresh) {
      const scanBtn = document.getElementById('scanBtn');
      const select = document.getElementById('ssidSelect');
      scanBtn.disabled = true;
      scanBtn.textContent = 'Scanning...';
      if (refresh) {
        select.innerHTML = '<option value="">-- Scanning networks... --</option>';
      }
      const url = refresh ? '/scan?refresh=1' : '/scan';
      fetch(url).then(r => r.json()).then(list => {
        select.innerHTML = '<option value="">-- Select a scanned Wi-Fi network --</option>';
        list.forEach(item => {
          const opt = document.createElement('option');
          opt.value = item.ssid;
          opt.textContent = `${item.ssid} (${item.rssi} dBm)${item.secure ? ' 🔒' : ''}`;
          select.appendChild(opt);
        });
        scanBtn.disabled = false;
        scanBtn.textContent = 'Rescan Wi-Fi Networks';
      }).catch(() => {
        select.innerHTML = '<option value="">-- Scan failed, enter SSID manually --</option>';
        scanBtn.disabled = false;
        scanBtn.textContent = 'Rescan Wi-Fi Networks';
      });
    }

    document.getElementById('wifiForm').onsubmit = function(e) {
      e.preventDefault();
      let data = new URLSearchParams(new FormData(e.target));
      fetch('/wifi', { method: 'POST', body: data })
        .then(r => r.text())
        .then(msg => alert(msg));
    };

    loadNetworkInfo();
    scanWifi(false);
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

bool isUpperLetter(char c) {
  return (c >= 'A' && c <= 'Z');
}

bool isLowerLetter(char c) {
  return (c >= 'a' && c <= 'z');
}

bool isShiftedSymbol(char c) {
  // Special characters that require Shift on a US English keyboard layout
  const char* shifted = "~!@#$%^&*()_+{}|:\"<>?";
  for (int i = 0; shifted[i] != '\0'; i++) {
    if (c == shifted[i]) return true;
  }
  return false;
}

bool shouldEnableCapsLock(const String& text, int curIdx) {
  if (curIdx < nextCapsCheckIndex) return false;

  int endIdx = min((int)text.length(), curIdx + CAPS_LOOKAHEAD_WINDOW);
  int upperCount = 0;

  for (int i = curIdx; i < endIdx; i++) {
    char ch = text[i];
    if (isUpperLetter(ch)) {
      upperCount++;
      if (upperCount >= CAPS_MIN_UPPER_RUN) {
        return true;
      }
    } else if (isLowerLetter(ch) || isShiftedSymbol(ch)) {
      // Stopper reached before accumulating 5 uppercase letters; skip re-checking up to this index
      nextCapsCheckIndex = i + 1;
      return false;
    }
  }

  // Since text[curIdx] is uppercase and leaves the window on the next step, at least
  // (CAPS_MIN_UPPER_RUN + 1 - upperCount) new characters must enter the window to reach 5.
  nextCapsCheckIndex = (endIdx >= (int)text.length())
                         ? (int)text.length()
                         : (curIdx + max(1, CAPS_MIN_UPPER_RUN + 1 - upperCount));
  return false;
}

void toggleCapsLock(bool logToSerial) {
  Keyboard.press(KEY_CAPS_LOCK);
  delay(random(keyPressMinMs, keyPressMaxMs + 1));
  Keyboard.releaseAll();
  capsLockActive = !capsLockActive;
  if (logToSerial) {
    Serial.print(capsLockActive ? "[CAPS:ON]" : "[CAPS:OFF]");
  }
}

void eraseAllAndReboot() {
  if (bleDevice.isConnected()) {
    if (capsLockActive) {
      delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
      toggleCapsLock(true);
    }
    Keyboard.releaseAll();
  }
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
  capsLockActive = false;
  nextCapsCheckIndex = 0;
  currentDelay = random(keyDelayMinMs, keyDelayMaxMs + 1);
  lastCharTime = millis();

  Serial.printf("Print started (%u chars): ", (unsigned int)text.length());
  if (text.length() > SERIAL_LOG_MAX_CHARS) {
    Serial.print(text.substring(0, SERIAL_LOG_MAX_CHARS));
    Serial.println("... [truncated]");
  } else {
    Serial.println(text);
  }
  Serial.print("Keys: ");
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
      if (heldMs >= BOOT_LONG_PRESS_MS) {
        // Long press released after a blocking operation
        Serial.println("\nBOOT long press (3s) -> full NVS reset");
        eraseAllAndReboot();
      } else if (heldMs >= 50) {
        // Short press on BOOT button — type human-readable connection instructions
        if (!allowShortPress) {
          Serial.println("\nBOOT short press -> ignored (Wi-Fi connecting, IP not ready yet)");
        } else if (!bleDevice.isConnected()) {
          Serial.println("BOOT short press -> ignored (BLE not connected)");
        } else if (isPrinting) {
          Serial.println("\nBOOT short press -> ignored (already printing)");
        } else {
          String infoStr;
          if (apMode) {
            infoStr = "Please connect to wifi access point " + myHostname +
                      " with password " + apPass +
                      ", then navigate to captive portal or manually open http://" +
                      WiFi.softAPIP().toString() + "/wifi";
          } else if (WiFi.status() == WL_CONNECTED) {
            infoStr = "Please connect via configured wifi network to http://" +
                      WiFi.localIP().toString();
          } else {
            infoStr = "Wi-Fi network " + savedSsid +
                      " is currently disconnected (reconnecting). Check router or reboot device to start Access Point";
          }
          Serial.println("BOOT short press -> typing connection info");
          startPrintText(infoStr);
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
  cachedScanJson = json;
  scanCacheValid = true;
  Serial.printf("Wi-Fi scan complete: %d networks found\n", max(0, n));
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

  // 2. Now switch to pure AP mode and start the Access Point
  WiFi.mode(WIFI_AP);
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
  bleDevice.setBatteryLevel(96);
  Keyboard.begin();
  Mouse.begin();

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
    if (capsLockActive && bleDevice.isConnected()) {
      delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
      toggleCapsLock(true);
    }
    capsLockActive = false;
    isPrinting = false;
    pendingText = String();
    server.send(200, "text/plain", "Cancelled");
    Serial.println("\nPrint cancelled");
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
    if (server.hasArg("refresh") || !scanCacheValid) {
      performWifiScan();
    }
    server.send(200, "application/json", cachedScanJson);
  });

  server.on("/wifi", HTTP_GET, []() {
    server.send_P(200, "text/html", wifiPage);
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

    if (bleDevice.isConnected()) {
      if (capsLockActive) {
        delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
        toggleCapsLock(true);
      }
      Keyboard.releaseAll();
    }

    Serial.printf("Wi-Fi credentials saved for SSID '%s'. Rebooting...\n", newSsid.c_str());
    server.send(200, "text/plain", "Wi-Fi credentials saved! Rebooting...");
    delay(500);
    ESP.restart();
  });

  server.on("/settings", HTTP_GET, []() {
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
    if (server.hasArg("kb_prs_min")) { keyPressMinMs = max(1, (int)server.arg("kb_prs_min").toInt()); preferences.putInt("kb_prs_min", keyPressMinMs); }
    if (server.hasArg("kb_prs_max")) { keyPressMaxMs = max(keyPressMinMs, (int)server.arg("kb_prs_max").toInt()); preferences.putInt("kb_prs_max", keyPressMaxMs); }
    if (server.hasArg("kb_dly_min")) { keyDelayMinMs = max(1, (int)server.arg("kb_dly_min").toInt()); preferences.putInt("kb_dly_min", keyDelayMinMs); }
    if (server.hasArg("kb_dly_max")) { keyDelayMaxMs = max(keyDelayMinMs, (int)server.arg("kb_dly_max").toInt()); preferences.putInt("kb_dly_max", keyDelayMaxMs); }
    Serial.printf("Keyboard settings saved: press=%d..%dms, delay=%d..%dms\n", keyPressMinMs, keyPressMaxMs, keyDelayMinMs, keyDelayMaxMs);
    server.send(200, "text/plain", "OK");
  });

  server.on("/settings", HTTP_POST, []() {
    if (server.hasArg("enabled")) {
      jiggleEnabled = (server.arg("enabled") == "true" || server.arg("enabled") == "on");
      preferences.putBool("enabled", jiggleEnabled);
    }
    if (server.hasArg("int_min")) { jiggleIntervalMinSec = max(1, (int)server.arg("int_min").toInt()); preferences.putInt("int_min", jiggleIntervalMinSec); }
    if (server.hasArg("int_max")) { jiggleIntervalMaxSec = max(jiggleIntervalMinSec, (int)server.arg("int_max").toInt()); preferences.putInt("int_max", jiggleIntervalMaxSec); }
    if (server.hasArg("shift_min")) { jiggleShiftMinPx = constrain((int)server.arg("shift_min").toInt(), 1, 127); preferences.putInt("shift_min", jiggleShiftMinPx); }
    if (server.hasArg("shift_max")) { jiggleShiftMaxPx = constrain((int)server.arg("shift_max").toInt(), jiggleShiftMinPx, 127); preferences.putInt("shift_max", jiggleShiftMaxPx); }
    if (server.hasArg("ret_min")) { jiggleReturnMinMs = max(1, (int)server.arg("ret_min").toInt()); preferences.putInt("ret_min", jiggleReturnMinMs); }
    if (server.hasArg("ret_max")) { jiggleReturnMaxMs = max(jiggleReturnMinMs, (int)server.arg("ret_max").toInt()); preferences.putInt("ret_max", jiggleReturnMaxMs); }
    
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
    if (server.hasArg("sc_enabled")) {
      scrollEnabled = (server.arg("sc_enabled") == "true" || server.arg("sc_enabled") == "on");
      preferences.putBool("sc_enabled", scrollEnabled);
    }
    if (server.hasArg("sc_int_min")) { scrollIntervalMinSec = max(1, (int)server.arg("sc_int_min").toInt()); preferences.putInt("sc_int_min", scrollIntervalMinSec); }
    if (server.hasArg("sc_int_max")) { scrollIntervalMaxSec = max(scrollIntervalMinSec, (int)server.arg("sc_int_max").toInt()); preferences.putInt("sc_int_max", scrollIntervalMaxSec); }
    if (server.hasArg("sc_cnt_min")) { scrollBurstMin = max(1, (int)server.arg("sc_cnt_min").toInt()); preferences.putInt("sc_cnt_min", scrollBurstMin); }
    if (server.hasArg("sc_cnt_max")) { scrollBurstMax = max(scrollBurstMin, (int)server.arg("sc_cnt_max").toInt()); preferences.putInt("sc_cnt_max", scrollBurstMax); }
    if (server.hasArg("sc_dly_min")) { scrollStepDelayMinMs = max(10, (int)server.arg("sc_dly_min").toInt()); preferences.putInt("sc_dly_min", scrollStepDelayMinMs); }
    if (server.hasArg("sc_dly_max")) { scrollStepDelayMaxMs = max(scrollStepDelayMinMs, (int)server.arg("sc_dly_max").toInt()); preferences.putInt("sc_dly_max", scrollStepDelayMaxMs); }
    if (server.hasArg("sc_stp_min")) { scrollStepMinTicks = constrain((int)server.arg("sc_stp_min").toInt(), 1, 127); preferences.putInt("sc_stp_min", scrollStepMinTicks); }
    if (server.hasArg("sc_stp_max")) { scrollStepMaxTicks = constrain((int)server.arg("sc_stp_max").toInt(), scrollStepMinTicks, 127); preferences.putInt("sc_stp_max", scrollStepMaxTicks); }
    if (server.hasArg("sc_ret_min")) { scrollReturnMinSec = max(1, (int)server.arg("sc_ret_min").toInt()); preferences.putInt("sc_ret_min", scrollReturnMinSec); }
    if (server.hasArg("sc_ret_max")) { scrollReturnMaxSec = max(scrollReturnMinSec, (int)server.arg("sc_ret_max").toInt()); preferences.putInt("sc_ret_max", scrollReturnMaxSec); }

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

  server.on("/reset_nvs", []() {
    Serial.println("\nWeb UI /reset_nvs -> full NVS reset");
    server.send(200, "text/plain", "NVS Erased. Rebooting...");
    eraseAllAndReboot();
  });

  // Redirect all unknown requests to the Wi-Fi setup page (/wifi) for Captive Portal support in AP mode
  server.onNotFound([]() {
    if (apMode) {
      server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/wifi", true);
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

  bool bleConnected = bleDevice.isConnected();
  if (bleConnected != lastBleConnected) {
    lastBleConnected = bleConnected;
    Serial.println(bleConnected ? "\nBLE client connected" : "\nBLE client disconnected");
  }

  // Asynchronous (non-blocking) text typing
  if (isPrinting && bleConnected) {
    if (millis() - lastCharTime >= currentDelay) {
      int curIdx = printIndex;
      char c = pendingText[printIndex];
      printIndex++;
      bool logChar = (curIdx < SERIAL_LOG_MAX_CHARS);
      if (curIdx == SERIAL_LOG_MAX_CHARS) {
        Serial.print("... [truncated]");
      }

      // Filter out non-ASCII UTF-8 characters (keep only ASCII 0-127)
      if ((unsigned char)c < 128) {
        // Ignore \r to prevent double line breaks
        if (c != '\r') {
          if (isUpperLetter(c)) {
            if (!capsLockActive && shouldEnableCapsLock(pendingText, curIdx)) {
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
      
      if (printIndex >= pendingText.length()) {
        if (capsLockActive) {
          delay(random(keyDelayMinMs, keyDelayMaxMs + 1));
          toggleCapsLock(true);
        }
        isPrinting = false;
        pendingText = String();
        Serial.println("\nPrinting finished");
      }
    }
  }
  
  // Abort typing if BLE disconnects mid-stream
  if (isPrinting && !bleDevice.isConnected()) {
    isPrinting = false;
    capsLockActive = false;
    pendingText = String();
    Serial.println("\nBLE disconnected, print aborted");
  }

  // Reset jiggler and scroller states/timers when BLE is disconnected
  if (!bleDevice.isConnected()) {
    jiggleShifted = false;
    lastJiggleActionTime = millis();
    scrollState = SCROLL_IDLE;
    scrollAccumulatedTicks = 0;
    lastScrollActionTime = millis();
  }

  // Keep idle timers fresh while actively typing text so Jiggler/Scroller count their interval from the end of typing
  if (isPrinting) {
    if (!jiggleShifted) lastJiggleActionTime = millis();
    if (scrollState == SCROLL_IDLE) lastScrollActionTime = millis();
  }

  // Non-blocking Mouse Jiggler (paused while actively typing text, except returning a shifted cursor)
  if ((jiggleEnabled || jiggleShifted) && bleDevice.isConnected() && (!isPrinting || jiggleShifted)) {
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

  // Independent Non-blocking Mouse Scroller (paused while actively typing text, except finishing an active cycle)
  if (scrollEnabled && bleDevice.isConnected() && (!isPrinting || scrollState != SCROLL_IDLE)) {
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
