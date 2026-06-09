#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <WebServer.h>

const char* apSsid = "Grzalka-ESP32";
const char* apPassword = "";

WebServer server(80);

const int oneWireBus = 27;     
const int sensorPowerPin = 32; 
const int relayPin = 26;       

OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

const float Kp_30 = 25.0;
const float Ki_30 = 0.018;
const float Kd_30 = 130.0;

const float Kp_80 = 25.0;
const float Ki_80 = 0.35;
const float Kd_80 = 130.0;

bool isHeating = false;
bool manualMode = false;
float targetTemp = 50.0;
float currentTemp = 0.0;
int currentPwm = 0; 
int manualPwm = 0;

float Kp, Ki, Kd;
float integral = 0;
float lastError = 0;

unsigned long windowStartTime = 0;
const unsigned long windowSize = 1000; 
unsigned long lastMsg = 0;
String serialBuffer;

void setupWiFi() {
  WiFi.mode(WIFI_AP);

  bool apOk = WiFi.softAP(apSsid, apPassword);
  if (apOk) {
    Serial.print("ESP32 AP started. Connect to: ");
    Serial.println(apSsid);
    Serial.print("AP panel address: http://");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Failed to start ESP32 AP.");
  }

  Serial.println("Panel is available only through ESP32 Wi-Fi AP.");
}


void adjustPID() {
  float t = constrain(targetTemp, 30.0, 80.0);

  Kp = Kp_30 + (t - 30.0) * (Kp_80 - Kp_30) / (80.0 - 30.0);
  Ki = Ki_30 + (t - 30.0) * (Ki_80 - Ki_30) / (80.0 - 30.0);
  Kd = Kd_30 + (t - 30.0) * (Kd_80 - Kd_30) / (80.0 - 30.0);
  
  Serial.printf("Nowe nastawy PID: Kp=%.1f, Ki=%.3f, Kd=%.1f\n", Kp, Ki, Kd);
}

void sendJsonStatus() {
  Serial.printf(
    "{\"temp\":%.2f,\"target\":%.2f,\"pwm\":%d,\"is_heating\":%s,\"manual\":%s}\n",
    currentTemp,
    targetTemp,
    currentPwm,
    isHeating ? "true" : "false",
    manualMode ? "true" : "false"
  );
}

void handleSerialCommand(const String &cmd) {
  if (cmd.startsWith("TARGET:")) {
    targetTemp = cmd.substring(7).toFloat();
    manualMode = false;
    isHeating = true;
    integral = 0;
    lastError = 0;
    adjustPID();
    return;
  }

  if (cmd.startsWith("MANUAL:")) {
    float pct = cmd.substring(7).toFloat();
    pct = constrain(pct, 0.0, 100.0);
    manualPwm = (int)((pct * 255.0) / 100.0);
    currentPwm = manualPwm;
    manualMode = true;
    isHeating = true;
    integral = 0;
    lastError = 0;
    return;
  }

  if (cmd == "CMD:STOP") {
    manualMode = false;
    isHeating = false;
    currentPwm = 0;
    manualPwm = 0;
    digitalWrite(relayPin, LOW);
    return;
  }
}

void readSerialCommands() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (serialBuffer.length() > 0) {
        handleSerialCommand(serialBuffer);
        serialBuffer = "";
      }
    } else {
      serialBuffer += c;
      if (serialBuffer.length() > 128) {
        serialBuffer = "   ";
      }
    }
  }
}

const char MAIN_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pl">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ESP32 Sterowanie Grzalka</title>
<style>
  :root{--bg:#0b1020;--card:#151d33;--line:#2b3a63;--txt:#e6edf7;--muted:#94a3c2;--ok:#22c55e;--hot:#f97316;--cold:#3b82f6;--danger:#ef4444}
  *{box-sizing:border-box} body{margin:0;background:radial-gradient(circle at top,#1b2746 0,#0b1020 55%);font-family:Arial,sans-serif;color:var(--txt)}
  .wrap{max-width:1100px;margin:0 auto;padding:16px;display:grid;gap:14px}
  .top{display:grid;grid-template-columns:1fr auto;gap:12px;align-items:center}
  .card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:14px}
  .row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
  .temp{font-size:clamp(38px,6vw,54px);font-weight:700;color:var(--hot);line-height:1.05;word-break:keep-all}
  .small{color:var(--muted);font-size:13px}
  .chip{padding:6px 10px;border-radius:18px;border:1px solid var(--line);font-size:12px}
  .chip.on{border-color:var(--ok);color:var(--ok)}
  .chip.manual{border-color:var(--hot);color:var(--hot)}
  .controls{display:grid;grid-template-columns:repeat(3,minmax(180px,1fr));gap:10px}
  input{width:100%;background:#0f1730;border:1px solid var(--line);border-radius:10px;padding:10px;color:var(--txt);font-size:16px}
  button{width:100%;border:0;border-radius:10px;padding:10px;font-weight:700;cursor:pointer}
  .b1{background:var(--cold);color:white}.b2{background:var(--hot);color:#111}.b3{background:var(--danger);color:white}.b4{background:transparent;color:var(--muted);border:1px solid var(--line)}
  .stats{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}
  .statv{font-size:26px;font-weight:700}
  .chartBox{height:380px}
  canvas{display:block;width:100%;height:100%}
  #log{max-height:110px;overflow:auto;font-size:12px;line-height:1.6;color:var(--muted)}
  @media (max-width:1100px){
    .top{grid-template-columns:1fr;align-items:stretch}
    .top .row{justify-content:flex-start}
  }

  @media (max-width:850px){
    .controls,.stats{grid-template-columns:1fr}
    .chartBox{height:300px}
    .wrap{padding:10px}
    .card{padding:12px}
    .temp{font-size:clamp(34px,10vw,48px)}
    .small{font-size:12px}
  }
</style>
</head>
<body>
  <div class="wrap">
    <div class="top card">
      <div>
        <div class="small">Sterowanie ukladem grzania</div>
        <div class="temp" id="temp">--.- C</div>
        <div class="small">Cel: <b id="target">--.-</b> C | Moc: <b id="pwm">0</b>%</div>
      </div>
      <div class="row">
        <span class="chip" id="mode">STOP</span>
        <button class="b4" onclick="resetChart()">Reset wykresu</button>
        <button class="b4" onclick="exportData()">Eksport .txt</button>
      </div>
    </div>

    <div class="controls">
      <div class="card">
        <div class="small">PID - temperatura zadana [C]</div>
        <input type="number" id="pidTemp" min="20" max="90" step="0.5" value="50">
        <div class="row" style="margin-top:8px"><button class="b1" onclick="setPID()">Uruchom PID</button></div>
      </div>
      <div class="card">
        <div class="small">Tryb manualny - moc [%]</div>
        <input type="number" id="manPwm" min="0" max="100" step="1" value="40">
        <div class="row" style="margin-top:8px"><button class="b2" onclick="setManual()">Ustaw moc</button></div>
      </div>
      <div class="card">
        <div class="small">Zatrzymanie</div>
        <div class="row" style="margin-top:30px"><button class="b3" onclick="stopHeat()">STOP</button></div>
      </div>
    </div>

    <div class="stats">
      <div class="card"><div class="small">Temperatura</div><div class="statv" id="sTemp">--.-</div><div class="small">aktualna</div></div>
      <div class="card"><div class="small">Zadana</div><div class="statv" id="sTarget">--.-</div></div>
      <div class="card"><div class="small">PWM</div><div class="statv" id="sPwm">0%</div></div>
      <div class="card"><div class="small">Tryb</div><div class="statv" id="sMode">STOP</div></div>
    </div>

    <div class="card">
      <div class="small">Wykres: odpowiedz ukladu + sygnal sterujacy</div>
      <div class="chartBox"><canvas id="chart"></canvas></div>
    </div>

    <div class="card"><div id="log"></div></div>
  </div>

<script>
let historyData=[];
const MAX_POINTS=360;
const tArr=[],tempArr=[],targetArr=[],pwmArr=[];
const canvas=document.getElementById('chart');
const ctx=canvas.getContext('2d');
let chartSize={w:0,h:0,dpr:1};

function resizeChart(){
  const rect=canvas.parentElement.getBoundingClientRect();
  const dpr=window.devicePixelRatio || 1;
  const w=Math.max(320, Math.floor(rect.width));
  const h=Math.max(220, Math.floor(rect.height));
  chartSize={w,h,dpr};
  canvas.width=Math.floor(w*dpr);
  canvas.height=Math.floor(h*dpr);
  canvas.style.width=w+'px';
  canvas.style.height=h+'px';
  ctx.setTransform(dpr,0,0,dpr,0,0);
  renderChart();
}

function renderChart(){
  const w=chartSize.w, h=chartSize.h;
  ctx.clearRect(0,0,w,h);

  ctx.fillStyle='#0f1730';
  ctx.fillRect(0,0,w,h);

  if(!tempArr.length){
    ctx.fillStyle='#94a3c2';
    ctx.font='14px Arial';
    ctx.fillText('Brak danych - oczekiwanie na pierwszy pomiar', 18, 28);
    return;
  }

  const pad={l:46,r:46,t:18,b:30};
  const x0=pad.l, y0=pad.t, plotW=w-pad.l-pad.r, plotH=h-pad.t-pad.b;
  const tempMin=0, tempMax=100, pwmMin=0, pwmMax=100;

  ctx.strokeStyle='rgba(43,58,99,0.75)';
  ctx.lineWidth=1;
  ctx.font='11px Arial';
  ctx.fillStyle='#9fb1d5';
  for(let i=0;i<=5;i++){
    const y=y0 + plotH*(i/5);
    ctx.beginPath(); ctx.moveTo(x0,y); ctx.lineTo(x0+plotW,y); ctx.stroke();
    const val=(tempMax - (tempMax-tempMin)*(i/5)).toFixed(0);
    ctx.fillText(val, 8, y+4);
  }

  const mapX=(i)=> x0 + plotW*(i/Math.max(1,tempArr.length-1));
  const mapTemp=(v)=> y0 + plotH*(1 - (v-tempMin)/(tempMax-tempMin));
  const mapPwm=(v)=> y0 + plotH*(1 - (v-pwmMin)/(pwmMax-pwmMin));

  const drawLine=(arr,color,mapY,fillColor)=>{
    if(arr.length<2) return;
    ctx.beginPath();
    arr.forEach((v,i)=>{
      const x=mapX(i), y=mapY(v);
      if(i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
    });
    ctx.strokeStyle=color;
    ctx.lineWidth=2;
    ctx.stroke();

    if(fillColor){
      ctx.lineTo(mapX(arr.length-1), y0+plotH);
      ctx.lineTo(mapX(0), y0+plotH);
      ctx.closePath();
      ctx.fillStyle=fillColor;
      ctx.fill();
    }
  };

  drawLine(targetArr,'#3b82f6',mapTemp,null);
  drawLine(tempArr,'#f97316',mapTemp,'rgba(249,115,22,0.12)');

  if(pwmArr.length>=2){
    ctx.beginPath();
    pwmArr.forEach((v,i)=>{
      const x=mapX(i), y=mapPwm(v);
      if(i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
    });
    ctx.strokeStyle='rgba(34,197,94,0.95)';
    ctx.lineWidth=2;
    ctx.stroke();
  }

  ctx.fillStyle='#8de2a9';
  ctx.fillText('PWM %', w-38, y0+10);
  ctx.fillStyle='#c6d2ea';
  ctx.fillText('Temperatura [C]', x0+6, 14);
}

window.addEventListener('resize', resizeChart);

function log(m){const el=document.getElementById('log');const ts=new Date().toLocaleTimeString('pl-PL',{hour12:false});el.innerHTML+=`[${ts}] ${m}<br>`;el.scrollTop=el.scrollHeight;}
async function setPID(){const t=parseFloat(document.getElementById('pidTemp').value||'50');await fetch(`/api/pid?t=${encodeURIComponent(t)}`);log(`PID start, cel ${t.toFixed(1)} C`);}
async function setManual(){const p=parseFloat(document.getElementById('manPwm').value||'0');await fetch(`/api/manual?p=${encodeURIComponent(p)}`);log(`Manual, moc ${Math.max(0,Math.min(100,p)).toFixed(0)}%`);}
async function stopHeat(){await fetch('/api/stop');log('Zatrzymano grzanie');}

function exportData(){
  if(!historyData.length){log('Brak danych do eksportu');return;}
  let txt='czas[s]\ttemp[C]\tcel[C]\tmoc[%]\n';
  historyData.forEach(d=>txt+=`${d.t}\t${d.temp.toFixed(2)}\t${d.target.toFixed(2)}\t${d.pwm}\n`);
  const blob=new Blob([txt],{type:'text/plain'});
  const a=document.createElement('a');
  a.href=URL.createObjectURL(blob);
  a.download=`dane_grzalki_${new Date().toISOString().slice(0,19).replace(/:/g,'-')}.txt`;
  a.click();
  log(`Eksport danych: ${historyData.length} punktow`);
}

function resetChart(){
  historyData = [];
  tArr.length = 0;
  tempArr.length = 0;
  targetArr.length = 0;
  pwmArr.length = 0;
  renderChart();
  log('Wykres zresetowany');
}

async function refresh(){
  try{
    const r=await fetch('/data',{cache:'no-store'});
    const d=await r.json();
    const pwmPct=Math.round((d.pwm/255)*100);
    const ts=((Date.now()-window._t0)/1000).toFixed(1);

    document.getElementById('temp').textContent=`${d.temp.toFixed(1)} C`;
    document.getElementById('target').textContent=d.target.toFixed(1);
    document.getElementById('pwm').textContent=pwmPct;
    document.getElementById('sTemp').textContent=d.temp.toFixed(1);
    document.getElementById('sTarget').textContent=d.target.toFixed(1);
    document.getElementById('sPwm').textContent=`${pwmPct}%`;

    const modeEl=document.getElementById('mode');
    const modeTxt=d.is_heating?(d.manual?'MANUAL':'PID'):'STOP';
    modeEl.textContent=modeTxt;
    modeEl.className='chip'+(d.is_heating?(d.manual?' manual':' on'):'');
    document.getElementById('sMode').textContent=modeTxt;

    if(tArr.length>=MAX_POINTS){tArr.shift();tempArr.shift();targetArr.shift();pwmArr.shift();}
    tArr.push(ts+'s');tempArr.push(d.temp);targetArr.push(d.target);pwmArr.push(pwmPct);
    historyData.push({t:ts,temp:d.temp,target:d.target,pwm:pwmPct});
    renderChart();
  }catch(e){log('Brak odpowiedzi z /data');}
}

window._t0=Date.now();
resizeChart();
setInterval(refresh,1000);
refresh();
log('Panel gotowy');
</script>
</body>
</html>
)rawliteral";

void handleRoot() {
  server.send_P(200, "text/html", MAIN_PAGE);
}

void handleData() {
  String json = "{";
  json += "\"temp\":" + String(currentTemp, 2);
  json += ",\"target\":" + String(targetTemp, 2);
  json += ",\"pwm\":" + String(currentPwm);
  json += ",\"is_heating\":" + String(isHeating ? "true" : "false");
  json += ",\"manual\":" + String(manualMode ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void handleApiPid() {
  if (server.hasArg("t")) {
    targetTemp = constrain(server.arg("t").toFloat(), 20.0, 90.0);
    manualMode = false;
    isHeating = true;
    manualPwm = 0;
    integral = 0;
    lastError = 0;
    adjustPID();
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiManual() {
  if (server.hasArg("p")) {
    float pct = constrain(server.arg("p").toFloat(), 0.0, 100.0);
    manualPwm = (int)((pct * 255.0) / 100.0);
    currentPwm = manualPwm;
    manualMode = true;
    isHeating = true;
    integral = 0;
    lastError = 0;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiStop() {
  isHeating = false;
  manualMode = false;
  currentPwm = 0;
  manualPwm = 0;
  digitalWrite(relayPin, LOW);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleSetTarget() {
  if (server.hasArg("t")) {
    targetTemp = server.arg("t").toFloat();
    manualMode = false;
    isHeating = true;
    adjustPID(); 
  }
  server.send(200, "text/plain", "OK");
}

void handleStart() { isHeating = true; manualMode = false; manualPwm = 0; integral = 0; lastError = 0; adjustPID(); server.sendHeader("Location", "/"); server.send(303); }
void handleStop() { isHeating = false; manualMode = false; currentPwm = 0; manualPwm = 0; digitalWrite(relayPin, LOW); server.sendHeader("Location", "/"); server.send(303); }

void setup() {
  Serial.begin(115200);
  pinMode(sensorPowerPin, OUTPUT);
  digitalWrite(sensorPowerPin, HIGH);
  pinMode(relayPin, OUTPUT);
  sensors.begin();

  setupWiFi();
  
  adjustPID(); 
  
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/api/pid", handleApiPid);
  server.on("/api/manual", handleApiManual);
  server.on("/api/stop", handleApiStop);
  server.on("/set", handleSetTarget);
  server.on("/start", handleStart);
  server.on("/stop", handleStop);
  server.begin();
  windowStartTime = millis();
}

void loop() {
  server.handleClient();
  readSerialCommands();

  if (millis() - lastMsg > 1000) {
    sensors.requestTemperatures();
    float tempRead = sensors.getTempCByIndex(0);

    if (tempRead != DEVICE_DISCONNECTED_C && tempRead > -50) {
      currentTemp = tempRead;
      
      if (isHeating && manualMode) {
        currentPwm = manualPwm;
      } else if (isHeating) {
        float error = targetTemp - currentTemp;
        float derivative = error - lastError;
        
        if (abs(error) < 2.5) {
          integral += error; 
          
          float maxInt = (Ki > 0.0) ? (130.0 / Ki) : 0.0; 
          if (integral > maxInt) integral = maxInt;
          if (integral < -maxInt) integral = -maxInt;
        } else {
          integral = 0; 
        }
        
        float output = (Kp * error) + (Ki * integral) + (Kd * derivative);
        currentPwm = constrain((int)output, 0, 255);
        lastError = error;
      }
    }

    sendJsonStatus();
    lastMsg = millis();
  }

  if (isHeating && currentPwm > 0) {
    unsigned long now = millis();
    if (now - windowStartTime >= windowSize) windowStartTime += windowSize;
    unsigned long onTime = (currentPwm * windowSize) / 255;
    digitalWrite(relayPin, (now - windowStartTime < onTime) ? HIGH : LOW);
  } else {
    digitalWrite(relayPin, LOW);
  }
}