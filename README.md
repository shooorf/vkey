# vkey — ESP32 BLE HID Keyboard & Mouse Jiggler with Web UI

**vkey** is an ESP32-based firmware (designed for **ESP32-C3 Super Mini** and compatible with other ESP32 boards) that turns the microcontroller into a **Bluetooth Low Energy (BLE) HID Keyboard + Mouse** controlled over **Wi-Fi** via a built-in web interface.

It is designed for pasting text and scripts into machines where clipboard sharing is restricted or unavailable (including **RDP sessions**, KVMs, and isolated hosts) and keeping the host awake with a configurable **Mouse Jiggler**.

---

## Features

* **Remote Text & Script Typing (with RDP Compatibility):**
  * Paste text or scripts into the web UI and type them out on the paired host over BLE.
  * Uses raw `text/plain` HTTP payloads (no `urlencoded` size bloat or escaping issues) with explicit heap buffer reclamation (`String()`).
  * Non-blocking character-by-character typing with two configurable randomized timing ranges (saved in NVS): key press hold duration (`30–40 ms` default, how long each key is held down) and delay between keys (`60–80 ms` default, pause between characters as well as between pressing `Shift` and the target key) so RDP sessions never drop uppercase characters or shifted symbols.
  * **Smart `Caps Lock` Optimization:** When more than 4 uppercase letters (`>= 5`) are detected within a 16-character lookahead window (before any lowercase letter or shifted symbol), the firmware toggles `KEY_CAPS_LOCK` on, types the uppercase run without pressing `Shift` on each letter, keeps `Caps Lock` active across neutral characters (digits, spaces, punctuation), and automatically turns `Caps Lock` off before the next lowercase letter, shifted symbol, cancellation, or end of text.
  * **Strict Printable ASCII Whitelist:** Only printable ASCII characters (`32..126`), newline (`\n`), and tab (`\t`) are allowed to be typed (`isAllowedPrintChar()`), filtering out all control codes (`0..31` and `127`, such as `Backspace 0x08` or `Escape 0x1B`).
  * Real-time progress bar (`%` and character counter) and instant cancellation (`Cancel`).
* **Configurable Non-Blocking Mouse Jiggler & Independent Mouse Scroller:**
  * **Mouse Jiggler:** Periodically shifts the mouse cursor by a small random offset (default `2–5 px` every `80–120 s`), waits `20–40 ms`, and returns the cursor to its exact starting position.
  * **Mouse Scroller (Disabled by default):** Independently simulates realistic human wheel scrolling bursts (default `3–5` steps in one direction with `60–80 ms` between steps, `1–2` wheel ticks ≈ `100–200 px` per step), waits `2–5 s`, and smoothly scrolls back by the exact accumulated amount to return the page to its original position. Disabling the Scroller immediately stops all scrolling operations.
  * **RAM-Only Sleep Timer:** Optional countdown timer (default `8 hours`, stored in RAM only) that automatically pauses both Mouse Jiggler and Mouse Scroller after the specified work shift expires without modifying NVS settings or interrupting keyboard typing. Can be restarted or disabled anytime from the web UI with a live `HH:MM:SS` countdown.
  * Completely pauses all mouse and scroll activity while text typing is in progress.
  * All parameters for Keyboard, Jiggler, and Scroller are validated, clamped on the server, synced back to the web UI on save, and persisted in NVS flash (`Preferences`).
* **Secure Zero-Touch Wi-Fi Provisioning (WPA3-Only SoftAP + Captive Portal) & Hardening:**
  * No Wi-Fi credentials are hardcoded in the firmware.
  * Unique per-device hostname and Access Point SSID based on the chip's MAC address: `vkeyxxxx` (last 4 hex digits of MAC).
  * **WPA3-Only Hotspot with Single-Client Lock (`max_connection = 1`):** Built-in SoftAP runs in strict **WPA3-Personal (`WIFI_AUTH_WPA3_PSK`, SAE + Protected Management Frames `802.11w`)** mode and limits simultaneous connections to `1` client, preventing offline handshake/PMKID dictionary attacks, Wi-Fi deauthentication attacks, and unauthorized parallel connections.
  * Deterministic per-board 10-character WPA3 password derived via `SHA-256` from internal factory-burned silicon IDs (`eFuse Block 2` 128-bit Unique ID, `eFuse Block 1` wafer data, and `64-bit SPI Flash Unique ID`) using an unambiguous lowercase alphanumeric character set (`abcdefghjkmnpqrstuvwxyz23456789`). These internal hardware IDs are never broadcast over radio and remain constant even after `Reset NVS` or a full flash erase.
  * **HTTP Digest Auth + SHA-256 Body Integrity + CSRF Token:**
    * When connected to a home/office Wi-Fi network (`STA Mode`), all routes require RFC 2617 **HTTP Digest Authentication** (username `admin`, password `<apPass>`) using ESP32 Core 3.3.x stateless `HMAC-SHA256` nonces (allowing simultaneous sessions on PC and smartphone without login loops or cleartext password transmission). In `AP Mode` (already protected by `WPA3-SAE` and `max_connection = 1`), Digest Auth is bypassed so smartphone Captive Portals open immediately.
    * Every `POST` request includes a 128-bit session `X-VKey-Token` header (blocking cross-origin CSRF) and appends a 128-bit `SHA-256` hash of the request payload (`?h=<sha256_hex>`) to the URL. On the ESP32-C3, the payload hash is verified via the hardware SHA-256 accelerator (`mbedtls_sha256`) and explicitly matched against the signed `uri="..."` attribute inside the `Authorization: Digest` header, preventing both payload tampering and cross-endpoint Digest URI replay.
  * **No Password Exposure over HTTP:** The AP/Web UI password (`apPass`) is never transmitted in `/status` JSON or displayed in the `/wifi` web UI (`Press BOOT button` is shown instead); obtaining the password requires physical access to the board's `BOOT` button.
  * **Reduced BLE Radio Footprint (`0 dBm`):** Constant BLE transmit power is set to `0 dBm` (`1 mW`, 8× lower than the `+9 dBm` default via `BLEDevice::setPower(0)`), reliably penetrating desks at the workstation while limiting long-range BLE visibility across rooms and reducing antenna heating.
  * **POST-Only State Mutations:** All state-changing and radio-blocking endpoints (`/type`, `/cancel`, `/wifi`, `/keyboard`, `/settings`, `/scroller`, `/sleep`, `/reset_nvs`, and live Wi-Fi rescan `POST /scan`) require `HTTP_POST`, preventing cross-site `GET` (`<img>` tag) resets or Wi-Fi scan DoS.
  * Pre-scans surrounding Wi-Fi networks in pure `WIFI_STA` mode before launching `WIFI_AP` so smartphones (including Android / Xiaomi Captive Portals) never drop the hotspot connection due to channel hopping when opening the setup page.
  * Dedicated `/wifi` setup page organized into 3 clear cards: **1. Current Network Status**, **2. Connect to Home Wi-Fi Router (Client Mode)** (showing connection failure status when in fallback AP mode), and **3. ESP32 Built-in Access Point (Fallback Hotspot)**. The Captive Portal opens the main page (`/`) directly so you can immediately use the keyboard/mouse in AP mode or click the top banner link to configure Wi-Fi (`/wifi`).
  * Accessible on the local network via mDNS at **`http://vkey.local`** or by IP address, with automatic Wi-Fi reconnection (`WiFi.setAutoReconnect(true)`) if the home router restarts.
* **Hardware `BOOT` Button Controls:**
  * **Short press (`50 ms – 3 s`):** Types human-readable connection instructions directly into the active text field on the BLE-connected computer:
    * In AP mode: `Please connect to wifi access point vkeyxxxx with password <password>, then navigate to captive portal or manually open http://192.168.4.1`
    * In home Wi-Fi mode (connected): `Please connect via configured wifi network to http://<dhcp-ip> (login: admin, password: <password>)`
    * In home Wi-Fi mode (disconnected/reconnecting): `Wi-Fi network <ssid> is currently disconnected (reconnecting). Check router or reboot device to start Access Point`
  * **Long press (hold for `3 seconds` after power-on):** Full factory reset — safely releases keys / turns off `Caps Lock` if active, erases all NVS data (Wi-Fi credentials, Keyboard/Jiggler/Scroller settings, and BLE bonds), and reboots.
* **Live Memory Diagnostics & Privacy-Safe Serial Logging:**
  * Displays real-time Free Heap RAM and Maximum Contiguous Allocatable Block in the web UI header.
  * Logs lifecycle events, BLE connect/disconnect transitions, Wi-Fi scans, settings updates, Jiggler/Scroller actions, and anonymized typing statistics (`Print started (N chars)`, `Printing finished`) to `Serial` (`115200 baud`). By default (`#define DEBUG_LOG_KEYS 0`), typed characters and `apPass` are **never** echoed to the USB Serial port to prevent local processes on the USB host from snooping keystrokes.

---

## Usage Guide

### 1. Initial Setup (First Boot)
1. Power on the ESP32 board (via USB charger or PC USB port).
2. On your target computer, open Bluetooth settings, scan for new devices, and pair with **`Ugreen BT`**.
3. Open any plain text editor (e.g., Notepad) on the target computer, ensure the **US English** keyboard layout is active, and place the cursor in the editor.
4. **Short-press the physical `BOOT` button** on the ESP32 board. In Access Point mode, it will type out:
   ```text
   Please connect to wifi access point vkeya1b2 with password k9m4x7p2q8, then navigate to captive portal or manually open http://192.168.4.1
   ```
   *(When already connected to your home Wi-Fi, a short press on `BOOT` will instead type: `Please connect via configured wifi network to http://192.168.1.xxx (login: admin, password: k9m4x7p2q8)`)*
5. Connect your phone or PC to the Wi-Fi network **`vkeya1b2`** using the printed password.
6. A Captive Portal window will open automatically to the main page (`http://192.168.4.1/`), where you can immediately use the keyboard/mouse in AP mode or click **Wi-Fi Settings →** (`/wifi`) in the top banner to connect the board to your home router.
7. In the **Connect to Home Wi-Fi Router** card, select your home Wi-Fi network from the scanned dropdown (or type the SSID manually), enter your Wi-Fi password, and click **Save Wi-Fi & Reboot**.

### 2. Daily Operation
* Once connected to your home router, open **`http://vkey.local`** in your browser (or use the IP address shown in your router's DHCP leases for `vkeyxxxx` — you can also short-press `BOOT` anytime to type out the new local IP).
* **Typing Text / Scripts:**
  * Make sure the target machine's active keyboard layout is **English (US)** and the cursor is focused in a plain text input field (avoid smart auto-closing/auto-indenting code editors like VS Code or Jupyter when typing raw code).
  * Paste your text into the text area and click **Print**.
* **Changing Wi-Fi Networks:**
  * Click **Wi-Fi & Network Settings →** (`/wifi`) at the top of the main page to view detailed network parameters or connect to a different Wi-Fi network.
  * If the saved Wi-Fi network is unreachable for 15 seconds on boot (e.g., you moved the device to a new location), the board automatically falls back to its WPA3-Only Access Point (`vkeyxxxx`).
* **Factory Reset (`Reset NVS`):**
  * Hold the onboard **`BOOT` button for 3 seconds** (while the device is already powered on), or click **Reset NVS (Full Reset)** at the bottom of the web UI.
  * *Note:* Do not hold `BOOT` (`GPIO 9`) *while* plugging in the USB cable, as `GPIO 9` is the ESP32-C3 hardware strapping pin for ROM bootloader download mode.

---

## Build Environment & Flashing (Arduino IDE)

### Hardware & Board Settings
* **Board:** `ESP32-C3 Super Mini` (or any ESP32 / ESP32-S3 / ESP32-C6 board with BLE support)
* **Arduino IDE:** `2.3.x`
* **ESP32 Arduino Core:** `esp32 by Espressif Systems` version **`3.3.8`** (Board Manager URL: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`)

#### Required Arduino IDE `Tools` Menu Configuration
Make sure to change the 3 highlighted settings from their defaults before compiling and uploading:

| Setting | Required Value | Why It Matters |
| :--- | :--- | :--- |
| **Board** | `"ESP32C3 Dev Module"` | Target microcontroller profile |
| **USB CDC On Boot** | **`"Enabled"`** *(default is Disabled)* | **Required:** ESP32-C3 Super Mini uses native USB Serial/JTAG (`GPIO 18`/`19`) with no external UART chip. Without this, `Serial Monitor` will not work and subsequent uploads may fail to auto-reset. |
| **Flash Mode** | **`"DIO"`** *(default is QIO)* | **Required:** Most ESP32-C3 Super Mini boards fail to boot (infinite bootloop) if flashed in `QIO` mode. |
| **Partition Scheme** | **`"Huge APP (3MB No OTA/1MB SPIFFS)"`** | **Required:** The combined Wi-Fi + WebServer + DNS + mDNS + NimBLE stack exceeds the default `1.2MB` app partition limit. |
| **CPU Frequency** | `"160MHz (WiFi)"` | Default |
| **Flash Frequency** | `"80MHz"` | Default |
| **Flash Size** | `"4MB (32Mb)"` | Default |
| **Upload Speed** | `"921600"` | Default |

> **Tip if the board is not detected or fails to upload on first flash:**  
> Hold down the **`BOOT`** button (`GPIO 9`), press and release the **`RST`** button (or plug in the USB cable while holding `BOOT`), then release **`BOOT`** to force the ESP32-C3 into ROM bootloader download mode. After flashing once with `USB CDC On Boot: "Enabled"`, future uploads will reset automatically.

### Dependencies
1. **`NimBLE-Arduino`** by `h2zero` (version **`2.5.0`**) — install directly from the Arduino IDE Library Manager.
2. **`ESP32_BLE_HID_Combo`** — **embedded locally** in `src/ESP32_BLE_HID_Combo/`. Do **not** install the outdated version from the Arduino Library Manager.

> **Important Note for Arduino IDE:**  
> Arduino IDE requires the sketch folder name to match the `.ino` filename. Either rename the project folder to `vkey` (so the path is `vkey/vkey.ino`), or rename `vkey.ino` to match your folder name before opening it in Arduino IDE.

---

## Technical Details: BLE Stack & `ESP32_BLE_HID_Combo` Modernization

### Why `NimBLE-Arduino`?
The standard ESP32 Bluedroid BLE stack consumes a large amount of Flash and SRAM, which is critical on the ESP32-C3 when running Wi-Fi (`WebServer`), `DNSServer`, `mDNS`, and BLE HID simultaneously. `NimBLE-Arduino` reduces BLE RAM usage by ~100 KB and Flash usage by ~50%.

### Why the Original `ESP32_BLE_HID_Combo (0.3.1)` Was Broken
The upstream `ESP32-BLE-Combo` library (last updated in 2022) fails to compile and operate properly on modern ESP32 environments due to three compounding issues:
1. **Broken Header Include Order in `BleCombo.cpp`:**
   In the original `BleCombo.cpp`, `#if defined(USE_NIMBLE)` was evaluated on line 1 *before* `#include "BleCombo.h"` on line 17. Because Arduino IDE compiles library `.cpp` files as separate translation units, defining `USE_NIMBLE` in `BleCombo.h` (or in the `.ino` sketch) had no effect on lines 1–16 of `BleCombo.cpp`, causing it to include the conflicting Bluedroid `<BLEDevice.h>` headers alongside NimBLE headers.
2. **ESP32 Arduino Core 3.x Breaking Changes:**
   ESP32 Core 3.x migrated built-in BLE APIs from `std::string` to Arduino `String` and deprecated legacy ESP-IDF headers such as `<driver/adc.h>`.
3. **`NimBLE-Arduino` 1.x to 2.x Breaking API Changes:**
   Original `BleCombo` targeted `NimBLE 1.x`. In `NimBLE 2.x` (`2.5.0`), the API was overhauled:
   * `NimBLEHIDDevice` methods were renamed to explicit getters/setters (`inputReport()` -> `getInputReport()`, `outputReport()` -> `getOutputReport()`, `manufacturer()->setValue()` -> `setManufacturer()`, `pnp()` -> `setPnp()`, `hidInfo()` -> `setHidInfo()`, `reportMap()` -> `setReportMap()`, `hidService()` -> `getHidService()`).
   * Server and Characteristic callback signatures (`onConnect`, `onDisconnect`, `onWrite`) now require `NimBLEConnInfo& connInfo` (and `int reason` for `onDisconnect`).
   * Automatic advertising restart on disconnect (`m_advertiseOnDisconnect`) was changed from `true` in 1.x to **`false`** by default in 2.x.
   * Device name is no longer automatically inserted into BLE Advertisement packets by `BLEDevice::init()` unless `advertising->setName()` is explicitly called.

### Summary of Fixes in `src/ESP32_BLE_HID_Combo`
All fixes have been applied to the embedded library in `src/ESP32_BLE_HID_Combo/`:
* **Forced NimBLE Mode & Include Order:** Defined `#define USE_NIMBLE` at the top of `BleCombo.h` and moved `#include "BleCombo.h"` to line 1 of `BleCombo.cpp`.
* **NimBLE 2.x API & Callbacks:** Updated all `NimBLEHIDDevice` calls and `NimBLEServerCallbacks` / `NimBLECharacteristicCallbacks` virtual method signatures; passed `pServer->setCallbacks(this, false)` to prevent `NimBLEServer` destructor from calling `delete` on the static `bleDevice` instance.
* **Auto-Reconnect After Disconnect:** Added `pServer->advertiseOnDisconnect(true)` in `BleCombo::begin()` so the ESP32 automatically resumes advertising if the host PC sleeps, reboots, or disconnects.
* **Constant `0 dBm` BLE Transmit Power:** Added `BLEDevice::setPower(0)` right after `BLEDevice::init()` in `BleCombo::begin()` to reduce BLE transmit power from `+9 dBm` (`8 mW`) to `0 dBm` (`1 mW`).
* **Advertisement Device Name & 31-Byte Packet Handling:** Added `advertising->setName(deviceName)` in `BleCombo::begin()` so the custom name (`"Ugreen BT"`) is broadcast in the primary advertisement packet instead of showing up as a generic `"Keyboard"`. If a name longer than 18 bytes is configured (exceeding the 31-byte primary advertisement payload), it automatically enables Scan Response (`enableScanResponse(true)`) and places the name there.
* **Fixed `Mouse_::releaseAll()` Bug:** Fixed a bug in the original `Mouse.cpp` where `Mouse_::releaseAll()` mistakenly called `bleDevice.release(MOUSE_ALL_ALL)` (keyboard key release) instead of `bleDevice.releaseMouse(MOUSE_ALL_ALL)`.
* **Removed Deprecated Calls & Added Include Guards:** Removed unused `<driver/adc.h>`, restricted `hid->startServices()` to non-NimBLE builds, and added proper `#ifndef` include guards and relative `"BleCombo.h"` includes in `BleKeyboard.h` and `BleMouse.h`.

### Switching to a Standalone External Library in the Future
The folder `src/ESP32_BLE_HID_Combo/` contains `library.properties` and `keywords.txt` and can be published directly as a standalone Arduino library repository. Once installed in `Documents/Arduino/libraries`, switching `vkey.ino` to the external library only requires deleting `src/ESP32_BLE_HID_Combo/` and toggling two `#include` lines in `vkey.ino`:
```cpp
// #include "src/ESP32_BLE_HID_Combo/BleKeyboard.h"
// #include "src/ESP32_BLE_HID_Combo/BleMouse.h"
#include <BleKeyboard.h>
#include <BleMouse.h>
```

