# 🔌 Astra26 Hardware Wiring & Pinout Guide

Complete wiring schematics, pin mappings, and electrical safety instructions for connecting all sensors to your ESP32 board.

---

## 📌 Master Pinout Table

| Sensor / Module | Sensor Pin | ESP32 Pin | Notes & Resistor Requirements |
|---|---|---|---|
| **DS18B20 Waterproof Temp** | **VCC (Red)** | **3.3V** | Requires **4.7 kΩ pull-up resistor** between 3.3V and Yellow (DATA) |
| | **GND (Black)** | **GND** | Connect to common GND |
| | **DATA (Yellow)** | **GPIO 4** | OneWire digital communication pin |
| **Rain / Water Level Sensor** | **VCC** | **3.3V** | Powers sensor trace board |
| | **GND** | **GND** | Connect to common GND |
| | **AO (Analog Out)** | **GPIO 34** | ADC1 pin (safe to use with WiFi active) |
| **Soil Moisture Sensor** | **VCC** | **3.3V** | Powers capacitive or resistive sensor |
| | **GND** | **GND** | Connect to common GND |
| | **AO (Analog Out)** | **GPIO 33** | ADC1 pin (safe to use with WiFi active) &bull; 0–4095 ADC |
| **LDR Light Sensor Module** | **VCC** | **3.3V** | Module VCC |
| | **GND** | **GND** | Connect to common GND |
| | **AO (Analog Out)** | **GPIO 35** | ADC1 pin (safe to use with WiFi active) |
| **Tilt Switch (SW-520D)** | **Pin 1 (Signal)** | **GPIO 27** | Internal pull-up enabled in software |
| | **Pin 2 (Ground)** | **GND** | Switch shorts to GND when upright / tilted |
| **MPU-6050 6-Axis IMU** | **VCC** | **3.3V** | Powers sensor module |
| | **GND** | **GND** | Connect to common GND |
| | **SDA** | **GPIO 21** | ESP32 hardware I2C Data line |
| | **SCL** | **GPIO 22** | ESP32 hardware I2C Clock line |
| **0-25V DC Voltage Divider** | **VCC (+)** | **Positive Battery terminal** | Measures 0V to 25V DC source (e.g., 12V / 9V / 3.7V) |
| | **GND (-)** | **Negative Battery terminal & ESP32 GND** | **COMMON GROUND IS ESSENTIAL!** |
| | **Signal (S)** | **GPIO 32** | Scaled down 5:1 (Max 3.3V into ESP32 pin) |
| **Status LED** | **Anode (+)** | **GPIO 2** | Built-in blue LED on NodeMCU-32S / ESP32 DevKit |

---

## ⚠️ Critical Electrical Safeguards

### 1. The DS18B20 4.7kΩ Pull-Up Resistor
- The DS18B20 1-Wire protocol requires a pull-up resistor between the **DATA (Yellow)** wire and the **VCC (Red/3.3V)** wire.
- If you don't connect this 4.7kΩ (or 10kΩ in a pinch) resistor, the sensor will read `-127.0 °C` or disconnect.

### 2. ADC1 vs ADC2 on ESP32 (Crucial Rule)
- On the ESP32, **ADC2 pins are disabled when WiFi is transmitting**!
- Therefore, all analog sensors (Rain, Soil Moisture, LDR, Voltage) **MUST** use ADC1:
  - Rain Sensor $\rightarrow$ **GPIO 34** (ADC1)
  - Soil Moisture Sensor $\rightarrow$ **GPIO 33** (ADC1)
  - LDR Light Sensor $\rightarrow$ **GPIO 35** (ADC1)
  - Voltage Sensor $\rightarrow$ **GPIO 32** (ADC1)
- Do **not** move them to GPIO 2, 4, 12, 13, 14, 15, 25, 26!

### 3. Soil Moisture Sensor Scaling & 12-Bit ADC Scale (0 to 4095)
- **12-bit ADC Discrete Levels:**
  - The ESP32 ADC operates with 12-bit resolution: $2^{12} = 4096$ discrete levels ranging strictly from **0 to 4095** (0V = 0, 3.3V = 4095).
- **Capacitive Soil Moisture Sensor v1.2 (Recommended):**
  - Uses high-frequency capacitance measurement with no exposed metal traces in soil (corrosion-proof).
  - Dry in Air: ADC is ~ `3200 to 3500` $\rightarrow$ 0% moisture.
  - Saturated in Water: ADC drops to ~ `1200 to 1400` $\rightarrow$ 100% moisture.
  - Inverted curve: `Moisture % = ((3200 - ADC) / (3200 - 1400)) * 100%`.
- **Resistive Soil Moisture Sensor (LM393 probe):**
  - Direct scale: Dry is ~0-150 ADC, Wet in water rises to ~2500-2900 ADC.
  - Configurable via `SOIL_IS_CAPACITIVE` in `esp32_sensor_node.ino`.

### 4. Voltage Sensor Calibration
- The standard voltage divider has a 5:1 reduction ratio ($R_1 = 30\text{ k}\Omega$, $R_2 = 7.5\text{ k}\Omega$).
- $V_{in} = V_{pin} \times 5.0$.
- With ESP32 nominal 3.3V ADC full-scale, the maximum measurable voltage is $3.3 \times 5 = 16.5\text{ V}$ (or up to 25V on 5V Arduino).
- Always ensure ESP32 GND is tied to the battery/power supply GND.

### 5. Water Level vs Raindrop Sensor Scaling
- **Standard Water Level Sensor (Red striped PCB probe):**
  - When dry in air: Signal line is grounded (ADC is ~ 0 to 80) $\rightarrow$ 0%.
  - When water touches / submerges: Signal voltage rises (ADC is ~ 500 to 2800) $\rightarrow$ 1% to 100%.
  - Configured with `SENSOR_DIRECT_SCALE = true` in `esp32_sensor_node.ino`.
- **Raindrop Module with LM393 Comparator Board:**
  - Some modules output 3.3V (ADC ~ 4095) when dry and drop towards 1200 when wet.
  - If your specific hardware uses this inverted behavior, set `SENSOR_DIRECT_SCALE = false` in `esp32_sensor_node.ino`.

### 6. LDR Ambient Light Sensor Scaling
- **Direct Scale (`LDR_DIRECT_SCALE = true`):**
  - Dark / Covered: ADC is near 0 (~ 50-100) $\rightarrow$ 0% illuminance.
  - Bright light: ADC rises towards 3800 $\rightarrow$ 100% illuminance.
- **Inverted Scale (`LDR_DIRECT_SCALE = false`):**
  - For inverted LM393 boards where dark gives high ADC (~3900) and light drops to low ADC.

### 7. MPU-6050 6-Axis IMU (Angles & Vibration Monitoring)
- Uses ESP32 default I2C pins: **SDA $\rightarrow$ GPIO 21**, **SCL $\rightarrow$ GPIO 22**.
- Default I2C address is `0x68` (or `0x69` if AD0 is tied to 3.3V).
- Firmware automatically detects MPU6050 on boot:
  - **Live Angles:** Pitch ($-90^\circ$ to $+90^\circ$) and Roll ($-180^\circ$ to $+180^\circ$) inclination angles in degrees.
  - **Dynamic Vibrations:** Computes dynamic acceleration deviation $| \sqrt{a_x^2 + a_y^2 + a_z^2} - 1.0\text{ g} |$ and rotational angular velocity $\sqrt{g_x^2 + g_y^2 + g_z^2}$ in $^\circ/\text{s}$.
  - Triggers physical inclination alarms if tilt exceeds $\pm 25^\circ$ or if vibration shock spikes over $0.60\text{ g}$.

---

## 📷 ESP32-CAM Setup Instructions

If you are using an AI-Thinker ESP32-CAM:
1. **Flashing with FTDI / USB-TTL Adapter:**
   - FTDI `VCC (5V)` $\rightarrow$ ESP32-CAM `5V`
   - FTDI `GND` $\rightarrow$ ESP32-CAM `GND`
   - FTDI `TX` $\rightarrow$ ESP32-CAM `U0R (GPIO 3)`
   - FTDI `RX` $\rightarrow$ ESP32-CAM `U0T (GPIO 1)`
   - **Crucial:** Jumper **GPIO 0** to **GND** before clicking Upload in Arduino IDE!
2. **After uploading finishes (CRITICAL STEP):**
   - **Disconnect the GPIO 0 jumper from GND!**
   - Press the **RST (Reset)** button on the underside of the ESP32-CAM board.
   - *If GPIO 0 remains connected to GND, the ESP32-CAM stays frozen in bootloader mode and will never execute your sketch!*
3. **Open Serial Monitor at `115200` baud:**
   - Look for `[Camera] OV2640 camera sensor initialized successfully!`
   - Look for `[WiFi] Connected successfully!`
   - Look for `[HTTP] SUCCESS! Image received by server (Code: 200)`

---

## 🛠️ ESP32-CAM Troubleshooting Checklist

### 1. Camera Initialization Failed (`error 0x102` or `error 0x20003`)
- **Fix:** In Arduino IDE, click **Tools** $\rightarrow$ **PSRAM** $\rightarrow$ select **"Enabled"**.
- Ensure Board is set to **"AI Thinker ESP32-CAM"**.
- Check the 24-pin ribbon cable connecting the OV2640 lens to the board. Gently lift the black clip, re-seat the ribbon cable flat, and snap the clip down firmly.

### 2. Brownout Detector Triggered / Boot Loop
- **Fix:** Power the ESP32-CAM using the **5V pin** with an external supply or quality USB port capable of at least **1.0 Ampere** (1000mA).
- Never power through a weak 3.3V pin from an FTDI adapter during camera captures; the camera + WiFi radio draw up to 500mA peak, causing voltage drops.
- The firmware automatically disables the brownout detector via `RTC_CNTL_BROWN_OUT_REG` to protect against micro-dips.

### 3. Image Upload Failed / No Image on Website
- Verify that your WiFi SSID and Password in `esp32_cam_uploader.ino` match your network.
- Confirm that `SERVER_UPLOAD_URL` is set to `https://astra26.onrender.com/api/camera/upload`.
- Check Serial Monitor logs: our updated firmware streams frames directly over TCP without memory copying, preventing heap crashes even on non-PSRAM setups.
