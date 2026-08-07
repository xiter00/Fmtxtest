#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <FMTX.h> 

// --- DEKLARASI FUNGSI PGA DARI FMTX.CPP ---
// Karena tidak ada di FMTX.h, kita deklarasikan manual di sini agar bisa dipakai
extern "C++" {
  void fmtx_set_pga(fmtx_pga_type pga);
}

// --- TENTUKAN PIN SDA & SCL DI SINI ---
const int SDA_PIN = 8; // Ganti sesuai pin SDA ESP32-S3 lu
const int SCL_PIN = 9; // Ganti sesuai pin SCL ESP32-S3 lu

// Setting Hotspot ESP32
const char* ssid = "ORCA-FM-LINK";
const char* password = "hackerpassword"; 

WebServer server(80);

// Frekuensi awal radio
float currentFreq = 87.5;

// Tampilan Web UI (HTML + CSS + JavaScript)
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ORCA FM Tx Dashboard</title>
  <style>
    body { background-color: #0a0a0a; color: #00ff00; font-family: 'Courier New', Courier, monospace; text-align: center; margin-top: 50px; }
    h1 { color: #00ff00; text-shadow: 0 0 10px #00ff00; }
    .box { border: 2px solid #00ff00; padding: 20px; margin: 20px auto; width: 80%; max-width: 400px; border-radius: 10px; background: #111; box-shadow: 0 0 15px #00ff0022; }
    input[type="number"] { width: 100px; padding: 10px; font-size: 20px; background: #222; color: #0f0; border: 1px solid #0f0; text-align: center; border-radius: 5px; }
    button { background: #00ff00; color: #000; font-size: 18px; font-weight: bold; border: none; padding: 10px 20px; cursor: pointer; border-radius: 5px; margin-top: 10px; text-transform: uppercase; }
    button:active { background: #00cc00; box-shadow: inset 0 0 10px #000; }
    #ptt-btn { display: block; width: 100%; height: 100px; font-size: 24px; background: #ff0000; color: #fff; margin-top: 30px; box-shadow: 0 0 20px #ff000055; user-select: none; }
    #ptt-btn.active { background: #cc0000; transform: scale(0.95); }
    .status { margin-top: 15px; font-size: 14px; color: #aaa; }
  </style>
</head>
<body>
  <h1>[ ORCA FM-LINK ]</h1>
  
  <div class="box">
    <h3>SET FREKUENSI (MHz)</h3>
    <input type="number" id="freqInput" value="87.5" step="0.1" min="70.0" max="108.0">
    <br>
    <button onclick="changeFreq()">UPDATE FREKUENSI</button>
    <div class="status" id="freqStatus">Radio memancar di: 87.5 MHz</div>
  </div>

  <div class="box">
    <h3>TRANSMISI SUARA (MIC)</h3>
    <p style="font-size: 12px; color:#888;">Tahan tombol untuk bicara (PTT)</p>
    <button id="ptt-btn" 
            onmousedown="talk(true)" onmouseup="talk(false)" onmouseleave="talk(false)"
            ontouchstart="talk(true); event.preventDefault();" ontouchend="talk(false); event.preventDefault();">
      [ TAHAN & BICARA ]
    </button>
    <div class="status" id="micStatus">Status: RADIO BISU (MUTED)</div>
  </div>

  <script>
    function changeFreq() {
      var f = document.getElementById('freqInput').value;
      fetch('/setFreq?f=' + f).then(res => res.text()).then(txt => {
        document.getElementById('freqStatus').innerText = "Radio memancar di: " + f + " MHz";
      });
    }

    function talk(isActive) {
      var btn = document.getElementById('ptt-btn');
      var status = document.getElementById('micStatus');
      
      if (isActive) {
        btn.classList.add('active');
        btn.innerText = "((( MENGUDARA )))";
        status.innerHTML = "<span style='color:#0f0'>Status: ON-AIR (MIC AKTIF)</span>";
        fetch('/unmute'); 
      } else {
        btn.classList.remove('active');
        btn.innerText = "[ TAHAN & BICARA ]";
        status.innerHTML = "Status: RADIO BISU (MUTED)";
        fetch('/mute'); 
      }
    }
  </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  
  // Setup Wi-Fi Access Point (Hotspot)
  Serial.println("Membuat Hotspot ORCA-FM-LINK...");
  WiFi.softAP(ssid, password);
  Serial.print("Hotspot Aktif! IP Address: ");
  Serial.println(WiFi.softAPIP()); 

  // Inisialisasi I2C dengan pin custom
  Wire.begin(SDA_PIN, SCL_PIN); 

  // Inisialisasi FM Transmitter
  fmtx_init(currentFreq, EUROPE); 
  
  // Saat pertama nyalakan, set PGA ke tingkat paling rendah (-12dB) agar mic MUTE
  fmtx_set_pga(PGA_N12DB); 
  Serial.println("Radio Standby: Mic dalam kondisi MUTE.");

  // --- ROUTING WEB SERVER ---
  
  // 1. Kirim Halaman Utama
  server.on("/", []() {
    server.send(200, "text/html", htmlPage);
  });

  // 2. Endpoint Ganti Frekuensi
  server.on("/setFreq", []() {
    if (server.hasArg("f")) {
      currentFreq = server.arg("f").toFloat();
      fmtx_set_freq(currentFreq);
      Serial.print("Frekuensi diubah ke: ");
      Serial.println(currentFreq);
    }
    server.send(200, "text/plain", "OK");
  });

  // 3. Endpoint Tombol PTT Ditekan (UNMUTE / MIC HIDUP)
  server.on("/unmute", []() {
    fmtx_set_pga(PGA_12DB); // Naikkan gain mic maksimal (+12dB)
    Serial.println("MIC AKTIF! MANCAR...");
    server.send(200, "text/plain", "UNMUTED");
  });

  // 4. Endpoint Tombol PTT Dilepas (MUTE / MIC MATI)
  server.on("/mute", []() {
    fmtx_set_pga(PGA_N12DB); // Turunkan gain mic minimal (-12dB / nyaris tak terdengar)
    Serial.println("MIC MATI (Muted).");
    server.send(200, "text/plain", "MUTED");
  });

  // Mulai Server
  server.begin();
  Serial.println("Web Server berjalan!");
}

void loop() {
  server.handleClient();
}
