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
    <textarea name="payload" id="payloadText" rows="10" maxlength="32768" placeholder="Enter text (up to 32 KB)..."></textarea><br><br>
    <input type="submit" id="printBtn" class="btn" value="Print">
    <button type="button" id="cancelBtn" class="btn" onclick="apiPost('/cancel').then(async r => { if(!r.ok) alert(await r.text() || 'Cancel failed'); updateStatus(); })">Cancel</button>
  </form>

  <hr>
  <h3>Keyboard Settings</h3>
  <form id="keyboardForm">
    <div class="form-row">
      <span class="form-label">Key Press Hold (ms):</span>
      <div class="form-pair">
        <label>Min <input type="number" name="kb_prs_min" id="set_kb_prs_min" min="1" max="1000" required></label>
        <label>Max <input type="number" name="kb_prs_max" id="set_kb_prs_max" min="1" max="1000" required></label>
      </div>
    </div>
    <div class="form-row">
      <span class="form-label">Delay Between Keys (ms):</span>
      <div class="form-pair">
        <label>Min <input type="number" name="kb_dly_min" id="set_kb_dly_min" min="1" max="1000" required></label>
        <label>Max <input type="number" name="kb_dly_max" id="set_kb_dly_max" min="1" max="1000" required></label>
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
        <label>Min <input type="number" name="int_min" id="set_int_min" min="1" max="86400" required></label>
        <label>Max <input type="number" name="int_max" id="set_int_max" min="1" max="86400" required></label>
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
        <label>Min <input type="number" name="ret_min" id="set_ret_min" min="1" max="5000" required></label>
        <label>Max <input type="number" name="ret_max" id="set_ret_max" min="1" max="5000" required></label>
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
        <label>Min <input type="number" name="sc_int_min" id="set_sc_int_min" min="1" max="86400" required></label>
        <label>Max <input type="number" name="sc_int_max" id="set_sc_int_max" min="1" max="86400" required></label>
      </div>
    </div>
    <div class="form-row">
      <span class="form-label">Burst Steps:</span>
      <div class="form-pair">
        <label>Min <input type="number" name="sc_cnt_min" id="set_sc_cnt_min" min="1" max="100" required></label>
        <label>Max <input type="number" name="sc_cnt_max" id="set_sc_cnt_max" min="1" max="100" required></label>
      </div>
    </div>
    <div class="form-row">
      <span class="form-label">Step Delay (ms):</span>
      <div class="form-pair">
        <label>Min <input type="number" name="sc_dly_min" id="set_sc_dly_min" min="10" max="1000" required></label>
        <label>Max <input type="number" name="sc_dly_max" id="set_sc_dly_max" min="10" max="1000" required></label>
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
        <label>Min <input type="number" name="sc_ret_min" id="set_sc_ret_min" min="1" max="3600" required></label>
        <label>Max <input type="number" name="sc_ret_max" id="set_sc_ret_max" min="1" max="3600" required></label>
      </div>
    </div>
    <button type="submit" class="btn">Save Scroller Settings</button>
  </form>

  <hr>
  <h3>Sleep Timer (RAM Only)</h3>
  <div class="form-row">
    <span class="form-label">Stop Mouse &amp; Scroller after (hours):</span>
    <div class="form-pair">
      <input type="number" id="sleep_hours" value="8" min="0.01" max="24" step="any" required style="width:70px;">
    </div>
  </div>
  <div id="sleepStatus" style="font-size:14px; font-weight:bold; color:#666; margin-bottom:10px;">Sleep Timer: Disabled</div>
  <button type="button" class="btn" onclick="setSleepTimer('start')">Start / Restart Timer</button>
  <button type="button" id="sleepStopBtn" class="btn" onclick="setSleepTimer('stop')" disabled>Disable Timer</button>

  <hr>
  <button type="button" class="btn" style="background-color: #ffcccc;" onclick="if(confirm('Erase all settings (Wi-Fi, Jiggler, BLE bonds) and Reboot?')) apiPost('/reset_nvs').then(async r => { alert(await r.text() || 'Reset request sent'); });">Reset NVS (Full Reset)</button>

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
    let statusInFlight = false;
    let pollTick = 0;
    let csrfToken = '';

    function sha256_128(str) {
      const K = [
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
      ];
      const bytes = new TextEncoder().encode(str);
      const bitLen = bytes.length * 8;
      const totalWords = (((bytes.length + 8) >> 6) + 1) << 4;
      const words = new Int32Array(totalWords);
      for (let i = 0; i < bytes.length; i++) {
        words[i >> 2] |= bytes[i] << (24 - (i & 3) * 8);
      }
      words[bytes.length >> 2] |= 0x80 << (24 - (bytes.length & 3) * 8);
      words[totalWords - 2] = Math.floor(bitLen / 0x100000000);
      words[totalWords - 1] = bitLen | 0;

      let H = [0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19];
      const W = new Int32Array(64);
      const rotr = (n, x) => (x >>> n) | (x << (32 - n));

      for (let i = 0; i < totalWords; i += 16) {
        for (let t = 0; t < 16; t++) W[t] = words[i + t];
        for (let t = 16; t < 64; t++) {
          const s0 = rotr(7, W[t - 15]) ^ rotr(18, W[t - 15]) ^ (W[t - 15] >>> 3);
          const s1 = rotr(17, W[t - 2]) ^ rotr(19, W[t - 2]) ^ (W[t - 2] >>> 10);
          W[t] = (W[t - 16] + s0 + W[t - 7] + s1) | 0;
        }
        let [a, b, c, d, e, f, g, h] = H;
        for (let t = 0; t < 64; t++) {
          const S1 = rotr(6, e) ^ rotr(11, e) ^ rotr(25, e);
          const ch = (e & f) ^ (~e & g);
          const temp1 = (h + S1 + ch + K[t] + W[t]) | 0;
          const S0 = rotr(2, a) ^ rotr(13, a) ^ rotr(22, a);
          const maj = (a & b) ^ (a & c) ^ (b & c);
          const temp2 = (S0 + maj) | 0;
          h = g; g = f; f = e; e = (d + temp1) | 0;
          d = c; c = b; b = a; a = (temp1 + temp2) | 0;
        }
        H = [(H[0]+a)|0, (H[1]+b)|0, (H[2]+c)|0, (H[3]+d)|0, (H[4]+e)|0, (H[5]+f)|0, (H[6]+g)|0, (H[7]+h)|0];
      }
      return H.slice(0, 4).map(v => (v >>> 0).toString(16).padStart(8, '0')).join('');
    }

    async function apiPost(url, body) {
      let raw = '';
      if (typeof body === 'string') {
        raw = body;
      } else if (body instanceof URLSearchParams) {
        for (const [k, v] of body) raw += k + '=' + v + '\n';
      }
      const h = sha256_128(raw);
      if (!csrfToken) {
        try {
          const s = await fetch('/status').then(r => r.json());
          if (s.token) csrfToken = s.token;
        } catch (e) {}
      }
      const sendReq = () => {
        const headers = { 'X-VKey-Token': csrfToken };
        if (typeof body === 'string') headers['Content-Type'] = 'text/plain;charset=UTF-8';
        const opts = { method: 'POST', headers };
        if (body !== undefined) opts.body = body;
        return fetch(url + '?h=' + h, opts);
      };
      let res = await sendReq();
      if (res.status === 403) {
        try {
          const s = await fetch('/status').then(r => r.json());
          if (s.token) csrfToken = s.token;
          res = await sendReq();
        } catch (e) {}
      }
      return res;
    }

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
        if (isNaN(hrs) || hrs <= 0 || hrs > 24) {
          alert('Please enter hours between 0.01 and 24');
          return;
        }
        data.set('hours', String(hrs));
      }
      apiPost('/sleep', data).then(async r => {
        if (!r.ok) alert(await r.text() || 'Failed to update sleep timer');
        updateStatus();
      });
    }

    function updateStatus() {
      if (statusInFlight) return;
      statusInFlight = true;
      fetch('/status').then(async r => {
        if (r.status === 429) {
          statusDiv.innerHTML = "<span style='color:#d9534f;'>SECURITY LOCKOUT &mdash; Reboot device!</span>";
          printBtn.disabled = true;
          return null;
        }
        if (!r.ok) return null;
        return r.json();
      }).then(data => {
        if (!data) return;
        if (data.token) csrfToken = data.token;
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
      }).catch(() => {}).finally(() => {
        statusInFlight = false;
      });
    }

    document.getElementById('typeForm').onsubmit = function(e) {
      e.preventDefault();
      const text = document.getElementById('payloadText').value;
      const byteLen = new TextEncoder().encode(text).length;
      if (byteLen > 32768) {
        alert(`Text is too large (${(byteLen / 1024).toFixed(1)} KB). Maximum allowed is 32 KB.`);
        return;
      }
      apiPost('/type', text).then(async r => {
        if (!r.ok) alert(await r.text() || 'Print failed (' + r.status + ')');
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
      apiPost('/keyboard', data).then(async r => {
        if (!r.ok) { alert(await r.text() || 'Failed to save keyboard settings'); return; }
        loadSettings();
        alert('Keyboard settings saved!');
      });
    };

    document.getElementById('settingsForm').onsubmit = function(e) {
      e.preventDefault();
      let data = new URLSearchParams(new FormData(e.target));
      if (!document.getElementById('set_enabled').checked) data.append('enabled', 'false');
      else data.set('enabled', 'true');

      apiPost('/settings', data).then(async r => {
        if (!r.ok) { alert(await r.text() || 'Failed to save jiggler settings'); return; }
        loadSettings();
        alert('Jiggler settings saved!');
      });
    };

    document.getElementById('scrollerForm').onsubmit = function(e) {
      e.preventDefault();
      let data = new URLSearchParams(new FormData(e.target));
      if (!document.getElementById('set_sc_enabled').checked) data.append('sc_enabled', 'false');
      else data.set('sc_enabled', 'true');

      apiPost('/scroller', data).then(async r => {
        if (!r.ok) { alert(await r.text() || 'Failed to save scroller settings'); return; }
        loadSettings();
        alert('Scroller settings saved!');
      });
    };

    document.addEventListener('visibilitychange', function() {
      if (!document.hidden) updateStatus();
    });
    updateStatus();
    loadSettings();
    setInterval(function() {
      if (sleepActive && sleepRemSec > 0) {
        sleepRemSec--;
        renderSleepStatus();
      }
      pollTick++;
      const pollIntervalSec = isPrintingNow ? 3 : 5;
      if (!document.hidden && (pollTick % pollIntervalSec === 0)) {
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

  <div class="card">
    <h3>4. Bluetooth Pairing (Ugreen BT)</h3>
    <p class="card-desc">Once paired with a computer, BLE is locked to that host so other nearby devices cannot connect. Clear pairing here if you reinstall your OS or switch computers.</p>
    <div class="info-row" style="border-bottom:none;"><span class="info-label">BLE Pairing State:</span> <span id="blePairState">Loading...</span></div>
    <button type="button" class="btn-sm" style="margin-top:8px;" onclick="resetBlePairing()">Reset BLE Pairing</button>
  </div>

  <a href="/" class="nav-link">&larr; Back to Main Page (Keyboard &amp; Mouse)</a>

  <script>
    let csrfToken = '';

    function sha256_128(str) {
      const K = [
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
      ];
      const bytes = new TextEncoder().encode(str);
      const bitLen = bytes.length * 8;
      const totalWords = (((bytes.length + 8) >> 6) + 1) << 4;
      const words = new Int32Array(totalWords);
      for (let i = 0; i < bytes.length; i++) {
        words[i >> 2] |= bytes[i] << (24 - (i & 3) * 8);
      }
      words[bytes.length >> 2] |= 0x80 << (24 - (bytes.length & 3) * 8);
      words[totalWords - 2] = Math.floor(bitLen / 0x100000000);
      words[totalWords - 1] = bitLen | 0;

      let H = [0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19];
      const W = new Int32Array(64);
      const rotr = (n, x) => (x >>> n) | (x << (32 - n));

      for (let i = 0; i < totalWords; i += 16) {
        for (let t = 0; t < 16; t++) W[t] = words[i + t];
        for (let t = 16; t < 64; t++) {
          const s0 = rotr(7, W[t - 15]) ^ rotr(18, W[t - 15]) ^ (W[t - 15] >>> 3);
          const s1 = rotr(17, W[t - 2]) ^ rotr(19, W[t - 2]) ^ (W[t - 2] >>> 10);
          W[t] = (W[t - 16] + s0 + W[t - 7] + s1) | 0;
        }
        let [a, b, c, d, e, f, g, h] = H;
        for (let t = 0; t < 64; t++) {
          const S1 = rotr(6, e) ^ rotr(11, e) ^ rotr(25, e);
          const ch = (e & f) ^ (~e & g);
          const temp1 = (h + S1 + ch + K[t] + W[t]) | 0;
          const S0 = rotr(2, a) ^ rotr(13, a) ^ rotr(22, a);
          const maj = (a & b) ^ (a & c) ^ (b & c);
          const temp2 = (S0 + maj) | 0;
          h = g; g = f; f = e; e = (d + temp1) | 0;
          d = c; c = b; b = a; a = (temp1 + temp2) | 0;
        }
        H = [(H[0]+a)|0, (H[1]+b)|0, (H[2]+c)|0, (H[3]+d)|0, (H[4]+e)|0, (H[5]+f)|0, (H[6]+g)|0, (H[7]+h)|0];
      }
      return H.slice(0, 4).map(v => (v >>> 0).toString(16).padStart(8, '0')).join('');
    }

    async function apiPost(url, body) {
      let raw = '';
      if (typeof body === 'string') {
        raw = body;
      } else if (body instanceof URLSearchParams) {
        for (const [k, v] of body) raw += k + '=' + v + '\n';
      }
      const h = sha256_128(raw);
      if (!csrfToken) {
        try {
          const s = await fetch('/status').then(r => r.json());
          if (s.token) csrfToken = s.token;
        } catch (e) {}
      }
      const sendReq = () => {
        const headers = { 'X-VKey-Token': csrfToken };
        if (typeof body === 'string') headers['Content-Type'] = 'text/plain;charset=UTF-8';
        const opts = { method: 'POST', headers };
        if (body !== undefined) opts.body = body;
        return fetch(url + '?h=' + h, opts);
      };
      let res = await sendReq();
      if (res.status === 403) {
        try {
          const s = await fetch('/status').then(r => r.json());
          if (s.token) csrfToken = s.token;
          res = await sendReq();
        } catch (e) {}
      }
      return res;
    }

    function loadNetworkInfo() {
      fetch('/status').then(async r => {
        if (r.status === 429) {
          document.getElementById('curMode').innerHTML = '<span class="badge-err">SECURITY LOCKOUT &mdash; Reboot device!</span>';
          return null;
        }
        if (!r.ok) return null;
        return r.json();
      }).then(data => {
        if (!data) return;
        if (data.token) csrfToken = data.token;
        const curMode = document.getElementById('curMode');
        const curNetwork = document.getElementById('curNetwork');
        const apState = document.getElementById('apState');
        const homeWifiStatus = document.getElementById('homeWifiStatus');
        const blePairState = document.getElementById('blePairState');

        if (data.connected) {
          blePairState.innerHTML = '<span class="badge-sta">Paired &amp; Connected (Encrypted)</span>';
        } else if (data.ble_paired) {
          blePairState.innerHTML = '<span class="badge-ap">Paired (Locked to Host)</span>';
        } else {
          blePairState.innerHTML = 'Open for Pairing';
        }

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

    function resetBlePairing() {
      if (!confirm("Clear saved BLE pairing so a new computer can pair with 'Ugreen BT'?\n(Wi-Fi and Jiggler settings will NOT be erased.)")) return;
      apiPost('/reset_ble').then(async r => {
        const msg = await r.text();
        alert(msg || (r.ok ? 'BLE pairing cleared' : 'Failed to clear BLE pairing'));
        loadNetworkInfo();
      });
    }

    function scanWifi(refresh) {
      const scanBtn = document.getElementById('scanBtn');
      const select = document.getElementById('ssidSelect');
      scanBtn.disabled = true;
      scanBtn.textContent = 'Scanning...';
      if (refresh) {
        select.innerHTML = '<option value="">-- Scanning networks... --</option>';
      }
      const req = refresh ? apiPost('/scan') : fetch('/scan');
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
      apiPost('/wifi', data)
        .then(r => r.text())
        .then(msg => alert(msg));
    };

    loadNetworkInfo();
    scanWifi(false);
  </script>
</body></html>
)=====";

