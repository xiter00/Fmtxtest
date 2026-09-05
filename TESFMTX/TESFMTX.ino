/*
  ===========================================================================
  TESFMTX - ESP32-S3 controller for Elechouse KT0803L FM Transmitter module
  ===========================================================================

  APA YANG DILAKUKAN SKETCH INI:
  - ESP32-S3 mengontrol chip KT0803L via I2C (SDA=GPIO35, SCL=GPIO36)
  - Begitu ESP32 nyala, dia langsung buat WiFi Access Point (hotspot):
        SSID     : TESFMTX
        Password : testingfm123
  - Buka http://192.168.4.1 di browser HP/laptop yang connect ke hotspot itu
    untuk atur frekuensi FM TX, mute, mono/stereo, PGA (gain audio), dan
    RF gain (kuat sinyal pemancar).
  - Setting terakhir disimpan di NVS (flash), jadi kalau ESP restart,
    settingan sebelumnya otomatis dipakai lagi.

  CATATAN PENTING SOAL HARDWARE:
  - Jack audio di modul itu murni jalur ANALOG (masuk ke pin INL/INR
    KT0803L). Firmware ini TIDAK mengontrol sumber audio (mic vs jack) -
    itu murni tergantung wiring/switch mekanis di PCB modul kamu.
    Kalau mic onboard mati tapi jack juga gak bunyi, kemungkinan besar
    itu soal hardware modul (jalur mic/jack/short), bukan soal kode ini.
  - Komponen kecil panjang bertuliskan "EACD" di sebelah IC kemungkinan
    besar adalah crystal/resonator referensi clock buat KT0803L - tidak
    perlu disetting dari software.
  - KT0803L pakai I2C address 0x3E (7-bit) / 0x7C (8-bit write).
    Library akan otomatis pakai address ini.
  - Range frekuensi FM yang dipakai di sini: 76.0 - 108.0 MHz.

  LIBRARY YANG DIPAKAI:
  - KT0803 by Rob Tillaart (support KT0803, KT0803K, KT0803L, KT0803M)
    https://github.com/RobTillaart/KT0803
    (diinstall otomatis oleh GitHub Actions workflow yang disertakan)

  PIN:
  - SDA -> GPIO 35
  - SCL -> GPIO 36
  - Ingat: KT0803L itu device 3.3V. ESP32-S3 GPIO defaultnya 3.3V logic
    jadi aman, tapi tetap cek datasheet modul elechouse-nya kalau ragu.
  ===========================================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Preferences.h>
#include <KT0803.h>

// ------------------------- KONFIGURASI USER -------------------------------
#define I2C_SDA_PIN      35
#define I2C_SCL_PIN      36

#define WIFI_AP_SSID     "TESFMTX"
#define WIFI_AP_PASSWORD "testingfm123"

#define FREQ_MIN_MHZ     76.0f
#define FREQ_MAX_MHZ     108.0f
#define FREQ_DEFAULT_MHZ 100.0f
// ---------------------------------------------------------------------------

KT0803L   fm;
WebServer server(80);
Preferences prefs;

float   currentFreq   = FREQ_DEFAULT_MHZ;
bool    currentMute   = false;
bool    currentStereo = true;
uint8_t currentPGA    = 0;   // 0..7, lihat tabel PGA di README
uint8_t currentRFGain = 15;  // 0..15, default 15 (paling kuat)
bool    chipConnected = false;

// ------------------------- NVS (SIMPAN SETTING) ----------------------------
void loadSettings() {
  prefs.begin("fmtx", true);
  currentFreq   = prefs.getFloat("freq", FREQ_DEFAULT_MHZ);
  currentMute   = prefs.getBool("mute", false);
  currentStereo = prefs.getBool("stereo", true);
  currentPGA    = prefs.getUChar("pga", 0);
  currentRFGain = prefs.getUChar("rfgain", 15);
  prefs.end();

  if (currentFreq < FREQ_MIN_MHZ || currentFreq > FREQ_MAX_MHZ) {
    currentFreq = FREQ_DEFAULT_MHZ;
  }
}

void saveSettings() {
  prefs.begin("fmtx", false);
  prefs.putFloat("freq", currentFreq);
  prefs.putBool("mute", currentMute);
  prefs.putBool("stereo", currentStereo);
  prefs.putUChar("pga", currentPGA);
  prefs.putUChar("rfgain", currentRFGain);
  prefs.end();
}

// ------------------------- APPLY KE CHIP -----------------------------------
void applyToChip() {
  fm.setFrequency(currentFreq);
  fm.setMute(currentMute);
  if (currentStereo) fm.setStereo(); else fm.setMono();
  fm.setPGA(currentPGA);
  fm.setRFGain(currentRFGain);
}

// ------------------------- WEB PAGE (HTML) ----------------------------------
const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>TESFMTX - FM Transmitter Control</title>
<style>
  :root{
    --bg:#0f1220; --card:#181c2e; --accent:#6c8cff; --accent2:#4be1a0;
    --text:#eef0f8; --muted:#9099b8; --danger:#ff6c6c;
  }
  *{box-sizing:border-box;}
  body{
    margin:0; font-family:-apple-system,Segoe UI,Roboto,sans-serif;
    background:linear-gradient(160deg,#0f1220,#181c2e); color:var(--text);
    min-height:100vh; padding:20px;
  }
  .wrap{max-width:480px;margin:0 auto;}
  h1{font-size:1.3rem; margin:0 0 4px 0;}
  .sub{color:var(--muted); font-size:0.85rem; margin-bottom:18px;}
  .card{
    background:var(--card); border-radius:16px; padding:18px 20px;
    margin-bottom:16px; box-shadow:0 4px 20px rgba(0,0,0,0.25);
  }
  .row{display:flex; justify-content:space-between; align-items:center; margin-bottom:10px;}
  .badge{
    display:inline-flex; align-items:center; gap:6px; font-size:0.8rem;
    padding:4px 10px; border-radius:999px; background:#232842;
  }
  .dot{width:9px;height:9px;border-radius:50%;background:var(--danger);}
  .dot.ok{background:var(--accent2);}
  .freq-display{
    font-size:2.6rem; font-weight:700; text-align:center; margin:6px 0 14px 0;
    letter-spacing:1px;
  }
  .freq-display span{font-size:1.1rem; color:var(--muted); font-weight:400;}
  input[type=range]{width:100%; accent-color:var(--accent); height:6px;}
  .fine{display:flex; gap:8px; margin-top:12px;}
  .fine input[type=number]{
    flex:1; background:#0f1220; border:1px solid #2a2f4a; color:var(--text);
    border-radius:10px; padding:10px; font-size:1rem; text-align:center;
  }
  button{
    border:none; border-radius:10px; padding:11px 14px; font-size:0.95rem;
    font-weight:600; cursor:pointer; color:#0f1220;
  }
  .btn-primary{background:var(--accent); color:#fff;}
  .btn-ghost{background:#232842; color:var(--text);}
  .btn-toggle{background:#232842; color:var(--text); width:100%;}
  .btn-toggle.active{background:var(--accent2); color:#0f1220;}
  .grid2{display:grid; grid-template-columns:1fr 1fr; gap:10px;}
  select{
    width:100%; background:#0f1220; color:var(--text); border:1px solid #2a2f4a;
    border-radius:10px; padding:10px; font-size:0.95rem;
  }
  label.small{font-size:0.78rem; color:var(--muted); display:block; margin-bottom:6px;}
  .footer-note{font-size:0.75rem; color:var(--muted); text-align:center; margin-top:6px; line-height:1.4;}
  .toast{
    position:fixed; left:50%; bottom:24px; transform:translateX(-50%);
    background:var(--accent2); color:#0f1220; padding:10px 18px; border-radius:999px;
    font-size:0.85rem; font-weight:600; opacity:0; transition:opacity .25s;
    pointer-events:none;
  }
  .toast.show{opacity:1;}
</style>
</head>
<body>
<div class="wrap">
  <h1>📻 TESFMTX</h1>
  <div class="sub">Kontrol KT0803L FM Transmitter via ESP32-S3</div>

  <div class="card">
    <div class="row">
      <span id="badge" class="badge"><span id="dot" class="dot"></span><span id="badgeText">Cek koneksi...</span></span>
      <span class="sub" style="margin:0">192.168.4.1</span>
    </div>

    <div class="freq-display"><span id="freqBig">--.-</span><span> MHz</span></div>
    <input type="range" id="freqSlider" min="76" max="108" step="0.1" value="100">
    <div class="fine">
      <input type="number" id="freqNum" min="76" max="108" step="0.1" value="100.0">
      <button class="btn-primary" onclick="setFreq()">Set</button>
    </div>
  </div>

  <div class="card">
    <div class="grid2">
      <button id="btnMute" class="btn-toggle" onclick="toggleMute()">🔇 Mute</button>
      <button id="btnStereo" class="btn-toggle" onclick="toggleStereo()">🎧 Stereo</button>
    </div>
  </div>

  <div class="card">
    <label class="small">PGA (Audio Gain)</label>
    <select id="pgaSel" onchange="setMisc()">
      <option value="4">+12 dB</option>
      <option value="6">+8 dB</option>
      <option value="5">+4 dB</option>
      <option value="0" selected>0 dB (default)</option>
      <option value="1">-4 dB</option>
      <option value="2">-8 dB</option>
      <option value="3">-12 dB</option>
    </select>
    <div style="height:12px"></div>
    <label class="small">RF Gain (Kuat Sinyal Pemancar)</label>
    <select id="rfSel" onchange="setMisc()">
      <option value="0">0 - paling lemah</option>
      <option value="4">4</option>
      <option value="8">8</option>
      <option value="12">12</option>
      <option value="15" selected>15 - paling kuat (default)</option>
    </select>
  </div>

  <div class="footer-note">
    Jack audio di modul bersifat analog murni (langsung ke chip).<br>
    Halaman ini cuma atur frekuensi &amp; parameter RF/audio KT0803L.
  </div>
</div>

<div id="toast" class="toast">Tersimpan</div>

<script>
let state = {freq:100.0, mute:false, stereo:true, pga:0, rfgain:15, connected:false};

function showToast(msg){
  const t=document.getElementById('toast');
  t.textContent = msg; t.classList.add('show');
  setTimeout(()=>t.classList.remove('show'), 1200);
}

function renderState(){
  document.getElementById('freqBig').textContent = state.freq.toFixed(1);
  document.getElementById('freqSlider').value = state.freq;
  document.getElementById('freqNum').value = state.freq.toFixed(1);

  const dot = document.getElementById('dot');
  const bt  = document.getElementById('badgeText');
  dot.classList.toggle('ok', state.connected);
  bt.textContent = state.connected ? 'KT0803L Terhubung' : 'KT0803L Tidak Terdeteksi';

  const bm = document.getElementById('btnMute');
  bm.classList.toggle('active', state.mute);
  bm.textContent = state.mute ? '🔇 Muted (tap untuk unmute)' : '🔊 Unmuted (tap untuk mute)';

  const bs = document.getElementById('btnStereo');
  bs.classList.toggle('active', state.stereo);
  bs.textContent = state.stereo ? '🎧 Stereo' : '🔈 Mono';

  document.getElementById('pgaSel').value = state.pga;
  document.getElementById('rfSel').value = state.rfgain;
}

async function fetchStatus(){
  try{
    const r = await fetch('/status');
    const j = await r.json();
    state = {freq:j.freq, mute:j.mute, stereo:j.stereo, pga:j.pga, rfgain:j.rfgain, connected:j.connected};
    renderState();
  }catch(e){ /* silent */ }
}

async function pushSet(params){
  const q = new URLSearchParams(params).toString();
  try{
    const r = await fetch('/set?' + q);
    const j = await r.json();
    state = {freq:j.freq, mute:j.mute, stereo:j.stereo, pga:j.pga, rfgain:j.rfgain, connected:j.connected};
    renderState();
    showToast('Tersimpan');
  }catch(e){ showToast('Gagal kirim'); }
}

function setFreq(){
  let v = parseFloat(document.getElementById('freqNum').value);
  if(isNaN(v)) v = parseFloat(document.getElementById('freqSlider').value);
  v = Math.min(108, Math.max(76, v));
  pushSet({freq:v.toFixed(1)});
}

document.getElementById('freqSlider').addEventListener('input', (e)=>{
  document.getElementById('freqNum').value = parseFloat(e.target.value).toFixed(1);
});
document.getElementById('freqSlider').addEventListener('change', setFreq);

function toggleMute(){ pushSet({mute: state.mute ? 0 : 1}); }
function toggleStereo(){ pushSet({stereo: state.stereo ? 0 : 1}); }
function setMisc(){
  pushSet({
    pga: document.getElementById('pgaSel').value,
    rfgain: document.getElementById('rfSel').value
  });
}

fetchStatus();
setInterval(fetchStatus, 4000);
</script>
</body>
</html>
)HTMLPAGE";

// ------------------------- HTTP HANDLERS ------------------------------------
void sendStatusJson() {
  chipConnected = fm.isConnected();
  String json = "{";
  json += "\"connected\":" + String(chipConnected ? "true" : "false") + ",";
  json += "\"freq\":" + String(currentFreq, 1) + ",";
  json += "\"mute\":" + String(currentMute ? "true" : "false") + ",";
  json += "\"stereo\":" + String(currentStereo ? "true" : "false") + ",";
  json += "\"pga\":" + String(currentPGA) + ",";
  json += "\"rfgain\":" + String(currentRFGain);
  json += "}";
  server.send(200, "application/json", json);
}

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleStatus() {
  sendStatusJson();
}

void handleSet() {
  bool changed = false;

  if (server.hasArg("freq")) {
    float f = server.arg("freq").toFloat();
    if (f >= FREQ_MIN_MHZ && f <= FREQ_MAX_MHZ) {
      currentFreq = f;
      changed = true;
    }
  }
  if (server.hasArg("mute")) {
    currentMute = server.arg("mute").toInt() == 1;
    changed = true;
  }
  if (server.hasArg("stereo")) {
    currentStereo = server.arg("stereo").toInt() == 1;
    changed = true;
  }
  if (server.hasArg("pga")) {
    int p = server.arg("pga").toInt();
    if (p >= 0 && p <= 7) { currentPGA = (uint8_t)p; changed = true; }
  }
  if (server.hasArg("rfgain")) {
    int r = server.arg("rfgain").toInt();
    if (r >= 0 && r <= 15) { currentRFGain = (uint8_t)r; changed = true; }
  }

  if (changed) {
    applyToChip();
    saveSettings();
  }
  sendStatusJson();
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ------------------------- SETUP / LOOP --------------------------------------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=======================================");
  Serial.println(" TESFMTX - ESP32-S3 + KT0803L FM TX");
  Serial.println("=======================================");

  loadSettings();

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  delay(50);

  bool ok = fm.begin(currentFreq, true); // begin() default mute=true, kita override di bawah
  chipConnected = ok;
  Serial.print("[I2C] KT0803L terdeteksi di alamat 0x3E: ");
  Serial.println(ok ? "YA" : "TIDAK (cek wiring SDA=35, SCL=36, dan power 3.3V modul)");

  applyToChip();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  delay(200);

  Serial.print("[WiFi] SSID     : ");
  Serial.println(WIFI_AP_SSID);
  Serial.print("[WiFi] Password : ");
  Serial.println(WIFI_AP_PASSWORD);
  Serial.print("[WiFi] IP Address: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("[Web] Buka http://192.168.4.1 di browser HP kamu");

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/set", handleSet);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("[Web] Server siap.");
}

void loop() {
  server.handleClient();
}
