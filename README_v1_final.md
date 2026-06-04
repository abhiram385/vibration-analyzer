# Real-Time Vibration Analyzer for Predictive Maintenance

Early fault detection in industrial machinery using FFT-based vibrational analysis on a single ESP32 with an MPU-6050 MEMS accelerometer.

> **v2 available:** DSP offloaded to STM32F401 ARM Cortex-M4 — see [`v2_stm32_esp32/`](../v2_stm32_esp32/README.md)

---

## Live Documentation

| Resource | Description |
|----------|-------------|
| [FFT Spectrum — Healthy vs Fault](https://abhiram385.github.io/vibration-analyzer/docs/vibration_analyzer_fft_plot.html) | Interactive chart — dominant frequency spike at 73.24 Hz under fault condition |
| [System Architecture Diagram](https://abhiram385.github.io/vibration-analyzer/docs/vibration_analyzer_architecture.html) | Full hardware and signal pipeline block diagram |
| [Dashboard Mockup — Both States](https://abhiram385.github.io/vibration-analyzer/docs/vibration_analyzer_dashboard_mockup.html) | ESP32 HTTP dashboard, healthy and fault states side by side |

---

## Overview

This system performs real-time vibration analysis to detect mechanical faults in rotating machinery — targeting use cases like motor imbalance, bearing wear, and misalignment. It captures 1024 acceleration samples at 1 kHz, applies a Hamming-windowed 1024-point FFT to extract the dominant frequency and magnitude, and classifies the system as healthy or faulty based on configurable thresholds. Results are served live over a local HTTP dashboard accessible from any device on the network — no cloud dependency.

Tested on a DC motor with a controlled mechanical imbalance to simulate industrial predictive maintenance scenarios. The fault signature appeared as a dominant frequency spike in the **50–100 Hz range** with peak magnitudes up to **~0.38g**, compared to a near-zero baseline (<0.005g, <3 Hz) under balanced conditions.

---

## Hardware

| Component | Details |
|-----------|---------|
| Microcontroller | ESP32 DevKit V1 (CP2102, 30-pin) |
| Sensor | MPU-6050 MEMS 3-axis accelerometer (I2C, addr 0x68) |
| Interface | SDA → GPIO 21, SCL → GPIO 22 |
| Range | ±2g (16384 LSB/g) |
| Power | USB 5V via CP2102 |

---

## System Architecture

```
MPU-6050 (I2C 0x68)
    │
    │  I2C · SDA=GPIO21 · SCL=GPIO22
    ▼
ESP32 DevKit
    ├─ Sample 1024 points @ 1 kHz (1.024s window)
    ├─ Apply Hamming window (spectral leakage reduction)
    ├─ Compute 1024-point FFT (arduinoFFT library)
    ├─ Extract peak bin → dominant frequency (Hz)
    ├─ Normalise peak magnitude → g
    ├─ Fault condition: magnitude > 0.1g OR frequency > 5 Hz
    └─ Serve live JSON + HTML dashboard over Wi-Fi (port 80)
         │
         │  HTTP · LAN · no cloud
         ▼
    Any browser on local network
```

**Frequency resolution:** 1000 Hz ÷ 1024 = **0.977 Hz/bin**  
**Analysis window:** 1024 samples × 1 ms = **1.024 seconds per reading**

---

## Signal Processing Pipeline

### 1. Sampling
Raw 16-bit accelerometer data read from register `0x3B` (ACCEL_XOUT_H) via I2C at 1 ms intervals:
```c
int16_t ax = Wire.read() << 8 | Wire.read();
buf[i] = ax / 16384.0f;  // Convert to g (±2g range)
```

### 2. Hamming Windowing
Applied before FFT to reduce spectral leakage from the finite sample window:
```c
FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);
```

### 3. FFT Computation
1024-point FFT via the arduinoFFT library. Complex output converted to magnitude spectrum:
```c
FFT.compute(FFTDirection::Forward);
FFT.complexToMagnitude();
```

### 4. Peak Detection
DC bin (bin 0) skipped. Peak bin converted to frequency:
```c
latest_freq = (peak_bin * SAMPLE_RATE) / (float)SAMPLES;
latest_mag  = peak_val / (SAMPLES / 2);  // Normalised to g
```

### 5. Fault Classification
```c
latest_fault = (latest_mag > 0.1 || latest_freq > 5.0) ? 1 : 0;
```

---

## Results

### Healthy State (balanced motor)
| Metric | Value |
|--------|-------|
| Dominant frequency | ~1–3 Hz (near-DC noise) |
| Peak magnitude | < 0.005 g |
| Fault flag | 0 (OK) |

### Fault State (mechanical imbalance on DC motor shaft)
| Metric | Value |
|--------|-------|
| Dominant frequency | **73.24 Hz** (fundamental imbalance frequency) |
| Second harmonic | ~146.5 Hz (2× fundamental — physically expected) |
| Peak magnitude | **0.3841 g** (~77× above healthy baseline) |
| Fault flag | 1 (FAULT) |
| Dashboard | Red flashing UI + 880/440 Hz audio alarm |

The 73 Hz fundamental is consistent with the rotational frequency of the DC motor under test at the applied load. The presence of the second harmonic at 146.5 Hz is physically expected for mechanical imbalance and further validates the result.

---

## Dashboard

The ESP32 hosts a lightweight HTTP server on port 80 serving:

- `GET /` — Full HTML dashboard (terminal-style dark UI)
- `GET /data` — Live JSON: `{"freq": 73.24, "mag": 0.3841, "fault": 1, "time": "4m 44s uptime"}`

Dashboard features:
- Live dominant frequency and magnitude readings
- Fault/OK status with colour-coded visual indicator
- Flashing red background animation on fault detection
- Web Audio API alarm (880 Hz / 440 Hz square wave, alternating at 0.25s intervals)
- Mute toggle for alarm
- 2-second auto-refresh poll

No internet connection required. Accessible from any device (phone, laptop, tablet) on the same Wi-Fi network.

---

## Dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| arduinoFFT | ≥ 2.x | FFT computation with windowing |
| WiFi | ESP32 Arduino core | Wi-Fi connectivity |
| WebServer | ESP32 Arduino core | HTTP server |
| Wire | ESP32 Arduino core | I2C communication |

Install via Arduino Library Manager or PlatformIO.

---

## Setup

1. Clone this repository
2. Open `vibration_analyzer_esp32.ino` in Arduino IDE
3. Set your Wi-Fi credentials:
   ```c
   const char* ssid = "YOUR_WIFI";
   const char* pass = "YOUR_PASS";
   ```
4. Flash to ESP32 (board: ESP32 Dev Module, upload speed: 115200)
5. Open Serial Monitor at 115200 baud — IP address will be printed on connection
6. Navigate to `http://[ESP32-IP]` on any device on the same network

---

## Applications

- Predictive maintenance on industrial motors, pumps, compressors
- Bearing fault detection (BPFO, BPFI frequencies)
- Rotating machinery health monitoring
- Low-cost alternative to commercial vibration analyzers (< ₹500 BOM vs ₹6000+ commercial units)

---

## Author

**Abhiram Kurella**  
B.Tech Electronics and Instrumentation Engineering  
VNR Vignana Jyothi Institute of Engineering & Technology (2023–2027)  
abhiram.kurella@gmail.com
