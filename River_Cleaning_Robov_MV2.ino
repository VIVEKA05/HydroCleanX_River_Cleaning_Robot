// RC car with remote + bio-motor PWM speed (analogWrite) + sensor display (NO pH) + SPEED SLIDER
// Slider only works when Bio motors are STARTED.
 
#include <WiFi.h>
#include <WebServer.h>
 
// ===== Access Point credentials =====
const char* ssid     = "Rover-Access-Point";
const char* password = "123456789";
 
WebServer server(80);
 
// ===== Drive motor control pins (L298N / similar) =====
#define MOTOR_LEFT_FORWARD_PIN   14
#define MOTOR_LEFT_BACKWARD_PIN  27
#define MOTOR_RIGHT_FORWARD_PIN  26
#define MOTOR_RIGHT_BACKWARD_PIN 33   // moved from 26 -> 33 to free 26 for BIO motor
 
// ===== Bio motors (on 21 & 26, PWM via analogWrite) =====
#define BIO_MOTOR_IN2 21
#define BIO_MOTOR_IN1 23
#define BIO_MOTOR_IN4 22
#define BIO_MOTOR_IN3 19
 
// ===== Sensor pins (ADC1 channels recommended on ESP32) =====
#define TDS_PIN        18   // analog in
#define WATER_PIN      35   // analog in (generic water-quality / turbidity-like)
 
// ---- Simple calibration (tune for your hardware) ----
const float ADC_MAX = 4095.0; // 12-bit
const float VREF    = 3.3;    // ESP32 ADC reference (approx)
 
// TDS (very rough): ppm ≈ k * voltage; tweak 'TDS_K' by comparing with a meter
const float TDS_K = 500.0;  // ppm per volt (placeholder)
 
// Water Quality display (%): map voltage to 0–100%
float waterQualityPercent(float v) {
  const float minV = 0.5;  // worst
  const float maxV = 2.8;  // best
  float pct = (v - minV) * 100.0f / (maxV - minV);
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  return pct;
}
 
// ===== BIO motor speed control (analogWrite range 0..255) =====
const int BIO_PWM_MAX = 255;
int bioDuty = 120;        // default slower speed (~47%)
bool bioRunning = false;  // <-- slider only works when this is true
 
void setBioSpeed(int duty) {
  bioDuty = constrain(duty, 0, BIO_PWM_MAX);
  // Only apply to pins if running; otherwise just remember bioDuty
  if (bioRunning) {
    analogWrite(BIO_MOTOR_IN1, bioDuty);
    analogWrite(BIO_MOTOR_IN2, 0);
    analogWrite(BIO_MOTOR_IN3, bioDuty);
    analogWrite(BIO_MOTOR_IN4,0);
  }
}
 
void applyBioOutputs() {
  if (bioRunning) {
    analogWrite(BIO_MOTOR_IN1, bioDuty);
    analogWrite(BIO_MOTOR_IN2, 0);
    analogWrite(BIO_MOTOR_IN3, bioDuty);
    analogWrite(BIO_MOTOR_IN4,0);
  } else {

    analogWrite(BIO_MOTOR_IN1, 0);
    analogWrite(BIO_MOTOR_IN2, 0);
    analogWrite(BIO_MOTOR_IN3, 0);
    analogWrite(BIO_MOTOR_IN4,0);
  }
}
 
void startBioMotors() {
  bioRunning = true;
  if (bioDuty <= 0) bioDuty = 120;   // ensure a sensible default
  applyBioOutputs();
  Serial.printf("Bio Motors START @ duty %d/255 (~%d%%)\n", bioDuty, (bioDuty * 100) / 255);
}
void stopBioMotors() {
  bioRunning = false;
  applyBioOutputs();
  Serial.println("Bio Motors STOPPED");
}
 
// ===== HTML Remote Interface (NO pH card) =====
String htmlContent = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Rover Remote</title>
  <style>
    :root { --bg:#121212; --panel:#1e1e1e; --primary:#1e88e5; --primaryH:#1565c0; --accent:#00e676; --danger:#e53935; --dangerH:#b71c1c; --text:#eaeaea; }
    * { box-sizing: border-box; }
    body { margin: 0; padding: 24px; background-color: var(--bg); color: var(--text);
           font-family: Arial, sans-serif; display: flex; flex-direction: column; align-items: center; gap: 16px; }
    h1 { color: var(--accent); margin: 0 0 8px; font-size: 24px; }
    .topbar { width: 100%; max-width: 420px; display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
    .btn { height: 48px; border: none; cursor: pointer; font-size: 16px; border-radius: 10px;
           background: var(--primary); color: white; transition: background-color .2s ease, transform .05s ease; }
    .btn:hover { background: var(--primaryH); }
    .btn:active { transform: translateY(1px); }
    .btn-danger { background: var(--danger); }
    .btn-danger:hover { background: var(--dangerH); }
 
    .panel { width: 100%; max-width: 420px; background: var(--panel); border-radius: 16px; padding: 16px;
             box-shadow: 0 8px 24px rgba(0,0,0,.3); }
    .panel h2 { margin: 0 0 12px; font-size: 18px; color: #a0e0b5; }
 
    .row { display:flex; align-items:center; gap:10px; margin-top: 6px; }
    .slider { width:100%; }
    .mono  { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }
 
    .sensors { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
    .sensor { background: #242424; border-radius: 12px; padding: 12px; text-align: center; }
    .sensor .label { font-size: 12px; color: #bbbbbb; margin-bottom: 6px; }
    .sensor .value { font-size: 18px; font-weight: bold; }
 
    .remote { display: grid; grid-template-columns: 100px 100px 100px; grid-template-rows: 100px 100px 100px;
              gap: 10px; justify-content: center; }
    button.pad { width: 100px; height: 100px; background-color: var(--primary); border: none; cursor: pointer; transition: background-color .2s ease; }
    button.pad:hover { background-color: var(--primaryH); }
    #forward        { clip-path: polygon(50% 0%, 0% 100%, 100% 100%); }
    #backward       { clip-path: polygon(0% 0%, 100% 0%, 50% 100%); }
    #left           { clip-path: polygon(100% 0%, 100% 100%, 0% 50%); }
    #right          { clip-path: polygon(0% 0%, 100% 50%, 0% 100%); }
    #fwd-left, #fwd-right, #bwd-left, #bwd-right { clip-path: polygon(25% 0%, 75% 0%, 100% 25%, 100% 75%, 75% 100%, 25% 100%, 0% 75%, 0% 25%); transform: rotate(45deg); }
    #fwd-left { transform: rotate(-45deg); }
    #fwd-right { transform: rotate(45deg); }
    #bwd-left { transform: rotate(-135deg); }
    #bwd-right { transform: rotate(135deg); }
    #stop { background-color: var(--danger); clip-path: circle(40% at 50% 50%); font-size: 26px; color: white; font-weight: bold; }
    #stop:hover { background-color: var(--dangerH); }
    .note { color:#9aa0a6; font-size: 12px; text-align:center; margin-top: 6px; }
  </style>
</head>
<body>
  <h1>Rover Remote</h1>
 
  <!-- Top bar: Bio Motors (Start/Stop + Speed Slider) -->
  <div class="topbar panel" style="grid-template-columns: 1fr 1fr;">
    <button id="bio-start" class="btn">Start Bio Motors</button>
    <button id="bio-stop" class="btn btn-danger">Stop Bio Motors</button>
 
    <div style="grid-column:1/-1;">
      <div class="row">
        <span>Bio Speed</span>
        <input id="bio-speed" class="slider" type="range" min="0" max="255" step="1" value="0" disabled>
        <span class="mono" id="bio-speed-val">0</span>
      </div>
      <div class="note">Slider works only when motors are started.</div>
    </div>
  </div>
 
  <!-- Sensors -->
  <div class="panel">
    <h2>Live Sensors</h2>
    <div class="sensors">
      <div class="sensor">
        <div class="label">TDS</div>
        <div class="value"><span id="tds">--</span> ppm</div>
      </div>
      <div class="sensor">
        <div class="label">Water Quality</div>
        <div class="value"><span id="wq">--</span>%</div>
      </div>
    </div>
  </div>
 
  <!-- Directional pad -->
  <div class="panel">
    <h2>Drive Control</h2>
    <div class="remote">
      <button id="fwd-left"  class="pad"></button>
      <button id="forward"   class="pad"></button>
      <button id="fwd-right" class="pad"></button>
 
      <button id="left"  class="pad"></button>
      <button id="stop"  class="pad"></button>
      <button id="right" class="pad"></button>
 
      <button id="bwd-left"  class="pad"></button>
      <button id="backward"  class="pad"></button>
      <button id="bwd-right" class="pad"></button>
    </div>
    <div class="note">Hold a direction; releasing sends stop.</div>
  </div>
 
  <script>
    // ===== Drive commands =====
    let interval = null;
    let currentCommand = null;
 
    const commandMap = {
      "fwd-left": "forward-left",
      "forward": "forward",
      "fwd-right": "forward-right",
      "left": "left",
      "right": "right",
      "backward": "backward",
      "bwd-left": "backward-left",
      "bwd-right": "backward-right",
      "stop": "stop"
    };
 
    function sendCommand(command) {
      fetch('/move?direction=' + command)
        .then(res => res.text())
        .then(console.log)
        .catch(console.error);
    }
 
    function startCommand(command) {
      stopCommand();
      currentCommand = command;
      sendCommand(command);
      interval = setInterval(() => sendCommand(command), 300);
    }
 
    function stopCommand() {
      if (interval) { clearInterval(interval); interval = null; }
      if (currentCommand && currentCommand !== "stop") {
        sendCommand("stop");
        currentCommand = null;
      }
    }
 
    document.querySelectorAll("button.pad").forEach(btn => {
      const id = btn.id;
      const command = commandMap[id];
 
      btn.addEventListener("mousedown", (e) => { e.stopPropagation(); startCommand(command); });
      btn.addEventListener("mouseup",   (e) => { e.stopPropagation(); stopCommand(); });
 
      btn.addEventListener("touchstart", (e) => { e.preventDefault(); e.stopPropagation(); startCommand(command); }, { passive: false });
      btn.addEventListener("touchend",   (e) => { e.stopPropagation(); stopCommand(); });
    });
 
    document.addEventListener("mouseup", stopCommand);
    document.addEventListener("touchend", stopCommand);
 
    // ===== Bio motors start/stop =====
    const speedEl  = document.getElementById('bio-speed');
    const speedVal = document.getElementById('bio-speed-val');
 
    function setSpeedUI(val) {
      speedEl.value = val;
      speedVal.textContent = val;
    }
    function setSliderEnabled(enabled) {
      speedEl.disabled = !enabled;
    }
 
    async function updateState() {
      try {
        const s = await fetch('/bio?get=state').then(r => r.text());
        const sp = await fetch('/bio?get=speed').then(r => r.text());
        setSpeedUI(parseInt(sp) || 0);
        setSliderEnabled(s.trim() === 'running');
      } catch(e) {
        console.log(e);
      }
    }
 
    document.getElementById('bio-start').addEventListener('click', async () => {
      await fetch('/bio?cmd=start').catch(console.error);
      updateState();
    });
    document.getElementById('bio-stop').addEventListener('click', async () => {
      await fetch('/bio?cmd=stop').catch(console.error);
      updateState();
    });
 
    // Send speed while sliding ONLY if enabled (running)
    speedEl.addEventListener('input', () => {
      if (speedEl.disabled) return; // ignore when stopped
      const v = parseInt(speedEl.value);
      speedVal.textContent = v;
      fetch('/bio?speed=' + v).catch(console.error);
    });
 
    // ===== Live sensors polling =====
    async function pollSensors() {
      try {
        const res = await fetch('/sensors');
        const data = await res.json();
        document.getElementById('tds').textContent = data.tds_ppm?.toFixed(0) ?? '--';
        document.getElementById('wq').textContent  = data.water_quality_percent?.toFixed(0) ?? '--';
      } catch (e) {
        console.log(e);
      }
    }
    updateState();
    pollSensors();
    setInterval(pollSensors, 1000);
  </script>
</body>
</html>
)rawliteral";
 
// ===== Sensor reading helpers =====
float readVoltageAvg(int pin, int samples = 10) {
  uint32_t sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(2);
  }
  float avg = sum / (float)samples;
  return (avg / ADC_MAX) * VREF;
}
 
// Forward decl
void handleMovement(const String& direction);
 
void setup() {
  Serial.begin(115200);
 
  // Pins for drive motors
  pinMode(MOTOR_LEFT_FORWARD_PIN, OUTPUT);
  pinMode(MOTOR_LEFT_BACKWARD_PIN, OUTPUT);
  pinMode(MOTOR_RIGHT_FORWARD_PIN, OUTPUT);
  pinMode(MOTOR_RIGHT_BACKWARD_PIN, OUTPUT);
 
  // BIO motor pins (PWM by analogWrite)
  pinMode(BIO_MOTOR_IN1, OUTPUT);
  pinMode(BIO_MOTOR_IN2, OUTPUT);
  pinMode(BIO_MOTOR_IN3, OUTPUT);
  pinMode(BIO_MOTOR_IN4, OUTPUT);
  analogWrite(BIO_MOTOR_IN1, 0);
  analogWrite(BIO_MOTOR_IN2, 0);
  analogWrite(BIO_MOTOR_IN3, 0);
  analogWrite(BIO_MOTOR_IN4, 0);
 
  // ADC config
  analogReadResolution(12);
  analogSetPinAttenuation(TDS_PIN,   ADC_11db); // ~0..3.6V
  analogSetPinAttenuation(WATER_PIN, ADC_11db);
 
  // Wi-Fi AP
  WiFi.softAP(ssid, password);
  Serial.println("ESP32 Access Point Started");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
 
  // Routes
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", htmlContent);
  });
 
  // Drive movement
  server.on("/move", HTTP_GET, []() {
    String direction = server.arg("direction");
    handleMovement(direction);
    server.send(200, "text/plain", "Moving: " + direction);
  });
 
  // Bio motors control (start/stop OR set/get speed/state)
  server.on("/bio", HTTP_GET, []() {
    // Query state or speed
    if (server.hasArg("get")) {
      String what = server.arg("get");
      if (what == "speed") {
        server.send(200, "text/plain", String(bioDuty));
        return;
      } else if (what == "state") {
        server.send(200, "text/plain", bioRunning ? "running" : "stopped");
        return;
      }
    }
 
    // Set speed: only apply when running; otherwise ignore per requirement
    if (server.hasArg("speed")) {
      int duty = constrain(server.arg("speed").toInt(), 0, BIO_PWM_MAX);
      if (bioRunning) {
        setBioSpeed(duty);
        server.send(200, "text/plain", "Bio speed set to " + String(duty));
      } else {
        // Ignored while stopped
        server.send(200, "text/plain", "Ignored: bio motors are stopped");
      }
      return;
    }
 
    // Start/Stop
    String cmd = server.arg("cmd");
    if (cmd == "start") {
      startBioMotors();
      server.send(200, "text/plain", "Bio motors: START @" + String(bioDuty));
    } else {
      stopBioMotors();
      server.send(200, "text/plain", "Bio motors: STOP");
    }
  });
 
  // Sensors JSON (NO pH)
  server.on("/sensors", HTTP_GET, []() {
    float v_tds   = readVoltageAvg(TDS_PIN);
    float v_water = readVoltageAvg(WATER_PIN);
 
    float tds_ppm = TDS_K * v_tds;              // rough
    float wq_pct  = waterQualityPercent(v_water);
 
    String json = "{";
    json += "\"tds_ppm\":" + String(tds_ppm, 1) + ",";
    json += "\"water_quality_percent\":" + String(wq_pct, 1);
    json += "}";
    server.send(200, "application/json", json);
  });
 
  server.begin();
}
 
void loop() {
  server.handleClient();
}
 
void handleMovement(const String& direction) {
  Serial.println(direction);
 
  // Stop drive motors first
  digitalWrite(MOTOR_LEFT_FORWARD_PIN, LOW);
  digitalWrite(MOTOR_LEFT_BACKWARD_PIN, LOW);
  digitalWrite(MOTOR_RIGHT_FORWARD_PIN, LOW);
  digitalWrite(MOTOR_RIGHT_BACKWARD_PIN, LOW);
 
  if (direction == "forward") {
    digitalWrite(MOTOR_LEFT_FORWARD_PIN, HIGH);
    digitalWrite(MOTOR_RIGHT_FORWARD_PIN, HIGH);
  } else if (direction == "backward") {
    digitalWrite(MOTOR_LEFT_BACKWARD_PIN, HIGH);
    digitalWrite(MOTOR_RIGHT_BACKWARD_PIN, HIGH);
  } else if (direction == "left") {
    digitalWrite(MOTOR_LEFT_BACKWARD_PIN, HIGH);
    digitalWrite(MOTOR_RIGHT_FORWARD_PIN, HIGH);
  } else if (direction == "right") {
    digitalWrite(MOTOR_LEFT_FORWARD_PIN, HIGH);
    digitalWrite(MOTOR_RIGHT_BACKWARD_PIN, HIGH);
  } else if (direction == "forward-left") {
    digitalWrite(MOTOR_LEFT_FORWARD_PIN, HIGH);
  } else if (direction == "forward-right") {
    digitalWrite(MOTOR_RIGHT_FORWARD_PIN, HIGH);
  } else if (direction == "backward-left") {
    digitalWrite(MOTOR_LEFT_BACKWARD_PIN, HIGH);
  } else if (direction == "backward-right") {
    digitalWrite(MOTOR_RIGHT_BACKWARD_PIN, HIGH);
  }
}