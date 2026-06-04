/* =============================================================
 *  Real-Time Vibration Analyzer — ESP32 (Dashboard Node)
 *  Hardware : ESP32 DevKit V1 (CP2102, 30-pin)
 *  Input    : UART2 @ 115200 baud ← STM32F401 (GPIO16=RX)
 *  Output   : HTTP dashboard on port 80 (local Wi-Fi)
 *  Role     : Receive pre-computed FFT results from STM32,
 *             serve live web dashboard — no DSP on this node
 * =============================================================
 *  UART format from STM32:
 *  "FREQ:73.24,MAG:0.3841,FAULT:1\n"
 * ============================================================= */

#include <WiFi.h>
#include <WebServer.h>

/* ─── Wi-Fi credentials ─────────────────────────────────────── */
const char* ssid = "YOUR_WIFI";
const char* pass = "YOUR_PASS";

/* ─── UART2 — receives data from STM32 ─────────────────────── */
#define STM32_UART_RX  16   /* GPIO16 = UART2 RX on ESP32       */
#define STM32_UART_TX  17   /* GPIO17 = UART2 TX (unused here)  */
#define STM32_BAUD     115200

/* ─── Web server on port 80 ─────────────────────────────────── */
WebServer server(80);

/* ─── Global state (updated on each UART receive) ───────────── */
float  latest_freq  = 0.0;
float  latest_mag   = 0.0;
int    latest_fault = 0;
String last_updated = "No data yet";

/* ─── HTML dashboard (identical UI to v1) ───────────────────── */
const char PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
  <title>Vibration Analyzer</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: monospace; background: #0d0d0d; color: #00ff88;
           display: flex; flex-direction: column; align-items: center;
           padding: 2rem; transition: background 0.3s; }
    body.fault { animation: flashbg 0.5s infinite; }
    @keyframes flashbg {
      0%  { background: #1a0000; }
      50% { background: #3a0000; }
      100%{ background: #1a0000; }
    }
    h1 { font-size: 1.2rem; letter-spacing: 4px; margin-bottom: 2rem; }
    .card { background: #1a1a1a; border: 1px solid #00ff88; border-radius: 8px;
            padding: 1.5rem 2rem; margin: 0.5rem; width: 260px; text-align: center; }
    body.fault .card  { border-color: #ff4444; background: #2a0000; }
    body.fault h1     { color: #ff4444; }
    .label { font-size: 0.75rem; color: #888; margin-bottom: 6px; }
    .value { font-size: 2rem; font-weight: bold; }
    .ok    { color: #00ff88; }
    .warn  { color: #ff4444; animation: pulse 0.5s infinite; }
    @keyframes pulse { 0%{opacity:1} 50%{opacity:0.3} 100%{opacity:1} }
    .ts    { font-size: 0.7rem; color: #555; margin-top: 2rem; }
    #start-btn { margin-top: 1.5rem; padding: 10px 24px; background: transparent;
                 border: 1px solid #00ff88; color: #00ff88; font-family: monospace;
                 border-radius: 4px; cursor: pointer; font-size: 13px; }
    #start-btn.muted { border-color: #555; color: #555; }
    #status { font-size: 11px; color: #555; margin-top: 8px; }
  </style>
</head>
<body id="body">
  <h1 id="title">VIBRATION ANALYZER</h1>
  <div class="card">
    <div class="label">DOMINANT FREQUENCY</div>
    <div class="value" id="freq">-- Hz</div>
  </div>
  <div class="card">
    <div class="label">MAGNITUDE</div>
    <div class="value" id="mag">-- g</div>
  </div>
  <div class="card">
    <div class="label">FAULT STATUS</div>
    <div class="value ok" id="fault">--</div>
  </div>
  <div class="ts" id="ts">Tap enable to start monitoring</div>
  <button id="start-btn" onclick="enableAudio()">enable monitoring + alarm</button>
  <div id="status"></div>
  <script>
    var audioCtx=null,oscillator=null,gainNode=null,alarmOn=false,muted=false,monitoring=false;
    function enableAudio(){
      audioCtx=new(window.AudioContext||window.webkitAudioContext)();
      monitoring=true;
      document.getElementById('start-btn').textContent='mute alarm';
      document.getElementById('start-btn').onclick=muteAlarm;
      document.getElementById('status').textContent='monitoring active';
      fetchData();
    }
    function muteAlarm(){
      muted=true; stopAlarm();
      document.getElementById('start-btn').textContent='alarm muted';
      document.getElementById('start-btn').classList.add('muted');
    }
    function startAlarm(){
      if(muted||alarmOn||!audioCtx)return;
      oscillator=audioCtx.createOscillator();
      gainNode=audioCtx.createGain();
      oscillator.connect(gainNode); gainNode.connect(audioCtx.destination);
      oscillator.type='square'; gainNode.gain.setValueAtTime(0.3,audioCtx.currentTime);
      var t=audioCtx.currentTime;
      for(var i=0;i<20;i++){
        oscillator.frequency.setValueAtTime(880,t+i*0.25);
        oscillator.frequency.setValueAtTime(440,t+i*0.25+0.125);
      }
      oscillator.start(); alarmOn=true;
    }
    function stopAlarm(){
      if(oscillator&&alarmOn){oscillator.stop();oscillator=null;alarmOn=false;}
    }
    function fetchData(){
      fetch('/data')
        .then(r=>r.json())
        .then(d=>{
          document.getElementById('freq').textContent=d.freq.toFixed(2)+' Hz';
          document.getElementById('mag').textContent=d.mag.toFixed(4)+' g';
          document.getElementById('ts').textContent='Last updated: '+d.time;
          var faultEl=document.getElementById('fault');
          var body=document.getElementById('body');
          var title=document.getElementById('title');
          if(d.fault===1){
            faultEl.textContent='FAULT'; faultEl.className='value warn';
            body.classList.add('fault'); title.textContent='!! FAULT DETECTED !!';
            startAlarm();
          } else {
            faultEl.textContent='OK'; faultEl.className='value ok';
            body.classList.remove('fault'); title.textContent='VIBRATION ANALYZER';
            if(!muted)stopAlarm();
          }
        })
        .catch(e=>{document.getElementById('status').textContent='connection lost...';});
      setTimeout(fetchData,2000);
    }
  </script>
</body>
</html>
)rawhtml";

/* ─── Route: serve dashboard ────────────────────────────────── */
void handleRoot() {
    server.send(200, "text/html", String(PAGE));
}

/* ─── Route: serve live JSON data ───────────────────────────── */
void handleData() {
    String json = "{";
    json += "\"freq\":"   + String(latest_freq,  2) + ",";
    json += "\"mag\":"    + String(latest_mag,   4) + ",";
    json += "\"fault\":"  + String(latest_fault)    + ",";
    json += "\"time\":\"" + last_updated            + "\"";
    json += "}";
    server.send(200, "application/json", json);
}

/* ─── Parse UART string from STM32 ─────────────────────────── */
/* Expected format: "FREQ:73.24,MAG:0.3841,FAULT:1\n"          */
void parseUARTString(String line) {
    line.trim();
    if (!line.startsWith("FREQ:")) return;  /* Discard malformed lines */

    /* Extract FREQ */
    int f_start = line.indexOf("FREQ:") + 5;
    int f_end   = line.indexOf(",MAG:");
    if (f_start < 0 || f_end < 0) return;
    latest_freq = line.substring(f_start, f_end).toFloat();

    /* Extract MAG */
    int m_start = line.indexOf("MAG:") + 4;
    int m_end   = line.indexOf(",FAULT:");
    if (m_start < 0 || m_end < 0) return;
    latest_mag = line.substring(m_start, m_end).toFloat();

    /* Extract FAULT */
    int fault_start = line.indexOf("FAULT:") + 6;
    if (fault_start < 0) return;
    latest_fault = line.substring(fault_start).toInt();

    /* Update timestamp */
    unsigned long s = millis() / 1000;
    last_updated = String(s / 60) + "m " + String(s % 60) + "s uptime";

    Serial.printf("[UART] Freq: %.2f Hz  Mag: %.4f g  Fault: %d\n",
                  latest_freq, latest_mag, latest_fault);
}

/* ─── Setup ─────────────────────────────────────────────────── */
void setup() {
    Serial.begin(115200);

    /* UART2 for STM32 communication */
    Serial2.begin(STM32_BAUD, SERIAL_8N1, STM32_UART_RX, STM32_UART_TX);
    Serial.println("UART2 listening for STM32 data...");

    /* Connect to Wi-Fi */
    WiFi.begin(ssid, pass);
    Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected. Dashboard at: http://" + WiFi.localIP().toString());

    /* Register HTTP routes */
    server.on("/",     handleRoot);
    server.on("/data", handleData);
    server.begin();
    Serial.println("Web server started.");
}

/* ─── Main loop ─────────────────────────────────────────────── */
void loop() {
    server.handleClient();  /* Serve HTTP requests */

    /* Read complete line from STM32 UART */
    if (Serial2.available()) {
        String line = Serial2.readStringUntil('\n');
        parseUARTString(line);
    }
}
