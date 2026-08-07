#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <FMTX.h> // Pastikan udah install library FMTX by Elechouse

const int SDAPIN = 8; 
const int SCLPIN = 9;

// Setting Hotspot ESP32
const char* ssid = "ORCA-FM-LINK";
const char* password = "hackerpassword"; // Minimal 8 karakter

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
    <!-- Mendukung sentuhan HP dan klik Mouse -->
    <button id="ptt-btn" 
            onmousedown="talk(true)" onmouseup="talk(false)" onmouseleave="talk(false)"
            ontouchstart="talk(true); event.preventDefault();" ontouchend="talk(false); event.preventDefault();">
      [ TAHAN & BICARA ]
    </button>
    <div class="status" id="micStatus">Status: RADIO BISU (MUTE)</div>
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
        fetch('/unmute'); // Kirim perintah unmute ke ESP32
      } else {
        btn.classList.remove('active');
        btn.innerText = "[ TAHAN & BICARA ]";
        status.innerHTML = "Status: RADIO BISU (MUTE)";
        fetch('/mute'); // Kirim perintah mute ke ESP32
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
  Serial.println(WiFi.softAPIP()); // Biasanya 192.168.4.1

  // Inisialisasi FM Transmitter (Sesuaikan SDA SCL ESP32-S3 lu)
  // Wire.begin(SDAPIN, SCLPIN); // Kalau lu pake pin custom buka comment ini
  fmtx_init(currentFreq, EUROPE); 
  // Biar awal nyala radio langsung diam (nggak bocor suara)
  // Cara Mute tergantung chip, untuk FMTX V2 biasanya bisa pakai perintah I2C atau diatur volume ke 0
  fmtx_set_vol(0); 

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

  // 3. Endpoint Tombol Ngomong Ditekan (UNMUTE)
  server.on("/unmute", []() {
    Serial.println("MIC AKTIF! MANCAR...");
    fmtx_set_vol(15); // Volume maksimal (0-15) buat mancar
    server.send(200, "text/plain", "UNMUTED");
  });

  // 4. Endpoint Tombol Ngomong Dilepas (MUTE)
  server.on("/mute", []() {
    Serial.println("MIC MATI.");
    fmtx_set_vol(0); // Volume 0 buat matiin mic
    server.send(200, "text/plain", "MUTED");
  });

  // Mulai Server
  server.begin();
  Serial.println("Web Server berjalan!");
}

void loop() {
  // Biarkan server dengerin request dari HP lu terus-terusan
  server.handleClient();
}
