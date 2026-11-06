#pragma once
#include <Arduino.h>

// Store HTML in Flash memory using PROGMEM and Raw String Literal (R"=====( ... )=====")
const char htmlPage[] PROGMEM = R"=====(
<!DOCTYPE html><html><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 BLE KeyBoard</title>
<style>
  body { font-family: sans-serif; padding: 14px; max-width: 600px; margin: auto; color: #222; }
  h2 { margin-top: 4px; margin-bottom: 12px; }
  h3 { margin-top: 0; margin-bottom: 12px; }
  textarea { width: 100%; padding: 10px; box-sizing: border-box; font-size: 14px; }
  .btn { padding: 10px 20px; font-size: 15px; font-weight: bold; cursor: pointer; margin-right: 8px; margin-bottom: 6px; }
  #status { font-weight: bold; color: #333; margin-top: 10px; margin-bottom: 10px; }
  #netInfo { font-size: 13px; color: #555; margin-bottom: 12px; padding: 10px 12px; background: #f5f5f5; border-radius: 6px; display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 6px 12px; }
  .ap-banner { background: #fff3cd; border: 1px solid #ffeeba; color: #856404; padding: 10px 12px; border-radius: 6px; margin-bottom: 12px; font-size: 14px; display: none; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 6px 12px; }
  .nav-link { font-weight: bold; color: #0056b3; text-decoration: none; white-space: nowrap; }
  .nav-link:hover { text-decoration: underline; }
  .form-row { display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 6px 10px; margin-bottom: 10px; font-size: 14px; }
  .form-label { color: #333; }
  .form-pair { display: flex; align-items: center; gap: 10px; white-space: nowrap; }
  .form-pair label { display: inline-flex; align-items: center; gap: 4px; color: #555; font-size: 13px; }
  .form-pair input[type="number"] { width: 58px; padding: 5px; box-sizing: border-box; font-size: 14px; text-align: center; }
  .check-row { display: block; font-size: 14px; font-weight: bold; margin-bottom: 12px; cursor: pointer; }
  hr { border: 0; border-top: 1px solid #ccc; margin: 18px 0; }
</style>
</head>
<body>
  <div id="apBanner" class="ap-banner">
    <span>Home Wi-Fi is not configured (AP mode).</span> <a href="/wifi" class="nav-link">Wi-Fi Settings &rarr;</a>
  </div>
  <h2>ESP32 BLE KeyBoard</h2>
  <div id="netInfo">Loading device info...</div>
  <div id="status">Status: Ready</div>

  <form id="typeForm" action="/type" method="POST">
    <textarea name="payload" id="payloadText" rows="10" placeholder="Enter text..."></textarea><br><br>
    <input type="submit" id="printBtn" class="btn" value="Print">
    <button type="button" id="cancelBtn" class="btn" onclick="fetch('/cancel', { method: 'POST' }).then(()=>updateStatus())">Cancel</button>
  </form>

  <hr>
  <h3>Keyboard Settings</h3>
  <form id="keyboardForm">
    <div class="form-row">
      <span class="form-label">Key Press Hold (ms):</span>
      <div class="form-pair">
        <label>Min <input type="number" name="kb_prs_min" id="set_kb_prs_min" min="1" required></label>
        <label>Max <input type="number" name="kb_prs_max" id="set_kb_prs_max" min="1" required></label>
      </div>
    </div>
    <div class="form-row">
      <span class="form-label">Delay Between Keys (ms):</span>
      <div class="form-pair">
        <label>Min <input type="number" name="kb_dly_min" id="set_kb_dly_min" min="1" required></label>
        <label>Max <input type="number" name="kb_dly_max" id="set_kb_dly_max" min="1" required></label>
      </div>
    </div>
    <button type="submit" class="btn">Save Keyboard Settings</button>
  </form>

  <hr>
  <h3>Jiggler Settings</h3>
  <form id="settingsForm">
    <label class="check-row"><input type="checkbox" name="enabled" id="set_enabled"> Enable Jiggler</label>
    <div class="form-row">
      <span class="form-label">Interval (sec):</span>
      <div class="form-pair">
        <label>Min <input type="number" name="int_min" id="set_int_min" min="1" required></label>
        <label>Max <input type="number" name="int_max" id="set_int_max" min="1" required></label>
      </div>
    </div>
    <div class="form-row">
      <span class="form-label">Shift Distance (px):</span>
      <div class="form-pair">
        <label>Min <input type="number" name="shift_min" id="set_shift_min" min="1" max="127" required></label>
        <label>Max <input type="number" name="shift_max" id="set_shift_max" min="1" max="127" required></label>
      </div>
    </div>
    <div class="form-row">
      <span class="form-label">Return Delay (ms):</span>
      <div class="form-pair">
        <label>Min <input type="number" name="ret_min" id="set_ret_min" min="1" required></label>
        <label>Max <input type="number" name="ret_max" id="set_ret_max" min="1" required></label>
      </div>
    </div>
    <button type="submit" class="btn">Save Jiggler Settings</button>
  </form>

  <hr>
  <h3>Scroller Settings</h3>
  <form id="scrollerForm">
    <label class="check-row"><input type="checkbox" name="sc_enabled" id="set_sc_enabled"> Enable Random Scroller</label>
    <div class="form-row">
      <span class="form-label">Interval (sec):</span>
      <div class="form-pair">
        <label>Min <input type="number" name="sc_int_min" id="set_sc_int_min" min="1" required></label>
        <label>Max <input type="number" name="sc_int_max" id="set_sc_int_max" min="1" required></label>
      </div>
    </div>
    <div class="form-row">
      <span class="form-label">Burst Steps:</span>
      <div class="form-pair">
        <label>Min <input type="number" name="sc_cnt_min" id="set_sc_cnt_min" min="1" required></label>
        <label>Max <input type="number" name="sc_cnt_max" id="set_sc_cnt_max" min="1" required></label>
      </div>
    </div>
    <div class="form-row">
      <span class="form-label">Step Delay (ms):</span>
      <div class="form-pair">
        <label>Min <input type="number" name="sc_dly_min" id="set_sc_dly_min" min="10" required></label>
        <label>Max <input type="number" name="sc_dly_max" id="set_sc_dly_max" min="10" required></label>
      </div>
    </div>
    <div class="form-row">
      <span class="form-label">Step (ticks, 1 &approx; 100px):</span>
      <div class="form-pair">
        <label>Min <input type="number" name="sc_stp_min" id="set_sc_stp_min" min="1" max="127" required></label>
        <label>Max <input type="number" name="sc_stp_max" id="set_sc_stp_max" min="1" max="127" required></label>
      </div>
    </div>
    <div class="form-row">
      <span class="form-label">Return Delay (sec):</span>
      <div class="form-pair">
        <label>Min <input type="number" name="sc_ret_min" id="set_sc_ret_min" min="1" required></label>
        <label>Max <input type="number" name="sc_ret_max" id="set_sc_ret_max" min="1" required></label>
      </div>
    </div>
    <button type="submit" class="btn">Save Scroller Settings</button>
  </form>

  <hr>
  <h3>Sleep Timer (RAM Only)</h3>
  <div class="form-row">
    <span class="form-label">Stop Mouse &amp; Scroller after (hours):</span>
    <div class="form-pair">
      <input type="number" id="sleep_hours" value="8" min="0.01" max="168" step="any" required style="width:70px;">
    </div>
  </div>
  <div id="sleepStatus" style="font-size:14px; font-weight:bold; color:#666; margin-bottom:10px;">Sleep Timer: Disabled</div>
  <button type="button" class="btn" onclick="setSleepTimer('start')">Start / Restart Timer</button>
  <button type="button" id="sleepStopBtn" class="btn" onclick="setSleepTimer('stop')" disabled>Disable Timer</button>

  <hr>
  <button type="button" class="btn" style="background-color: #ffcccc;" onclick="if(confirm('Erase all settings (Wi-Fi, Jiggler, BLE bonds) and Reboot?')) fetch('/reset_nvs', { method: 'POST' });">Reset NVS (Full Reset)</button>

  <script>
    const statusDiv = document.getElementById('status');
    const printBtn = document.getElementById('printBtn');
    const netInfoDiv = document.getElementById('netInfo');
    const apBanner = document.getElementById('apBanner');
    const sleepStatusDiv = document.getElementById('sleepStatus');
    const sleepStopBtn = document.getElementById('sleepStopBtn');

    let sleepActive = false;
    let sleepRemSec = 0;
    let isPrintingNow = false;
    let pollTick = 0;

    function escHtml(s) {
      return String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
    }

    function formatHms(totalSec) {
      const h = Math.floor(totalSec / 3600);
      const m = Math.floor((totalSec % 3600) / 60);
      const s = totalSec % 60;
      return String(h).padStart(2, '0') + ':' + String(m).padStart(2, '0') + ':' + String(s).padStart(2, '0');
    }

    function renderSleepStatus() {
      if (!sleepActive) {
        sleepStatusDiv.innerHTML = "<span style='color:#666;'>Sleep Timer: Disabled</span>";
        sleepStopBtn.disabled = true;
      } else if (sleepRemSec > 0) {
        sleepStatusDiv.innerHTML = `<span style='color:#0056b3;'>Sleep Timer Active: ${formatHms(sleepRemSec)} remaining</span>`;
        sleepStopBtn.disabled = false;
      } else {
        sleepStatusDiv.innerHTML = "<span style='color:#d9534f;'>Sleep Mode Active (Mouse &amp; Scroller paused)</span>";
        sleepStopBtn.disabled = false;
      }
    }

    function setSleepTimer(action) {
      let data = new URLSearchParams();
      data.set('action', action);
      if (action === 'start') {
        const hrs = parseFloat(document.getElementById('sleep_hours').value);
        if (isNaN(hrs) || hrs <= 0) {
          alert('Please enter a valid number of hours');
          return;
        }
        data.set('hours', String(hrs));
      }
      fetch('/sleep', { method: 'POST', body: data })
        .then(() => updateStatus());
    }

    function updateStatus() {
      fetch('/status').then(r => r.json()).then(data => {
        isPrintingNow = !!data.printing;
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

        sleepActive = !!data.sleep_active;
        sleepRemSec = Number(data.sleep_rem_sec) || 0;
        renderSleepStatus();

        if (data.ap_mode) {
          apBanner.style.display = 'flex';
          if (data.ssid) {
            apBanner.innerHTML = `<span>Home Wi-Fi <b>"${escHtml(data.ssid)}"</b> unreachable (AP mode).</span> <a href="/wifi" class="nav-link">Wi-Fi Settings &rarr;</a>`;
          } else {
            apBanner.innerHTML = `<span>Home Wi-Fi is not configured (AP mode).</span> <a href="/wifi" class="nav-link">Wi-Fi Settings &rarr;</a>`;
          }
        } else {
          apBanner.style.display = 'none';
        }
        let freeKb = (data.free_heap / 1024).toFixed(1);
        let maxBlockKb = (data.max_alloc / 1024).toFixed(1);
        netInfoDiv.innerHTML = `<span>Free RAM: <b>${freeKb} KB</b> (Max block: <b>${maxBlockKb} KB</b>)</span> <a href="/wifi" class="nav-link">Wi-Fi Settings &rarr;</a>`;
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
    updateStatus();
    setInterval(function() {
      if (sleepActive && sleepRemSec > 0) {
        sleepRemSec--;
        renderSleepStatus();
      }
      pollTick++;
      if (!document.hidden && (isPrintingNow || pollTick % 5 === 0)) {
        updateStatus();
      }
    }, 1000);
  </script>
</body></html>
)=====";

const char wifiPage[] PROGMEM = R"=====(
<!DOCTYPE html><html><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Wi-Fi &amp; Network Settings - ESP32 BLE KeyBoard</title>
<style>
  body { font-family: sans-serif; padding: 14px; max-width: 600px; margin: auto; color: #222; }
  h2 { margin-top: 4px; margin-bottom: 12px; }
  input[type="text"], input[type="password"], select { width: 100%; padding: 8px; box-sizing: border-box; margin-top: 4px; margin-bottom: 12px; font-size: 14px; }
  label { display: block; font-size: 14px; font-weight: bold; margin-top: 6px; }
  .btn { padding: 10px 20px; font-size: 15px; font-weight: bold; cursor: pointer; margin-top: 4px; }
  .btn-sm { padding: 6px 12px; font-size: 13px; cursor: pointer; }
  .scan-header { display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 6px; margin-top: 4px; margin-bottom: 4px; }
  .scan-header label { margin: 0; }
  .card { border: 1px solid #ddd; border-radius: 8px; padding: 14px; margin-bottom: 16px; background: #fafafa; }
  .card h3 { margin-top: 0; margin-bottom: 6px; font-size: 17px; }
  .card-desc { font-size: 13px; color: #666; margin-top: 0; margin-bottom: 10px; line-height: 1.4; }
  .info-row { display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 4px 10px; padding: 6px 0; border-bottom: 1px solid #eee; font-size: 14px; }
  .info-row:last-child { border-bottom: none; padding-bottom: 0; }
  .info-label { color: #555; }
  .badge-ap { display: inline-block; padding: 3px 8px; border-radius: 4px; background: #fff3cd; border: 1px solid #ffeeba; color: #856404; font-weight: bold; font-size: 13px; }
  .badge-sta { display: inline-block; padding: 3px 8px; border-radius: 4px; background: #d4edda; border: 1px solid #c3e6cb; color: #155724; font-weight: bold; font-size: 13px; }
  .badge-err { display: inline-block; padding: 3px 8px; border-radius: 4px; background: #f8d7da; border: 1px solid #f5c6cb; color: #721c24; font-weight: bold; font-size: 13px; }
  .nav-link { font-weight: bold; color: #0056b3; text-decoration: none; display: inline-block; margin-bottom: 10px; }
  .nav-link:hover { text-decoration: underline; }
  code { background: #eee; padding: 2px 6px; border-radius: 4px; font-size: 13px; }
  .pass-wrap { position: relative; display: flex; align-items: center; margin-top: 4px; margin-bottom: 12px; }
  .pass-wrap input { margin: 0; padding-right: 40px; }
  .eye-btn { position: absolute; right: 4px; background: none; border: none; font-size: 18px; cursor: pointer; padding: 4px 8px; opacity: 0.45; line-height: 1; }
  .eye-btn.active { opacity: 1; }
  input::-ms-reveal, input::-ms-clear { display: none; }
</style>
</head>
<body>
  <a href="/" class="nav-link">&larr; Back to Main Page (Keyboard &amp; Mouse)</a>
  <h2>Wi-Fi &amp; Network Settings</h2>

  <div class="card">
    <h3>1. Current Network Status</h3>
    <p class="card-desc">Shows how the ESP32 is currently connected and reachable right now.</p>
    <div class="info-row"><span class="info-label">Operating Mode:</span> <span id="curMode">Loading...</span></div>
    <div class="info-row"><span class="info-label">Active Network (SSID):</span> <b id="curNetwork">-</b></div>
    <div class="info-row"><span class="info-label">Current IP Address:</span> <code id="curIp">-</code></div>
    <div class="info-row"><span class="info-label">mDNS Address:</span> <code>http://vkey.local</code></div>
    <div class="info-row"><span class="info-label">Device Hostname:</span> <code id="curHostname">-</code></div>
  </div>

  <div class="card">
    <h3>2. Connect to Home Wi-Fi Router (Client Mode)</h3>
    <p class="card-desc">Select your external Wi-Fi router below so the ESP32 connects to your local network on boot.</p>
    <div class="info-row"><span class="info-label">Saved Home SSID:</span> <b id="savedSsidText">-</b></div>
    <div class="info-row" style="border-bottom:none;"><span class="info-label">Home Wi-Fi Status:</span> <span id="homeWifiStatus">-</span></div>
    <hr style="border:0; border-top:1px solid #ddd; margin: 10px 0 12px 0;">
    <form id="wifiForm">
      <div class="scan-header">
        <label for="ssidSelect">Available Wi-Fi Networks:</label>
        <button type="button" id="scanBtn" class="btn-sm" onclick="scanWifi(true)">Rescan Networks</button>
      </div>
      <select id="ssidSelect" onchange="if(this.value) document.getElementById('wifi_ssid').value=this.value;">
        <option value="">-- Scanning networks... --</option>
      </select>
      <label for="wifi_ssid">Home Wi-Fi SSID (Network Name):</label>
      <input type="text" name="ssid" id="wifi_ssid" placeholder="Enter your home Wi-Fi SSID" autocorrect="off" autocapitalize="none" spellcheck="false" required>
      <label for="wifi_pass">Home Wi-Fi Password:</label>
      <div class="pass-wrap">
        <input type="password" name="pass" id="wifi_pass" placeholder="Enter your home Wi-Fi password (leave empty if open)" autocorrect="off" autocapitalize="none" spellcheck="false">
        <button type="button" class="eye-btn" title="Show/Hide Password" onclick="const p=document.getElementById('wifi_pass');const s=p.type==='password';p.type=s?'text':'password';this.classList.toggle('active',s);">&#128065;</button>
      </div>
      <button type="submit" class="btn">Save Wi-Fi &amp; Reboot</button>
    </form>
  </div>

  <div class="card">
    <h3>3. ESP32 Built-in Access Point (Fallback Hotspot)</h3>
    <p class="card-desc">Credentials of the ESP32's own built-in Wi-Fi hotspot (active when no home Wi-Fi is configured or when your home router is unreachable).</p>
    <div class="info-row"><span class="info-label">Hotspot State:</span> <b id="apState">-</b></div>
    <div class="info-row"><span class="info-label">ESP32 AP Name (SSID):</span> <code id="apSsid">-</code></div>
    <div class="info-row"><span class="info-label">ESP32 AP Password (WPA3):</span> <code id="apPass">Press BOOT button</code></div>
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
          curNetwork.textContent = data.hostname + ' (ESP32 AP)';
          if (data.ssid) {
            curMode.innerHTML = '<span class="badge-ap">AP Mode (Fallback)</span>';
            homeWifiStatus.innerHTML = '<span class="badge-err">Failed to connect</span>';
            apState.textContent = 'Active (Fallback)';
          } else {
            curMode.innerHTML = '<span class="badge-ap">AP Mode (Initial Setup)</span>';
            homeWifiStatus.textContent = 'Not configured yet';
            apState.textContent = 'Active (Setup Mode)';
          }
        } else {
          curMode.innerHTML = '<span class="badge-sta">Client Mode (Home Wi-Fi)</span>';
          curNetwork.textContent = data.ssid;
          homeWifiStatus.innerHTML = '<span class="badge-sta">Connected</span>';
          apState.textContent = 'Standby (Inactive)';
        }

        document.getElementById('curIp').textContent = 'http://' + data.ip;
        document.getElementById('curHostname').textContent = data.hostname;
        document.getElementById('savedSsidText').textContent = data.ssid ? data.ssid : 'Not configured';
        document.getElementById('apSsid').textContent = data.hostname;

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
      const req = refresh ? fetch('/scan', { method: 'POST' }) : fetch('/scan');
      req.then(r => r.json()).then(list => {
        select.innerHTML = '<option value="">-- Select a scanned Wi-Fi network --</option>';
        list.forEach(item => {
          const opt = document.createElement('option');
          opt.value = item.ssid;
          opt.textContent = `${item.ssid} (${item.rssi} dBm)${item.secure ? ' 🔒' : ''}`;
          select.appendChild(opt);
        });
        scanBtn.disabled = false;
        scanBtn.textContent = 'Rescan Networks';
      }).catch(() => {
        select.innerHTML = '<option value="">-- Scan failed, enter SSID manually --</option>';
        scanBtn.disabled = false;
        scanBtn.textContent = 'Rescan Networks';
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

