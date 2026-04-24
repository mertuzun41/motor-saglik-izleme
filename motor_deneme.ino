/*******************************************************
 * MOTOR SAĞLIĞI İZLEME - ESP32-S3
 * - AsyncWebServer + WebSocket ("/ws")
 * - Canlı grafik (Chart.js) -> ESP32 LittleFS üzerinden /chart.min.js
 * - RPM / Akım / Yalpalama / Sıcaklık + Limitler + Koruma
 *******************************************************/

#include <Wire.h>
#include <Adafruit_INA219.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

// ---------------- WIFI (SoftAP) ----------------
const char* ssid = "Muh_Tas1";
const char* password = "12345678";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ---------------- LED PINLERİ ----------------
const int LED_RED    = 14;
const int LED_YELLOW = 13;
const int LED_GREEN  = 12;

// ---------------- SINIR AYARLARI ----------------
const float AKIM_LIMIT_MAX_PWM   = 270.0; // mA
const float AKIM_LIMIT_MIN_PWM   = 75.0;  // mA

const float WOBBLE_LIMIT_MAX_PWM = 60.0;  // deg/s
const float WOBBLE_LIMIT_MIN_PWM = 15.0;  // deg/s

const float LIMIT_TEMP_C         = 30.0;  // °C
const int   HATA_SURESI          = 5;     // ardışık kaç ölçüm sonra STOP

// ---------------- PINLER ----------------
const int SDA_PIN    = 8;
const int SCL_PIN    = 9;
const uint8_t MPU_ADDR = 0x68;

const int TEMP_PIN   = 1;

const int STBY_PIN   = 4;
const int PWM_PIN    = 5;
const int IN1_PIN    = 6;
const int IN2_PIN    = 7;

const int ENC_A_PIN  = 10;
const int ENC_B_PIN  = 11;

const float ENCODER_CPR     = 360.0;
const int   UPDATE_INTERVAL = 1000; // ms

// ---------------- ESP32 LEDC (PWM) ----------------
// ESP32-S3 için analogWrite yerine LEDC daha stabil
const int PWM_CH   = 0;
const int PWM_FREQ = 20000; // 20 kHz
const int PWM_RES  = 8;     // 0-255

// ---------------- Sensörler ----------------
Adafruit_INA219 ina219;
OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

// ---------------- GLOBAL ----------------
volatile long encoderCount = 0;
unsigned long lastTime = 0;

float rpm = 0;
int motorPWM = 0;

int hataSayaci = 0;
float sicaklikC = 0;
float avgCurrent = 0;
float yalpalamaDegeri = 0;

float anlikAkimLimiti = 0;
float anlikWobbleLimiti = 0;

String durumEtiketi = "[ BEKLEMEDE ]";
bool korumaKilit = false;
int hataTuru = 0;

// LED blink
unsigned long blinkTimer = 0;
bool ledState = false;

// ---------------- HTML (Dashboard) ----------------
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html lang="tr">
<head>
  <meta charset="UTF-8">
  <title>Motor Sağlığı İzleme</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">

  <script src="/chart.min.js"></script>

  <style>
    :root{
      --bg:#0d1321;
      --card:#111827;
      --primary:#00ff9d;
      --primary-dim: rgba(0,255,157,.2);
      --danger:#ff003c;
      --danger-dim: rgba(255,0,60,.2);
      --warning:#ffbd00;
      --text:#e0f2fe;
      --muted:#94a3b8;
    }
    body{
      font-family: 'Segoe UI','Roboto',sans-serif;
      background: var(--bg);
      color: var(--text);
      margin:0;
      padding:20px;
      padding-bottom:80px;
      display:flex;
      flex-direction:column;
      align-items:center;
      min-height:100vh;
    }
    h1{
      color: var(--primary);
      text-transform: uppercase;
      letter-spacing:3px;
      margin: 0 0 25px 0;
      font-size: 2rem;
      font-weight: 900;
      text-shadow:0 0 10px var(--primary),0 0 20px var(--primary-dim);
      text-align:center;
      width:100%;
    }
    .container{
      display:grid;
      grid-template-columns: repeat(auto-fit, minmax(240px,1fr));
      gap:20px;
      width:100%;
      max-width: 1200px;
    }
    .card{
      background: var(--card);
      border-radius: 14px;
      padding: 22px;
      border: 1px solid var(--primary-dim);
      box-shadow: 0 0 20px -6px var(--primary-dim), inset 0 0 20px -10px #000;
      backdrop-filter: blur(10px);
      transition: .2s;
      position: relative;
      overflow: hidden;
    }
    .card:hover{
      transform: translateY(-4px);
      border-color: var(--primary);
      box-shadow: 0 0 30px -6px var(--primary), inset 0 0 20px -10px #000;
    }
    .card h2{
      margin:0 0 14px 0;
      font-size:.9rem;
      letter-spacing:1.5px;
      color: var(--muted);
      text-transform: uppercase;
      display:flex;
      align-items:center;
      gap:10px;
    }
    .value{
      font-size: 3.0rem;
      font-weight: 900;
      text-align:center;
      letter-spacing: 1px;
      text-shadow: 0 0 10px var(--primary-dim);
      font-variant-numeric: tabular-nums;
    }
    .progress-bg{
      width:100%;
      height:6px;
      background:#0a0f1d;
      border-radius: 999px;
      overflow:hidden;
      margin-top:14px;
      border:1px solid rgba(255,255,255,.05);
      box-shadow: inset 0 1px 3px rgba(0,0,0,.8);
    }
    .progress-fill{
      height:100%;
      width:0%;
      background: var(--primary);
      transition: width .25s ease;
      box-shadow: 0 0 15px currentColor;
      border-radius: 999px;
    }
    .limit-text{
      margin-top: 8px;
      color: var(--muted);
      font-size: .78rem;
      text-align:right;
      font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
    }
    .control-panel{
      grid-column: 1 / -1;
      background: linear-gradient(90deg, #1a233a, #111827);
    }
    .control-row{
      display:flex;
      gap:12px;
      flex-wrap:wrap;
      justify-content:center;
      align-items:center;
    }
    input[type=number]{
      background:#0a0f1d;
      border: 1px solid var(--primary-dim);
      color: var(--primary);
      padding: 14px 18px;
      border-radius: 10px;
      font-size: 1.1rem;
      width: 180px;
      text-align:center;
      box-shadow: inset 0 0 10px rgba(0,0,0,.5);
      font-weight: 800;
    }
    input[type=number]:focus{
      outline:none;
      border-color: var(--primary);
      box-shadow: 0 0 20px var(--primary-dim), inset 0 0 5px rgba(0,0,0,.8);
    }
    .btn{
      padding: 14px 28px;
      border-radius: 10px;
      border: 1px solid transparent;
      font-weight: 900;
      cursor:pointer;
      letter-spacing:2px;
      text-transform: uppercase;
      font-size:.9rem;
      transition:.15s;
    }
    .btn-set{
      background: var(--primary);
      color:#000;
      box-shadow: 0 0 20px var(--primary-dim);
    }
    .btn-set:hover{
      background:#fff;
      color: var(--primary);
      border-color: var(--primary);
      box-shadow: 0 0 30px var(--primary);
    }
    .btn-stop{
      background: var(--danger);
      color:#fff;
      box-shadow: 0 0 20px var(--danger-dim);
    }
    .btn-stop:hover{
      background:#fff;
      color: var(--danger);
      border-color: var(--danger);
      box-shadow: 0 0 30px var(--danger);
    }
    .preset-btn{
      background: transparent;
      border:1px solid var(--muted);
      color: var(--muted);
      padding:8px 14px;
      border-radius: 999px;
      cursor:pointer;
      text-transform: uppercase;
      letter-spacing: 1px;
      font-size:.8rem;
      transition:.15s;
    }
    .preset-btn:hover{
      border-color: var(--primary);
      color: var(--primary);
      box-shadow: 0 0 10px var(--primary-dim);
    }

    /* STATUS KARTI */
    #status-card{
      grid-column: 1 / -1;
      display:flex;
      align-items:center;
      justify-content:space-between;
      gap:16px;
      background: linear-gradient(90deg, var(--card), rgba(0,0,0,.85));
      border-left: 4px solid var(--muted);
    }
    .status-left{
      display:flex;
      align-items:center;
      gap:18px;
    }
    .dot{
      width:18px;
      height:18px;
      border-radius:50%;
      background: var(--muted);
      box-shadow: 0 0 10px currentColor;
    }
    .pulsing{ animation:pulse 1.3s infinite; }
    @keyframes pulse{
      0%{ transform:scale(1); opacity:1;}
      50%{ transform:scale(1.2); opacity:.65;}
      100%{ transform:scale(1); opacity:1;}
    }

    .status-title{ font-size:.9rem; color: var(--muted); letter-spacing:1.5px; text-transform: uppercase;}
    .status-text{ font-size:1.55rem; font-weight: 900; letter-spacing:1px; text-shadow:0 0 10px currentColor;}

    /* OFFLINE */
    .offline{
      filter: grayscale(100%) opacity(.5);
      pointer-events:none;
    }

    /* ERROR highlight */
    .card-error{
      border-color: var(--danger) !important;
      box-shadow: 0 0 35px -6px var(--danger), inset 0 0 20px -10px #000 !important;
    }

    .footer{
      position:fixed;
      left:0; bottom:0;
      width:100%;
      padding:14px 0;
      text-align:center;
      background: rgba(13,19,33,.9);
      backdrop-filter: blur(8px);
      border-top:1px solid rgba(255,255,255,.05);
      color: var(--muted);
      font-size:.8rem;
      letter-spacing:3px;
      z-index:100;
      box-shadow: 0 -5px 20px rgba(0,0,0,.35);
    }
    .footer strong{
      color: var(--primary);
      text-shadow: 0 0 10px var(--primary-dim);
      transition:.2s;
    }
    .footer:hover strong{
      color:#fff;
      letter-spacing:5px;
      text-shadow: 0 0 20px var(--primary);
    }

    /* --- GRAFİK ALANI BÜYÜTME --- */
    .chart-card{
      grid-column: 1 / -1;
    }
    .chart-wrapper {
      position: relative;
      height: 450px; /* Grafiği yükselttik */
      width: 100%;
    }
    canvas {
        display: block;
        width: 100%;
        height: 100%;
    }
  </style>
</head>

<body>
  <h1>MOTOR SAĞLIĞI İZLEME</h1>

  <div class="container" id="main-container">
    <div class="card" id="status-card">
      <div class="status-left">
        <div id="status-dot" class="dot"></div>
        <div>
          <div class="status-title">SİSTEM DURUMU</div>
          <div id="status" class="status-text" style="color: var(--muted);">BAĞLANIYOR...</div>
        </div>
      </div>
      <div style="text-align:right;">
        <div class="status-title">ÇALIŞMA SÜRESİ</div>
        <div id="uptime" class="status-text" style="font-size:1.2rem; color: var(--primary);">00:00:00</div>
      </div>
    </div>

    <div class="card control-panel">
      <h2 style="width:100%; border-bottom:1px solid var(--primary-dim); padding-bottom:12px;">
        KONTROL PANELİ
      </h2>

      <div class="control-row">
        <input type="number" id="pwmInput" placeholder="PWM (0-255)" min="0" max="255">
        <button class="btn btn-set" onclick="setPWM()">BAŞLAT</button>
        <button class="btn btn-stop" onclick="stopMotor()">ACİL DURDUR</button>
      </div>

      <div class="control-row" style="margin-top:10px;">
        <button class="preset-btn" onclick="quickSet(60)">DÜŞÜK (60)</button>
        <button class="preset-btn" onclick="quickSet(150)">ORTA (150)</button>
        <button class="preset-btn" onclick="quickSet(255)">TAM GÜÇ (255)</button>
      </div>

      <div class="limit-text" style="text-align:center;">
        WebSocket: <span id="wsState" style="color:var(--muted); font-weight:900;">Bağlanıyor...</span>
      </div>
    </div>

    <div class="card" id="card-rpm">
      <h2>HIZ (RPM)</h2>
      <div class="value" id="rpm">0</div>
      <div class="progress-bg"><div id="bar-rpm" class="progress-fill"></div></div>
      <div class="limit-text">PWM: <span id="pwm" style="color:var(--primary); font-weight:900;">0</span></div>
    </div>

    <div class="card" id="card-curr">
      <h2>AKIM (mA)</h2>
      <div class="value" id="current">0</div>
      <div class="progress-bg"><div id="bar-curr" class="progress-fill"></div></div>
      <div class="limit-text">Limit: <span id="currLim">0</span> mA</div>
    </div>

    <div class="card" id="card-wob">
      <h2>YALPALAMA (deg/s)</h2>
      <div class="value" id="wobble">0.0</div>
      <div class="progress-bg"><div id="bar-wob" class="progress-fill"></div></div>
      <div class="limit-text">Limit: <span id="wobLim">0.0</span> deg/s</div>
    </div>

    <div class="card" id="card-temp">
      <h2>SICAKLIK (°C)</h2>
      <div class="value" id="temp">0.0</div>
      <div class="progress-bg"><div id="bar-temp" class="progress-fill"></div></div>
      <div class="limit-text">Limit: 30.0 °C</div>
    </div>

    <div class="card chart-card">
      <h2>GERÇEK ZAMANLI GRAFİK (Son 60 sn)</h2>
      <div class="chart-wrapper">
          <canvas id="liveChart"></canvas>
      </div>
      <div class="limit-text">RPM / Akım / Yalpalama / Sıcaklık + Limit çizgileri</div>
    </div>

  </div>

  <div class="footer">DESIGNED BY <strong>MERT UZUN</strong></div>

<script>
let lastUpdate = Date.now();
let socket;

function setPWM(){
  const val = document.getElementById("pwmInput").value;
  if(val === "") return;
  fetch("/set-pwm?val=" + val).then(_ => document.getElementById("pwmInput").value = "");
}
function quickSet(val){
  document.getElementById("pwmInput").value = val;
  setPWM();
}
function stopMotor(){
  fetch("/set-pwm?val=0");
  document.getElementById("pwmInput").value = "0";
}

function updateBar(barId, val, limit){
  const bar = document.getElementById(barId);
  let pct = (limit > 0) ? (val / limit) * 100 : 0;
  if(pct > 100) pct = 100;
  bar.style.width = pct + "%";

  if(limit > 0 && val > limit){
    bar.style.backgroundColor = "var(--danger)";
    bar.style.boxShadow = "0 0 20px var(--danger)";
  } else if(limit > 0 && val > limit * 0.8){
    bar.style.backgroundColor = "var(--warning)";
    bar.style.boxShadow = "0 0 15px var(--warning)";
  } else {
    bar.style.backgroundColor = "var(--primary)";
    bar.style.boxShadow = "0 0 15px var(--primary)";
  }
}

// ------------------ UPTIME ------------------
let startTs = Date.now();
function fmtTime(ms){
  const s = Math.floor(ms/1000);
  const hh = String(Math.floor(s/3600)).padStart(2,'0');
  const mm = String(Math.floor((s%3600)/60)).padStart(2,'0');
  const ss = String(s%60).padStart(2,'0');
  return `${hh}:${mm}:${ss}`;
}
setInterval(()=> {
  document.getElementById("uptime").innerText = fmtTime(Date.now() - startTs);
}, 500);

// ------------------ CHART ------------------
const ctx = document.getElementById("liveChart").getContext("2d");
const MAX_POINTS = 60;

const chartData = {
  labels: [],
  datasets: [
    { label: "RPM", data: [], tension: 0.3, borderWidth: 2, pointRadius: 0 },
    { label: "Akım (mA)", data: [], tension: 0.3, borderWidth: 2, pointRadius: 0 },
    { label: "Yalpalama (deg/s)", data: [], tension: 0.3, borderWidth: 2, pointRadius: 0 },
    { label: "Sıcaklık (°C)", data: [], tension: 0.3, borderWidth: 2, pointRadius: 0 },
    { label: "Akım Limit", data: [], borderDash: [6,6], borderWidth: 1, pointRadius: 0 },
    { label: "Wobble Limit", data: [], borderDash: [6,6], borderWidth: 1, pointRadius: 0 },
  ]
};

const liveChart = new Chart(ctx, {
  type: "line",
  data: chartData,
  options: {
    responsive: true,
    maintainAspectRatio: false, // ÖNEMLİ: CSS'teki yüksekliğe uyması için
    animation: false,
    plugins: {
      legend: { labels: { color: "#e0f2fe" } }
    },
    scales: {
      x: { ticks: { color: "#94a3b8" }, grid: { color: "rgba(148,163,184,0.1)" } },
      y: { ticks: { color: "#94a3b8" }, grid: { color: "rgba(148,163,184,0.1)" } }
    }
  }
});

// ------------------ WATCHDOG (OFFLINE) ------------------
setInterval(function(){
  if(Date.now() - lastUpdate > 2500){
    document.getElementById("status").innerText = "BAĞLANTI KOPTU!";
    document.getElementById("status").style.color = "#888";
    document.getElementById("status-card").style.borderLeftColor = "#555";
    document.getElementById("status-dot").style.backgroundColor = "#555";
    document.getElementById("main-container").classList.add("offline");
    document.getElementById("status-card").classList.remove("offline");
    document.getElementById("wsState").innerText = "Koptu";
  } else {
    document.getElementById("main-container").classList.remove("offline");
  }
}, 1000);

// ------------------ UI STATUS RENDER ------------------
function renderStatus(data){
  const statusEl = document.getElementById("status");
  const statusCard = document.getElementById("status-card");
  const dot = document.getElementById("status-dot");

  // kart error temizle
  document.getElementById("card-curr").classList.remove("card-error");
  document.getElementById("card-wob").classList.remove("card-error");
  document.getElementById("card-temp").classList.remove("card-error");

  dot.className = "dot"; // reset
  statusCard.style.color = "var(--text)";
  statusCard.style.borderLeftColor = "var(--muted)";

  const gelenDurum = (data.status || "");
  const hata = Number(data.err || 0);

  if(gelenDurum.includes("NORMAL")){
    statusEl.innerText = "SİSTEM NORMAL";
    statusEl.style.color = "var(--primary)";
    statusCard.style.borderLeftColor = "var(--primary)";
    dot.style.backgroundColor = "var(--primary)";
    dot.classList.add("pulsing");
  }
  else if(gelenDurum.includes("UYARI") || gelenDurum.includes("WARNING")){
    statusEl.innerText = "DİKKAT: LİMİT UYARISI";
    statusEl.style.color = "var(--warning)";
    statusCard.style.borderLeftColor = "var(--warning)";
    dot.style.backgroundColor = "var(--warning)";
    dot.classList.add("pulsing");
  }
  else if(gelenDurum.includes("STOP") || gelenDurum.includes("KORUMA")){
    let msg = "ACİL DURDURULDU";
    if(hata === 1){
      msg = "DURDURULDU: AŞIRI AKIM";
      document.getElementById("card-curr").classList.add("card-error");
    } else if(hata === 2){
      msg = "DURDURULDU: YÜKSEK YALPALAMA";
      document.getElementById("card-wob").classList.add("card-error");
    } else if(hata === 3){
      msg = "DURDURULDU: AŞIRI SICAKLIK";
      document.getElementById("card-temp").classList.add("card-error");
    }
    statusEl.innerText = msg;
    statusEl.style.color = "var(--danger)";
    statusCard.style.borderLeftColor = "var(--danger)";
    dot.style.backgroundColor = "var(--danger)";
    dot.classList.add("pulsing");
  }
  else{
    statusEl.innerText = "BEKLEME MODU";
    statusEl.style.color = "var(--muted)";
    dot.style.backgroundColor = "var(--muted)";
  }
}

// ------------------ CHART PUSH ------------------
function pushChart(data){
  const t = new Date().toLocaleTimeString();

  chartData.labels.push(t);
  chartData.datasets[0].data.push(Number(data.rpm || 0));
  chartData.datasets[1].data.push(Number(data.current || 0));
  chartData.datasets[2].data.push(Number(data.wobble || 0));
  chartData.datasets[3].data.push(Number(data.temp || 0));

  // limit çizgileri: her noktaya aynı limit değerini bas
  chartData.datasets[4].data.push(Number(data.currLim || 0));
  chartData.datasets[5].data.push(Number(data.wobLim || 0));

  if(chartData.labels.length > MAX_POINTS){
    chartData.labels.shift();
    chartData.datasets.forEach(ds => ds.data.shift());
  }
  liveChart.update();
}

// ------------------ DATA RENDER ------------------
function renderData(data){
  lastUpdate = Date.now();

  document.getElementById("rpm").innerText = Number(data.rpm || 0);
  document.getElementById("pwm").innerText = Number(data.pwm || 0);

  document.getElementById("current").innerText = Number(data.current || 0).toFixed(0);
  document.getElementById("currLim").innerText = Number(data.currLim || 0);

  document.getElementById("wobble").innerText = Number(data.wobble || 0).toFixed(1);
  document.getElementById("wobLim").innerText = Number(data.wobLim || 0).toFixed(1);

  document.getElementById("temp").innerText = Number(data.temp || 0).toFixed(1);

  // barlar
  updateBar("bar-rpm", Number(data.rpm || 0), 635);
  updateBar("bar-curr", Number(data.current || 0), Number(data.currLim || 0));
  updateBar("bar-wob", Number(data.wobble || 0), Number(data.wobLim || 0));
  updateBar("bar-temp", Number(data.temp || 0), 30.0);

  renderStatus(data);
  pushChart(data);
}

// ------------------ WEBSOCKET ------------------
function connectWS(){
  const url = "ws://" + location.host + "/ws";
  socket = new WebSocket(url);

  socket.onopen = () => {
    document.getElementById("wsState").innerText = "Bağlı";
    document.getElementById("wsState").style.color = "var(--primary)";
  };

  socket.onclose = () => {
    document.getElementById("wsState").innerText = "Koptu (yeniden deniyor)";
    document.getElementById("wsState").style.color = "var(--warning)";
    setTimeout(connectWS, 800);
  };

  socket.onerror = () => {
    // hata durumunda da close tetiklenir
  };

  socket.onmessage = (event) => {
    try{
      const data = JSON.parse(event.data);
      renderData(data);
    }catch(e){}
  };
}

connectWS();
</script>
</body>
</html>
)rawliteral";

// ---------------- Encoder ISR ----------------
void IRAM_ATTR readEncoder() {
  if (digitalRead(ENC_B_PIN) == digitalRead(ENC_A_PIN)) encoderCount++;
  else encoderCount--;
}

// ---------------- Helpers ----------------
void setLEDs(bool r, bool y, bool g) {
  digitalWrite(LED_RED, r);
  digitalWrite(LED_YELLOW, y);
  digitalWrite(LED_GREEN, g);
}

float lerp(float a, float b, float t) {
  return a + (b - a) * t;
}

// ---------------- WebSocket event ----------------
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
               AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("WS connect: #%u\n", client->id());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("WS disconnect: #%u\n", client->id());
  }
}

// ---------------- WS JSON push ----------------
void sendWSData() {
  String json = "{";
  json += "\"rpm\":" + String((int)rpm) + ",";
  json += "\"pwm\":" + String(motorPWM) + ",";
  json += "\"current\":" + String(avgCurrent) + ",";
  json += "\"currLim\":" + String((int)anlikAkimLimiti) + ",";
  json += "\"wobble\":" + String(yalpalamaDegeri) + ",";
  json += "\"wobLim\":" + String(anlikWobbleLimiti) + ",";
  json += "\"temp\":" + String(sicaklikC) + ",";
  json += "\"err\":" + String(hataTuru) + ",";
  json += "\"status\":\"" + durumEtiketi + "\"";
  json += "}";
  ws.textAll(json);
}

// ---------------- Setup ----------------
void setup() {
  Serial.begin(115200);
  delay(100);

  // WiFi SoftAP
  Serial.println("\nWiFi Baslatiliyor...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  delay(100);

  Serial.println("----------------------------------------");
  Serial.print("AG ADI: "); Serial.println(ssid);
  Serial.print("SIFRE:  "); Serial.println(password);
  Serial.print("IP ADRESI: "); Serial.println(WiFi.softAPIP());
  Serial.println("----------------------------------------");

  // Pinler
  pinMode(STBY_PIN, OUTPUT);
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);

  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  setLEDs(0,0,0);

  // Motor yön / standby
  digitalWrite(STBY_PIN, HIGH);
  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, LOW);

  // PWM (LEDC)
  ledcSetup(PWM_CH, PWM_FREQ, PWM_RES);
  ledcAttachPin(PWM_PIN, PWM_CH);
  ledcWrite(PWM_CH, motorPWM);

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  if (!ina219.begin()) {
    Serial.println("INA219 Yok!");
  }

  // MPU init
  Wire.beginTransmission(MPU_ADDR); Wire.write((uint8_t)0x6B); Wire.write((uint8_t)0x00); Wire.endTransmission();
  Wire.beginTransmission(MPU_ADDR); Wire.write((uint8_t)0x1A); Wire.write((uint8_t)0x04); Wire.endTransmission();
  Wire.beginTransmission(MPU_ADDR); Wire.write((uint8_t)0x1B); Wire.write((uint8_t)0x08); Wire.endTransmission();

  sensors.begin();

  attachInterrupt(digitalPinToInterrupt(ENC_A_PIN), readEncoder, CHANGE);

  // LittleFS (chart.min.js servis)
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount FAILED!");
  } else {
    Serial.println("LittleFS mounted.");
  }

  // Web routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Chart.js dosyası: /chart.min.js
  server.serveStatic("/chart.min.js", LittleFS, "/chart.min.js")
        .setCacheControl("max-age=86400");

  // (Opsiyonel) data endpoint (debug)
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "{";
    json += "\"rpm\":" + String((int)rpm) + ",";
    json += "\"pwm\":" + String(motorPWM) + ",";
    json += "\"current\":" + String(avgCurrent) + ",";
    json += "\"currLim\":" + String((int)anlikAkimLimiti) + ",";
    json += "\"wobble\":" + String(yalpalamaDegeri) + ",";
    json += "\"wobLim\":" + String(anlikWobbleLimiti) + ",";
    json += "\"temp\":" + String(sicaklikC) + ",";
    json += "\"err\":" + String(hataTuru) + ",";
    json += "\"status\":\"" + durumEtiketi + "\"";
    json += "}";
    request->send(200, "application/json", json);
  });

  // PWM set
  server.on("/set-pwm", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("val")) {
      String inputMessage = request->getParam("val")->value();
      int yeniPWM = inputMessage.toInt();
      if (yeniPWM >= 0 && yeniPWM <= 255) {
        motorPWM = yeniPWM;
        ledcWrite(PWM_CH, motorPWM);

        hataSayaci = 0;
        korumaKilit = false;
        hataTuru = 0;

        Serial.print("Webden PWM: ");
        Serial.println(motorPWM);
      }
    }
    request->send(200, "text/plain", "OK");
  });

  // WebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.begin();
  Serial.println("SISTEM VE WEB KONTROL HAZIR.");
}

// ---------------- Loop ----------------
void loop() {
  // WS client cleanup
  ws.cleanupClients();

  // Seri PWM (opsiyonel)
  if (Serial.available() > 0) {
    int val = Serial.parseInt();
    if (val >= 0 && val <= 255) {
      motorPWM = val;
      ledcWrite(PWM_CH, motorPWM);
      hataSayaci = 0;
      korumaKilit = false;
      hataTuru = 0;
    }
  }

  // LED blink (motor duruyorsa veya koruma kilidi varsa)
  bool blinkMode = (motorPWM == 0) || korumaKilit;
  if (blinkMode) {
    if (millis() - blinkTimer >= 500) {
      blinkTimer = millis();
      ledState = !ledState;
      setLEDs(ledState, 0, 0); // kırmızı blink
    }
  }

  // Ölçüm aralığı
  if (millis() - lastTime >= UPDATE_INTERVAL) {
    // RPM
    long currentCount = encoderCount;
    encoderCount = 0;
    rpm = fabs((float)(currentCount / ENCODER_CPR) * (60000.0 / UPDATE_INTERVAL));

    // Akım ortalama
    float totalCurrent = 0;
    for (int i = 0; i < 10; i++) {
      totalCurrent += ina219.getCurrent_mA();
      delay(1);
    }
    avgCurrent = totalCurrent / 10.0;

    // Yalpalama (gyro)
    float maxWobble = 0;
    for (int i = 0; i < 30; i++) {
      Wire.beginTransmission(MPU_ADDR);
      Wire.write((uint8_t)0x43);
      Wire.endTransmission(false);
      Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)4, (bool)true);

      int16_t GyroX = (Wire.read() << 8 | Wire.read());
      int16_t GyroY = (Wire.read() << 8 | Wire.read());

      float rotX = GyroX / 65.5;
      float rotY = GyroY / 65.5;

      float currentWobble = sqrtf(rotX * rotX + rotY * rotY);
      if (currentWobble > maxWobble) maxWobble = currentWobble;
    }
    yalpalamaDegeri = maxWobble;

    // Sıcaklık
    sensors.requestTemperatures();
    sicaklikC = sensors.getTempCByIndex(0);

    // Dinamik limitler
    if (motorPWM < 50) {
      anlikAkimLimiti = AKIM_LIMIT_MIN_PWM;
      anlikWobbleLimiti = WOBBLE_LIMIT_MIN_PWM;
    } else {
      float t = (float)(motorPWM - 50) / (float)(255 - 50); // 0..1
      anlikAkimLimiti = lerp(AKIM_LIMIT_MIN_PWM, AKIM_LIMIT_MAX_PWM, t);
      anlikWobbleLimiti = lerp(WOBBLE_LIMIT_MIN_PWM, WOBBLE_LIMIT_MAX_PWM, t);
    }

    // Koruma mantığı
    if (!korumaKilit && motorPWM > 0) {
      bool akimHatasi     = (avgCurrent > anlikAkimLimiti);
      bool wobbleHatasi   = (yalpalamaDegeri > anlikWobbleLimiti);
      bool sicaklikHatasi = (sicaklikC > LIMIT_TEMP_C) && (sicaklikC != -127.0);

      if (akimHatasi || wobbleHatasi || sicaklikHatasi) {
        hataSayaci++;
        if (hataSayaci >= HATA_SURESI) {
          if (akimHatasi) hataTuru = 1;
          else if (wobbleHatasi) hataTuru = 2;
          else if (sicaklikHatasi) hataTuru = 3;

          motorPWM = 0;
          ledcWrite(PWM_CH, 0);
          korumaKilit = true;
          durumEtiketi = "[ STOP - KORUMA ]";
        } else {
          setLEDs(0, 1, 0); // sarı
          durumEtiketi = "[ UYARI - LIMIT ]";
        }
      } else {
        hataSayaci = 0;
        setLEDs(0, 0, 1); // yeşil
        durumEtiketi = "[ NORMAL ]";
      }
    } else {
      // motorPWM=0 ise veya korumaKilit ise durum
      if (korumaKilit) durumEtiketi = "[ STOP - KORUMA ]";
      else durumEtiketi = "[ BEKLEMEDE ]";
    }

    // Serial debug
    Serial.print(durumEtiketi);
    Serial.print(" PWM:"); Serial.print(motorPWM);
    Serial.print("|RPM:"); Serial.print((int)rpm);
    Serial.print("|I:"); Serial.print(avgCurrent, 0); Serial.print("/"); Serial.print((int)anlikAkimLimiti);
    Serial.print("|Wob:"); Serial.print(yalpalamaDegeri, 1); Serial.print("/"); Serial.print(anlikWobbleLimiti, 1);
    Serial.print("|Tmp:"); Serial.println(sicaklikC, 1);

    // WebSocket ile UI güncelle
    sendWSData();

    lastTime = millis();
  }
}