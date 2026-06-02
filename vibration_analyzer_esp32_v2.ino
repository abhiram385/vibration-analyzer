/* =============================================================
 *  Real-Time Vibration Analyzer — ESP32 Standalone (v1)
 *  Hardware : ESP32 DevKit V1 (CP2102, 30-pin)
 *  Sensor   : MPU-6050 MEMS 3-axis accelerometer (I2C, 0x68)
 *  Libraries: WiFi, WebServer, Wire, arduinoFFT
 *
 *  v1 runs the full pipeline on a single chip:
 *  sampling, windowing, FFT, fault detection, HTTP dashboard.
 *  See v2 for dual-chip architecture with dedicated DSP node.
 * ============================================================= */

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <arduinoFFT.h>

const char* ssid = "YOUR_WIFI";
const char* pass = "YOUR_PASS";

WebServer server(80);

/* ─── FFT configuration ─────────────────────────────────────── 
 *  1024 points chosen over 512 for two reasons:
 *  1. Frequency resolution = sample_rate / FFT_size.
 *     1024 pts → 0.977 Hz/bin vs 512 pts → 1.95 Hz/bin.
 *     Finer resolution is critical for distinguishing closely
 *     spaced mechanical fault frequencies.
 *  2. FFT algorithms require power-of-2 sizes for efficiency.
 *     1024 is the optimal balance between resolution and
 *     computation time on embedded hardware.
 * ──────────────────────────────────────────────────────────── */
#define SAMPLES      1024
#define SAMPLE_RATE  1000

float vReal[SAMPLES];
float vImag[SAMPLES];
ArduinoFFT<float> FFT = ArduinoFFT<float>(vReal, vImag, SAMPLES, SAMPLE_RATE);

float  latest_freq  = 0.0;
float  latest_mag   = 0.0;
int    latest_fault = 0;
String last_updated = "No data yet";

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
            padding: 1.5rem 2rem; margin: 0.5rem; width: 260px; text-align: center;
            transition: all 0.3s; }
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
    var audioCtx=null,oscillator=null,gainNode=null,alarmOn=false,muted=false;
    function enableAudio(){
      audioCtx=new(window.AudioContext||window.webkitAudioContext)();
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

void handleRoot() {
    server.send(200, "text/html", String(PAGE));
}

void handleData() {
    String json = "{";
    json += "\"freq\":"   + String(latest_freq,  2) + ",";
    json += "\"mag\":"    + String(latest_mag,   4) + ",";
    json += "\"fault\":"  + String(latest_fault)    + ",";
    json += "\"time\":\"" + last_updated            + "\"";
    json += "}";
    server.send(200, "application/json", json);
}

void readMPU(float* buf) {
    for (int i = 0; i < SAMPLES; i++) {
        Wire.beginTransmission(0x68);
        Wire.write(0x3B);
        Wire.endTransmission(false);
        Wire.requestFrom(0x68, 2);
        int16_t ax = Wire.read() << 8 | Wire.read();
        buf[i] = ax / 16384.0f;
        delayMicroseconds(1000);
    }
}

void runFFT() {
    for (int i = 0; i < SAMPLES; i++) vImag[i] = 0;

    /* Hamming window applied to reduce spectral leakage.
     * A finite sample window abruptly truncates the signal,
     * creating artificial frequency components in the FFT output.
     * Hamming tapers the signal smoothly to zero at both ends,
     * eliminating the abrupt cutoff and producing a cleaner spectrum.
     * A rectangular window would spread energy from the dominant
     * frequency into adjacent bins, masking real fault signatures. */
    FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);

    FFT.compute(FFTDirection::Forward);
    FFT.complexToMagnitude();

    /* Skip bin 0 — DC offset from static gravity component */
    float peak_val = 0;
    int   peak_bin = 1;
    for (int i = 1; i < SAMPLES / 2; i++) {
        if (vReal[i] > peak_val) {
            peak_val = vReal[i];
            peak_bin = i;
        }
    }

    latest_freq  = (peak_bin * SAMPLE_RATE) / (float)SAMPLES;
    latest_mag   = peak_val / (SAMPLES / 2);

    /* Fault thresholds from DC motor testing:
     * Healthy: freq < 3 Hz, mag < 0.005g
     * Imbalance fault: spike at 50-100 Hz, mag > 0.1g */
    latest_fault = (latest_mag > 0.1 || latest_freq > 5.0) ? 1 : 0;

    unsigned long s = millis() / 1000;
    last_updated = String(s / 60) + "m " + String(s % 60) + "s uptime";

    Serial.printf("Freq: %.2f Hz  Mag: %.4f g  Fault: %d\n",
                  latest_freq, latest_mag, latest_fault);
}

void setup() {
    Serial.begin(115200);
    Wire.begin(21, 22);

    Wire.beginTransmission(0x68);
    Wire.write(0x6B);
    Wire.write(0x00);
    Wire.endTransmission();

    WiFi.begin(ssid, pass);
    Serial.print("Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected. Open: http://" + WiFi.localIP().toString());

    server.on("/",     handleRoot);
    server.on("/data", handleData);
    server.begin();
    Serial.println("Web server started.");
}

void loop() {
    server.handleClient();
    readMPU(vReal);
    runFFT();
}
