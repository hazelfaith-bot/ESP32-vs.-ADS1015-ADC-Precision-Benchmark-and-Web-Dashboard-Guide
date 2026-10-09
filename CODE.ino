/*
  ================================================================
  ESP32 vs. ADS1015 ADC Precision Benchmark and Web Dashboard Guide
  ================================================================
*/

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h> 

#include <Adafruit_ADS1X15.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// PIN DEFINITIONS
#define POT_PIN        34
#define LM35_PIN       35

#define BUZZER_PIN     25
#define YELLOW_LED_PIN 26  // Normal Status LED
#define RED_LED_PIN    27  // High Temp Alert LED

#define SDA_PIN        21
#define SCL_PIN        22

// OLED CONFIGURATION
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledPresent = false;

// ADS1015 CONFIGURATION
#define ADS_ADDRESS 0x48
Adafruit_ADS1015 ads;
bool adsPresent = false;

// SERVERS
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// LOCAL WI-FI ACCESS POINT CREDENTIALS
const char* AP_SSID     = "TECNO CAMON 50 Ultra 5G"; 
const char* AP_PASSWORD = "haizzennn";             

String espIPAddress = "192.168.4.1";

// CONTROL STATES
bool streamRunning = true;  
bool streamPaused  = false; 
bool filterEnabled = false;

const unsigned long SAMPLE_INTERVAL = 30;
unsigned long lastSampleTime = 0;
unsigned long sampleNumber = 0;

// TIMERS & ALERT STATE
unsigned long lastOledUpdate = 0;

// NON-BLOCKING DOUBLE-BEEP TIMING FOR RESUME
unsigned long doubleBeepStartTime = 0;
int doubleBeepStage = 0; // 0 = Idle, 1 = Beep 1, 2 = Silence, 3 = Beep 2

// ADC & TELEMETRY VARIABLES
float espPotVoltage = 0.0f, adsPotVoltage = 0.0f;
float espLM35Voltage = 0.0f, adsLM35Voltage = 0.0f;
float espTemperature = 0.0f, adsTemperature = 0.0f;
float voltageDifference = 0.0f;
bool tempAlarmActive = false;

// ALERT THRESHOLD & HYSTERESIS
const float ALERT_TEMP_THRESHOLD = 40.0f; // Celsius
const float TEMP_HYSTERESIS       = 2.0f;  

// MOVING AVERAGE FILTER
const int FILTER_SIZE = 10;
float espPotBuffer[FILTER_SIZE] = {0}, adsPotBuffer[FILTER_SIZE] = {0};

// SHARED WAVEFORM HISTORY BUFFER
const int HISTORY_SIZE = 40;
float espHistory[HISTORY_SIZE] = {0};
float adsHistory[HISTORY_SIZE] = {0};
int historyIndex = 0;

// MAIN DASHBOARD HTML
const char MAIN_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ADC Precision Benchmarking</title>
<style>
:root{--bg:#0b0f19;--card:#151c2e;--border:#243049;--accent:#38bdf8;--green:#4ade80;--red:#f87171;--amber:#fbbf24;--purple:#c084fc;}
body{font-family:'Segoe UI',Roboto,sans-serif;background:var(--bg);color:#f8fafc;margin:0;padding:16px;}
.header{border-bottom:1px solid var(--border);padding-bottom:12px;margin-bottom:16px;}
h1{font-size:20px;margin:0;color:#ffffff;display:flex;align-items:center;gap:8px;}
.subtitle-bar{background:#111827;border:1px solid var(--border);border-radius:6px;padding:8px 12px;margin-top:10px;font-size:11px;color:#94a3b8;line-height:1.6;}
.subtitle-bar b{color:#cbd5e1;}

.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;}
.card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:12px;box-shadow:0 4px 6px -1px rgba(0,0,0,0.3);}
.lbl{color:#94a3b8;font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:0.5px;}
.val{font-size:22px;font-weight:700;margin-top:4px;color:#fff;}
.unit{font-size:12px;color:#94a3b8;font-weight:normal;}

.control-header{font-size:10px;font-weight:700;color:var(--green);letter-spacing:0.5px;margin-bottom:8px;text-transform:uppercase;}
.btn-group{display:flex;flex-wrap:wrap;gap:8px;}
button{padding:8px 16px;border:0;border-radius:6px;cursor:pointer;font-weight:700;font-size:11px;display:inline-flex;align-items:center;gap:6px;transition:all 0.15s ease-in-out;}
button:hover{transform:translateY(-1px);filter:brightness(1.15);}
button:active{transform:scale(0.97);}
.btn-start{background:#16a34a;color:#fff;}
.btn-stop{background:#dc2626;color:#fff;}
.btn-amber{background:#d97706;color:#fff;}
.btn-blue{background:#2563eb;color:#fff;}
.btn-purple{background:#9333ea;color:#fff;}
.btn-gray{background:#334155;color:#fff;}

.dot{width:10px;height:10px;border-radius:50%;display:inline-block;background:var(--green);box-shadow:0 0 8px var(--green);}
.dot.red{background:var(--red);box-shadow:0 0 8px var(--red);}

.chart-card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:14px;margin-top:10px;}
.legend-container{display:flex;gap:20px;align-items:center;margin-bottom:8px;}
.legend-item{display:flex;align-items:center;gap:6px;font-size:11px;font-weight:600;}
.legend-box{width:12px;height:12px;border-radius:3px;}

canvas{width:100%;height:320px;background:#080c14;border-radius:8px;border:1px solid var(--border);display:block;}

.benchmark-card{background:var(--card);border:1px solid var(--border);border-radius:10px;padding:14px;margin-top:10px;}
.bm-title{font-size:14px;font-weight:700;color:#fff;margin-bottom:12px;display:flex;justify-content:space-between;align-items:center;}
.bm-badge{background:#1e3a8a;color:#60a5fa;font-size:10px;font-weight:700;padding:4px 8px;border-radius:4px;border:1px solid #1d4ed8;}
.bm-grid{display:grid;grid-template-columns:1fr 1fr;gap:16px;}
.bm-sub{font-size:11px;color:#94a3b8;margin-bottom:6px;font-weight:600;}
.bm-val{font-size:18px;font-weight:700;color:#fff;}
</style>
</head>
<body>

<div class="header">
  <h1>ESP32 vs ADS1015 Precision ADC Benchmark</h1>
  <div class="subtitle-bar">
    <b>Base Tutorial & Side-by-Side ADC Architecture</b><br>
    <b>Direct Sensor Read:</b> ESP32 onboard ADC (Pin 34 & 35) vs External 12-Bit ADS1015 over I2C.<br>
    <b>Multi-Device Sync:</b> Control state is stored on the ESP32 server. Any state change on one PC/phone instantly reflects across all connected client dashboards!
  </div>
</div>

<div class="grid">
  <div class="card"><div class="lbl">ESP32 Potentiometer</div><div class="val"><span id="espV">0.000</span> <span class="unit">V</span></div></div>
  <div class="card"><div class="lbl">ADS1015 Potentiometer</div><div class="val" style="color:var(--accent);"><span id="adsV">0.000</span> <span class="unit">V</span></div></div>
  <div class="card"><div class="lbl">Absolute Delta</div><div class="val"><span id="diff">0.0</span> <span class="unit">mV</span></div></div>
  <div class="card"><div class="lbl">ESP32 LM35</div><div class="val"><span id="espT">0.0</span> <span class="unit">&deg;C</span></div></div>
  <div class="card"><div class="lbl">ADS1015 LM35</div><div class="val" style="color:var(--accent);"><span id="adsT">0.0</span> <span class="unit">&deg;C</span></div></div>
  <div class="card"><div class="lbl">Total Samples</div><div class="val"><span id="samples">0</span></div></div>
</div>

<div class="grid" style="margin-top:10px">
  <div class="card"><div class="lbl">Thermal Status</div><div style="margin-top:4px;"><span id="led" class="dot"></span> <b id="stText" style="font-size:13px;margin-left:6px;">NORMAL</b></div></div>
  <div class="card"><div class="lbl">Buzzer Alert</div><div class="val" id="buzzer">OFF</div></div>
  <div class="card"><div class="lbl">Stream State</div><div class="val" id="streamSt" style="color:var(--green);">RUNNING</div></div>
  <div class="card"><div class="lbl">Filter Engine</div><div class="val" id="filterSt">DISABLED</div></div>
</div>

<div class="card" style="margin-top:10px">
  <div class="control-header">AUTOMATICALLY SYNCHRONIZED CONTROLS</div>
  <div class="btn-group">
    <button class="btn-start" onclick="execCmd('/start')">START</button>
    <button class="btn-stop" onclick="execCmd('/stop')">STOP</button>
    <button id="pauseBtn" class="btn-amber" onclick="execCmd('/togglePause')">PAUSE</button>
    <button class="btn-blue" onclick="execCmd('/reset')">RESET</button>
    <button class="btn-purple" onclick="execCmd('/filter?state=on')">FILTER ON</button>
    <button class="btn-gray" onclick="execCmd('/filter?state=off')">FILTER OFF</button>
  </div>
</div>

<div class="chart-card">
  <div class="legend-container">
    <div class="legend-item"><div class="legend-box" style="background:#4ade80;"></div>ESP32 Direct ADC (Raw Noise)</div>
    <div class="legend-item"><div class="legend-box" style="background:#38bdf8;"></div>ADS1015 External ADC (Precision)</div>
  </div>
  <canvas id="chart" width="800" height="320"></canvas>
</div>

<div class="benchmark-card">
  <div class="bm-title">
    <span>Comparative "Which ADC Should You Trust" Report</span>
    <span class="bm-badge">RECOMMENDED: ADS1015 External I2C ADC</span>
  </div>
  <div class="bm-grid">
    <div>
      <div class="bm-sub">ESP32 PEAK-JITTER (W-P)</div>
      <div class="bm-val"><span id="espJitter">0.0</span> <span class="unit">mV</span></div>
      <div class="bm-sub" style="margin-top:12px;">ESP32 RMS NOISE (STDDEV)</div>
      <div class="bm-val"><span id="espRms">0.0</span> <span class="unit">mV</span></div>
    </div>
    <div>
      <div class="bm-sub">ADS1015 PEAK-JITTER (W-P)</div>
      <div class="bm-val"><span id="adsJitter">0.0</span> <span class="unit">mV</span></div>
      <div class="bm-sub" style="margin-top:12px;">ADS1015 RMS NOISE (STDDEV)</div>
      <div class="bm-val"><span id="adsRms">0.0</span> <span class="unit">mV</span></div>
    </div>
  </div>
  <div style="font-size:11px;color:#94a3b8;margin-top:12px;line-height:1.5;">
    <b>Engineering Analysis:</b> Built-in ESP32 ADC exhibits noise floor fluctuations. The ADS1015 offers PGA precision and low noise.
  </div>
</div>

<script>
let E = [], A = [];
let ws;

function $(id){ return document.getElementById(id); }

function initWebSocket() {
  ws = new WebSocket('ws://' + window.location.hostname + ':81/');
  ws.onmessage = function(event) {
    let d = JSON.parse(event.data);
    
    $("espV").textContent = d.espPotV.toFixed(3);
    $("adsV").textContent = d.adsPotV.toFixed(3);
    $("diff").textContent = (d.diff * 1000).toFixed(1);
    $("espT").textContent = d.espTemp.toFixed(1);
    $("adsT").textContent = d.adsTemp.toFixed(1);
    $("samples").textContent = d.sample;

    $("filterSt").textContent = d.filter ? "ACTIVE" : "DISABLED";
    $("filterSt").style.color = d.filter ? "#c084fc" : "#94a3b8";

    let st = $("streamSt");
    let btn = $("pauseBtn");
    
    if (d.running) {
      st.textContent = "RUNNING"; st.style.color = "#4ade80";
      btn.textContent = "PAUSE"; btn.className = "btn-amber";
    } else if (d.paused) {
      st.textContent = "PAUSED"; st.style.color = "#fbbf24";
      btn.textContent = "RESUME"; btn.className = "btn-blue";
    } else {
      st.textContent = "STOPPED"; st.style.color = "#f87171";
      btn.textContent = "PAUSE"; btn.className = "btn-amber";
    }

    let alarm = d.alarm;
    $("led").className = alarm ? "dot red" : "dot";
    $("stText").textContent = alarm ? "HIGH TEMP ALERT" : "NORMAL";
    $("buzzer").textContent = alarm ? "ALERT BEEP" : "OFF";

    E = d.espHist;
    A = d.adsHist;
    calculateStats();
    draw();
  };
  ws.onclose = function() { setTimeout(initWebSocket, 1000); };
}

async function execCmd(url){ 
  try{ await fetch(url, {cache:"no-store"}); }catch(e){} 
}

function calculateStats(){
  if(!E || E.length === 0) return;
  
  let espMin = Math.min(...E), espMax = Math.max(...E);
  let adsMin = Math.min(...A), adsMax = Math.max(...A);
  
  $("espJitter").textContent = ((espMax - espMin) * 1000).toFixed(1);
  $("adsJitter").textContent = ((adsMax - adsMin) * 1000).toFixed(1);

  let espAvg = E.reduce((a,b)=>a+b,0)/E.length;
  let adsAvg = A.reduce((a,b)=>a+b,0)/A.length;

  let espVariance = E.reduce((a,b)=>a+Math.pow(b-espAvg,2),0)/E.length;
  let adsVariance = A.reduce((a,b)=>a+Math.pow(b-adsAvg,2),0)/A.length;

  $("espRms").textContent = (Math.sqrt(espVariance) * 1000).toFixed(1);
  $("adsRms").textContent = (Math.sqrt(adsVariance) * 1000).toFixed(1);
}

function draw(){
  let cv = $("chart"), ctx = cv.getContext("2d"), w = cv.width, h = cv.height;
  ctx.clearRect(0, 0, w, h);

  let leftM = 50, rightM = 100, topM = 50, botM = 40;
  let plotW = w - leftM - rightM;
  let plotH = h - topM - botM;

  if(!E || E.length < 2) return;

  let all = E.concat(A), min = Math.min(...all), max = Math.max(...all);
  if(max - min < 0.05){ max += 0.025; min -= 0.025; }

  // Y-AXIS GRID LINES & NUMERIC VOLTAGE VALUES
  ctx.textAlign = "right"; 
  ctx.font = "10px sans-serif";
  for(let i = 0; i <= 4; i++){
    let yVal = min + (max - min) * (i / 4);
    let yPos = topM + plotH - (plotH * (i / 4));
    
    ctx.strokeStyle = "#1e293b"; 
    ctx.lineWidth = 1;
    ctx.beginPath(); 
    ctx.moveTo(leftM, yPos); 
    ctx.lineTo(w - rightM, yPos); 
    ctx.stroke();

    ctx.fillStyle = "#94a3b8"; 
    ctx.fillText(yVal.toFixed(3) + "V", leftM - 8, yPos + 3);
  }

  // X-AXIS TIME LABELS & SAMPLE INDEX GRID
  ctx.textAlign = "center";
  ctx.font = "10px sans-serif";
  let totalPoints = E.length;

  for(let i = 0; i < totalPoints; i++){
    let xPos = leftM + i * plotW / Math.max(1, totalPoints - 1);
    
    // Vertical Sample Grid Lines
    ctx.strokeStyle = "rgba(30, 41, 59, 0.4)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(xPos, topM);
    ctx.lineTo(xPos, h - botM);
    ctx.stroke();
  }

  let xTicks = ["-576ms", "-420ms", "-228ms", "NOW"];
  xTicks.forEach((lbl, idx) => {
    let xPos = leftM + (plotW * (idx / (xTicks.length - 1)));
    ctx.fillStyle = "#64748b";
    ctx.fillText(lbl, xPos, h - botM + 16);
  });

  // WAVEFORM PLOTTING WITH NUMERIC VALUE PER SAMPLE (X & Y)
  function drawLineAndValues(arr, col, yTextOffset){
    ctx.strokeStyle = col; 
    ctx.lineWidth = 2; 
    ctx.beginPath();

    let points = [];
    arr.forEach((v, i) => {
      let x = leftM + i * plotW / (arr.length - 1);
      let y = topM + (max - v) * plotH / (max - min);
      if(i === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
      points.push({x: x, y: y, val: v, sampleIdx: i});
    });
    ctx.stroke();

    // Render Data Point Circle & Numeric Text Labels Per Sample Point
    points.forEach((p) => {
      ctx.fillStyle = col; 
      ctx.beginPath(); 
      ctx.arc(p.x, p.y, 2.5, 0, Math.PI * 2); 
      ctx.fill();

      // Render text label every 2 samples to keep graph readable
      if (p.sampleIdx % 2 === 0 || p.sampleIdx === totalPoints - 1) {
        ctx.fillStyle = col;
        ctx.font = "bold 8px sans-serif";
        ctx.textAlign = "center";
        ctx.fillText(p.val.toFixed(3), p.x, p.y + yTextOffset);
      }
    });
  }

  drawLineAndValues(E, "#4ade80", -7);
  drawLineAndValues(A, "#38bdf8", 12);

  // DYNAMIC OVERLAY LEGEND CARD
  let latestE = E[E.length - 1] || 0;
  let latestA = A[A.length - 1] || 0;
  let delta = Math.abs(latestE - latestA) * 1000;

  ctx.fillStyle = "rgba(15, 23, 42, 0.85)";
  ctx.strokeStyle = "#334155";
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.roundRect(leftM + 10, topM + 5, 250, 24, 6);
  ctx.fill();
  ctx.stroke();

  ctx.textAlign = "left";
  ctx.font = "bold 10px sans-serif";
  
  ctx.fillStyle = "#4ade80";
  ctx.fillText("ESP32: " + latestE.toFixed(3) + "V", leftM + 18, topM + 21);

  ctx.fillStyle = "#38bdf8";
  ctx.fillText("ADS1015: " + latestA.toFixed(3) + "V", leftM + 100, topM + 21);

  ctx.fillStyle = "#f8fafc";
  ctx.font = "9px sans-serif";
  ctx.fillText("Instant Delta (\u0394): " + delta.toFixed(1) + " mV", leftM + 180, topM + 21);

  // RIGHT-SIDE ENDPOINT INDICATORS
  let lastY_E = topM + (max - latestE) * plotH / (max - min);
  let lastY_A = topM + (max - latestA) * plotH / (max - min);

  // ESP32 Badge
  ctx.fillStyle = "#166534";
  ctx.beginPath(); ctx.roundRect(w - rightM + 6, lastY_E - 10, 85, 18, 4); ctx.fill();
  ctx.fillStyle = "#4ade80";
  ctx.font = "bold 10px sans-serif"; ctx.textAlign = "left";
  ctx.fillText("ESP: " + latestE.toFixed(3) + "V", w - rightM + 10, lastY_E + 3);

  // ADS1015 Badge
  ctx.fillStyle = "#075985";
  ctx.beginPath(); ctx.roundRect(w - rightM + 6, lastY_A - 10, 85, 18, 4); ctx.fill();
  ctx.fillStyle = "#38bdf8";
  ctx.font = "bold 10px sans-serif"; ctx.textAlign = "left";
  ctx.fillText("ADS: " + latestA.toFixed(3) + "V", w - rightM + 10, lastY_A + 3);
}

window.onload = initWebSocket;
</script>
</body>
</html>)rawliteral";

void setupADC();
void setupADS();
void setupOLED();
void setupAPMode();
void setupServer();
void readMeasurements();
void updateTemperatureAlert();
void updateOLED();
void resetFilter();
void resetMeasurements();
void broadcastData();
void handleDoubleBeep();

void setup()
{
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);

  // Initializing default NORMAL state
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000); 

  setupOLED();
  setupADC();
  setupADS();
  resetFilter();
  setupAPMode();
  setupServer();

  webSocket.begin();

  readMeasurements();
  updateTemperatureAlert();
  updateOLED();
}

void loop()
{
  server.handleClient();
  webSocket.loop();

  unsigned long now = millis();

  // Process non-blocking double-beep sequence if active
  handleDoubleBeep();

  if (streamRunning && !streamPaused && (now - lastSampleTime >= SAMPLE_INTERVAL))
  {
    lastSampleTime = now;
    readMeasurements();
    updateTemperatureAlert();
    broadcastData();
  }

  if (now - lastOledUpdate >= 100)
  {
    lastOledUpdate = now;
    updateOLED();
  }
}

float readEsp32CalibratedVoltage(int pin)
{
  int raw = analogRead(pin);
  if (raw <= 0) return 0.0f;
  if (raw >= 4095) return 3.300f;
  return (float)raw * (3.300f / 4095.0f);
}

void setupAPMode()
{
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  espIPAddress = WiFi.softAPIP().toString();
}

void setupServer()
{
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", MAIN_PAGE); });

  server.on("/start", HTTP_GET, []() {
    streamRunning = true; streamPaused = false;
    server.send(200, "text/plain", "OK");
    broadcastData();
  });

  server.on("/stop", HTTP_GET, []() {
    streamRunning = false; streamPaused = false;
    doubleBeepStage = 0;
    noTone(BUZZER_PIN);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(YELLOW_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
    server.send(200, "text/plain", "OK");
    broadcastData();
  });

  server.on("/togglePause", HTTP_GET, []() {
    if (streamRunning) {
      streamPaused = !streamPaused;
      
      // TRIGGER DOUBLE BEEP SPECIFICALLY WHEN RESUMING
      if (!streamPaused) {
        doubleBeepStage = 1;
        doubleBeepStartTime = millis();
      }
    }
    server.send(200, "text/plain", "OK");
    broadcastData();
  });

  server.on("/reset", HTTP_GET, []() {
    resetMeasurements();
    server.send(200, "text/plain", "OK");
    broadcastData();
  });

  server.on("/filter", HTTP_GET, []() {
    if (server.hasArg("state")) {
      filterEnabled = (server.arg("state") == "on");
    }
    resetFilter();
    server.send(200, "text/plain", "OK");
    broadcastData();
  });

  server.begin();
}

void handleDoubleBeep()
{
  if (doubleBeepStage == 0) return;

  unsigned long elapsed = millis() - doubleBeepStartTime;

  if (doubleBeepStage == 1) { // First Beep (100ms)
    tone(BUZZER_PIN, 2400);
    if (elapsed >= 100) {
      noTone(BUZZER_PIN);
      digitalWrite(BUZZER_PIN, LOW);
      doubleBeepStage = 2;
      doubleBeepStartTime = millis();
    }
  } 
  else if (doubleBeepStage == 2) { // Brief Pause (100ms)
    if (elapsed >= 100) {
      doubleBeepStage = 3;
      doubleBeepStartTime = millis();
    }
  } 
  else if (doubleBeepStage == 3) { // Second Beep (100ms)
    tone(BUZZER_PIN, 2400);
    if (elapsed >= 100) {
      noTone(BUZZER_PIN);
      digitalWrite(BUZZER_PIN, LOW);
      doubleBeepStage = 0; // Double beep complete
    }
  }
}

void broadcastData()
{
  String json = "{\"sample\":" + String(sampleNumber) +
                ",\"running\":" + String((streamRunning && !streamPaused) ? "true" : "false") +
                ",\"paused\":" + String(streamPaused ? "true" : "false") +
                ",\"filter\":" + String(filterEnabled ? "true" : "false") +
                ",\"alarm\":" + String(tempAlarmActive ? "true" : "false") +
                ",\"espPotV\":" + String(espPotVoltage, 4) +
                ",\"adsPotV\":" + String(adsPotVoltage, 4) +
                ",\"diff\":" + String(voltageDifference, 4) +
                ",\"espTemp\":" + String(espTemperature, 2) +
                ",\"adsTemp\":" + String(adsTemperature, 2) +
                ",\"espHist\":[";

  for (int i = 0; i < HISTORY_SIZE; i++) {
    int idx = (historyIndex + i) % HISTORY_SIZE;
    json += String(espHistory[idx], 4) + (i < HISTORY_SIZE - 1 ? "," : "");
  }
  json += "],\"adsHist\":[";
  for (int i = 0; i < HISTORY_SIZE; i++) {
    int idx = (historyIndex + i) % HISTORY_SIZE;
    json += String(adsHistory[idx], 4) + (i < HISTORY_SIZE - 1 ? "," : "");
  }
  json += "]}";

  webSocket.broadcastTXT(json);
}

void setupADC()
{
  analogReadResolution(12);
  analogSetPinAttenuation(POT_PIN, ADC_11db);
  analogSetPinAttenuation(LM35_PIN, ADC_11db);
}

void setupADS()
{
  adsPresent = ads.begin(ADS_ADDRESS);
  if (adsPresent) {
    ads.setGain(GAIN_TWOTHIRDS);
    ads.setDataRate(RATE_ADS1015_1600SPS);
  }
}

void setupOLED()
{
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    oledPresent = true;
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("SYSTEM READY");
    display.display();
  }
}

void resetFilter()
{
  for (int i = 0; i < FILTER_SIZE; i++) espPotBuffer[i] = adsPotBuffer[i] = 0.0f;
}

void resetMeasurements()
{
  sampleNumber = 0;
  historyIndex = 0;
  for (int i = 0; i < HISTORY_SIZE; i++) espHistory[i] = adsHistory[i] = 0.0f;
  resetFilter();
}

void readMeasurements()
{
  sampleNumber++;
  
  float rawEspPotV  = readEsp32CalibratedVoltage(POT_PIN);
  float rawAdsPotV = 0.0f;

  if (adsPresent) {
    int16_t ads0 = ads.readADC_SingleEnded(0);
    if (ads0 < 0) ads0 = 0;
    rawAdsPotV   = ads.computeVolts(ads0);
  } else {
    rawAdsPotV   = rawEspPotV;
  }

  if (filterEnabled) {
    static int filterIdx = 0;
    espPotBuffer[filterIdx] = rawEspPotV;
    adsPotBuffer[filterIdx] = rawAdsPotV;
    filterIdx = (filterIdx + 1) % FILTER_SIZE;

    float espSum = 0, adsSum = 0;
    for (int i = 0; i < FILTER_SIZE; i++) {
      espSum += espPotBuffer[i];
      adsSum += adsPotBuffer[i];
    }
    espPotVoltage = espSum / FILTER_SIZE;
    adsPotVoltage = adsSum / FILTER_SIZE;
  } else {
    espPotVoltage = rawEspPotV;
    adsPotVoltage = rawAdsPotV;
  }

  // Map potentiometer position to simulated temperature reading
  espTemperature = 20.0f + (espPotVoltage / 3.3f) * 40.0f; 
  adsTemperature = espTemperature;

  voltageDifference = fabsf(espPotVoltage - adsPotVoltage);

  espHistory[historyIndex] = espPotVoltage;
  adsHistory[historyIndex] = adsPotVoltage;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;
}

void updateTemperatureAlert()
{
  // Check against threshold (40.0 C)
  if (espTemperature >= ALERT_TEMP_THRESHOLD) {
    tempAlarmActive = true;
  } else if (espTemperature < (ALERT_TEMP_THRESHOLD - TEMP_HYSTERESIS)) {
    tempAlarmActive = false;
  }

  if (streamRunning && !streamPaused) {
    if (tempAlarmActive) {
      // ALERT STATE: Red LED Active, Continuous Alarm Tone (if not playing double beep)
      digitalWrite(YELLOW_LED_PIN, LOW);
      digitalWrite(RED_LED_PIN, HIGH);
      if (doubleBeepStage == 0) tone(BUZZER_PIN, 1800);
    } else {
      // NORMAL STATE: Yellow LED Active, Silence Buzzer
      if (doubleBeepStage == 0) {
        noTone(BUZZER_PIN); 
        digitalWrite(BUZZER_PIN, LOW);
      }
      digitalWrite(RED_LED_PIN, LOW);
      digitalWrite(YELLOW_LED_PIN, HIGH);
    }
  } else {
    // STOPPED/PAUSED STATE
    if (doubleBeepStage == 0) {
      noTone(BUZZER_PIN);
      digitalWrite(BUZZER_PIN, LOW);
    }
    digitalWrite(YELLOW_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
  }
}

void updateOLED()
{
  if (!oledPresent) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("Pot V : "); display.print(espPotVoltage, 3); display.print(" V");
  display.setCursor(0, 16);
  display.print("Temp  : "); display.print(espTemperature, 1); display.print(" C");
  display.setCursor(0, 32);
  display.print("Status: "); display.print(tempAlarmActive ? "ALERT!" : "NORMAL");
  display.setCursor(0, 48);
  display.print("IP    : "); display.print(espIPAddress);
  display.display();
}
