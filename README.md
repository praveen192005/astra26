# 🚀 Astra-26: ESP32 Multi-Sensor Real-Time Telemetry & Camera Hub

A full-stack Internet of Things (IoT) solution featuring an ESP32 multi-sensor node, companion ESP32-CAM frame uploader, and an ultra-modern cyber-glassmorphism live web dashboard ready to be hosted on **Render** (free tier).

---

## 📋 Hardware Supported & Pinout

| Sensor / Module | Sensor Pin | ESP32 Pin | Notes & Resistor Requirements |
|---|---|---|---|
| **DS18B20 Temp Probe** | VCC (Red)<br>GND (Black)<br>DATA (Yellow) | **3.3V**<br>**GND**<br>**GPIO 4** | **Requires 4.7 kΩ pull-up resistor** between 3.3V & DATA. |
| **Rain / Water Level** | VCC / GND / AO | **3.3V** / **GND** / **GPIO 34** | Uses **ADC1** (compatible with WiFi). Direct scale for water level probes (0=dry, rises with water); inverted scale option available. |
| **Soil Moisture Sensor** | VCC / GND / AO | **3.3V** / **GND** / **GPIO 33** | Uses **ADC1** (compatible with WiFi). **12-bit ADC provides 4096 discrete levels (Range: 0 to 4095)**. Capacitive v1.2 (dry ~3200-3500 ADC, wet ~1200-1400 ADC). |
| **LDR Light Sensor** | VCC / GND / AO | **3.3V** / **GND** / **GPIO 35** | Uses **ADC1** (compatible with WiFi). Configurable direct & inverted scaling. |
| **MPU-6050 6-Axis IMU** | VCC / GND<br>SDA / SCL | **3.3V** / **GND**<br>**GPIO 21** / **GPIO 22** | Hardware I2C bus (0x68). Measures live Angles (Pitch, Roll in degrees), dynamic Vibrations (g), and rotational angular speed (°/s). |
| **Tilt Switch (SW-520D)** | Signal / GND | **GPIO 27** / **GND** | Internal pull-up enabled in software. Detects incline/tamper. |
| **DC Voltage Divider (0-25V)** | VCC (+) / GND (-)<br>Signal (S) | **Battery +** / **GND**<br>**GPIO 32** | 5:1 scale factor. **ESP32 and battery must share common GND!** |
| **ESP32-CAM (OV2640)** | Standalone board | AI-Thinker Pinout | Periodically uploads live JPEG snapshots to web dashboard. |

> **⚠️ Critical Note on ESP32 ADCs:**
> Always connect analog sensors to **ADC1** pins (GPIO 32, 33, 34, 35). ADC2 pins are disabled by the ESP32 hardware whenever WiFi is transmitting.

---

## 🖥️ Project Structure

```text
astra26/
├── esp32/
│   ├── esp32_sensor_node.ino      # Main ESP32 sensor firmware (WiFi + HTTP JSON POST)
│   ├── esp32_cam_uploader.ino     # ESP32-CAM snapshot uploader (multipart JPEG POST)
│   └── wiring_diagram_guide.md    # Detailed schematics & resistor instructions
├── public/
│   ├── index.html                 # Glassmorphic live IoT dashboard
│   ├── css/
│   │   └── style.css              # Cyber-dark theme, glowing accents, responsive
│   ├── js/
│   │   └── app.js                 # Real-time SSE listener, Chart.js, simulator, audio
│   └── assets/
│       └── camera_standby.jpg     # Standby camera viewfinder asset
├── main.py                        # FastAPI backend (SSE streaming, REST API, static files)
├── requirements.txt               # Python production dependencies
├── render.yaml                    # 1-Click Render Blueprint configuration
├── Procfile                       # Web process definition for Render
└── README.md                      # Complete setup & deployment guide
```

---

## ⚡ Quick Start: Running Locally (Testing on Your PC)

Python 3.11 is already installed on your system. To run the server locally:

```powershell
# 1. Navigate to the project folder
cd c:\Users\BB\OneDrive\Desktop\astra26

# 2. Start the server
python -m uvicorn main:app --host 0.0.0.0 --port 8000 --reload
```

3. Open your browser to **`http://localhost:8000`**.
4. Click **"Simulator"** in the top navigation bar to test sensor sliders and verify real-time updates instantly even without physical hardware plugged in!

---

## ☁️ How to Host on Render (Free Cloud Hosting)

Hosting this dashboard on [Render](https://render.com) takes less than 3 minutes:

### Step 1: Push Project to GitHub
1. Create a new repository on your GitHub account (e.g., `astra26-telemetry`).
2. Run in terminal:
   ```bash
   git init
   git add .
   git commit -m "Initial commit of Astra26 ESP32 IoT hub"
   git branch -M main
   git remote add origin https://github.com/YOUR_USERNAME/astra26-telemetry.git
   git push -u origin main
   ```

### Step 2: Deploy on Render
1. Log in to [dashboard.render.com](https://dashboard.render.com/).
2. Click **New +** $\rightarrow$ **Web Service**.
3. Connect your GitHub repository.
4. Render will automatically read `render.yaml` or you can configure manually:
   - **Environment:** `Python 3`
   - **Build Command:** `pip install -r requirements.txt`
   - **Start Command:** `uvicorn main:app --host 0.0.0.0 --port $PORT`
   - **Plan:** Free
5. Click **Create Web Service**.
6. Render will assign you a live HTTPS URL (e.g., `https://astra26-esp32-telemetry.onrender.com`).

---

## 📡 Flashing the ESP32 Microcontroller

### 1. Arduino IDE Setup
1. Open **Arduino IDE**.
2. Go to **File $\rightarrow$ Preferences** and add the ESP32 board manager URL:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Go to **Tools $\rightarrow$ Board $\rightarrow$ Boards Manager**, search for `esp32`, and install **esp32 by Espressif Systems**.
4. Go to **Sketch $\rightarrow$ Include Library $\rightarrow$ Manage Libraries**, and install:
   - `OneWire` (by Jim Studt / Paul Stoffregen)
   - `DallasTemperature` (by Miles Burton)

### 2. Configure Credentials in `esp32_sensor_node.ino`
Open `esp32/esp32_sensor_node.ino` and update lines 35-43:
```cpp
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Use your Render public URL:
const char* SERVER_URL    = "https://astra26.onrender.com/api/telemetry";
```

### 3. Upload to Board
- Select Board: **ESP32 Dev Module** (or **DOIT ESP32 DEVKIT V1**)
- Select your COM Port
- Click **Upload**
- Open **Serial Monitor** at `115200 baud` to watch the connection handshake and sensor readouts!

---

## 📷 Flashing the ESP32-CAM (Optional)

1. Open `esp32/esp32_cam_uploader.ino`.
2. Update your WiFi SSID, Password, and your Render camera endpoint:
   ```cpp
   const char* SERVER_UPLOAD_URL = "https://astra26-esp32-telemetry.onrender.com/api/camera/upload";
   ```
3. In Arduino IDE:
   - Board: **AI Thinker ESP32-CAM**
   - Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**
4. Connect **GPIO 0 to GND** before clicking Upload. Once flashed, disconnect GPIO 0 from GND and press the Reset button.
5. Watch live photos appear on your web dashboard!

---

## 🌟 Dashboard Features

- 💧 **Waterproof Temperature (DS18B20):** °C/°F toggle, min/max session memory, thermal gauge.
- 🌧️ **Rain & Water Level:** 0-100% fluid immersion animation, raw ADC readout, flood alert threshold.
- 🌱 **Soil Moisture Sensor (0-4095 ADC Scale):** Real-time soil hydration %, raw 12-bit ADC tracking (0 to 4095 discrete levels, 4096 steps), and soil condition irrigation guides.
- ☀️ **LDR Ambient Light:** Dynamic sun/moon glow indicator, Lux estimate, light condition status.
- 🔄 **Gyroscopic Tilt Sensor:** Physical 3D visual bubble that tilts when triggered, with audible tamper alarm.
- 🧭 **MPU-6050 Angles & Vibration Monitoring:** Live Pitch & Roll inclination angle displays with artificial horizon level bars, dynamic vibration shock amplitude ($g$), gyro angular rate ($^\circ/\text{s}$), and automatic shock alerts.
- 🔋 **DC Voltage & Battery Health:** Smart battery profile detection (12V battery / 3.7V Li-Ion / 5V rail), percentage bar.
- 📷 **ESP32-CAM Live Viewport:** Real-time JPEG snapshot viewer with timestamp and expanded modal.
- 📈 **Real-Time Analytics:** Chart.js dual time-series graphs with smooth live data feeds.
- 📜 **Telemetry Ledger:** Historical audit table with live row injection and CSV export.
- 🎮 **In-Browser Hardware Simulator:** Allows testing all features, soil levels, and vibration alarms right from your browser.
