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
| **LDR Light Sensor Module** | **VCC** | **3.3V** | Module VCC |
| | **GND** | **GND** | Connect to common GND |
| | **AO (Analog Out)** | **GPIO 35** | ADC1 pin (safe to use with WiFi active) |
| **Tilt Switch (SW-520D)** | **Pin 1 (Signal)** | **GPIO 27** | Internal pull-up enabled in software |
| | **Pin 2 (Ground)** | **GND** | Switch shorts to GND when upright / tilted |
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
- Therefore, all analog sensors (Rain, LDR, Voltage) **MUST** use ADC1:
  - Rain Sensor $\rightarrow$ **GPIO 34** (ADC1)
  - LDR Sensor $\rightarrow$ **GPIO 35** (ADC1)
  - Voltage Sensor $\rightarrow$ **GPIO 32** (ADC1)
- Do **not** move them to GPIO 2, 4, 12, 13, 14, 15, 25, 26!

### 3. Voltage Sensor Calibration
- The standard voltage divider has a 5:1 reduction ratio ($R_1 = 30\text{ k}\Omega$, $R_2 = 7.5\text{ k}\Omega$).
- $V_{in} = V_{pin} \times 5.0$.
- With ESP32 nominal 3.3V ADC full-scale, the maximum measurable voltage is $3.3 \times 5 = 16.5\text{ V}$ (or up to 25V on 5V Arduino).
- Always ensure ESP32 GND is tied to the battery/power supply GND.

---

## 📷 ESP32-CAM Setup Instructions

If you are using an AI-Thinker ESP32-CAM:
1. **Flashing with FTDI / USB-TTL Adapter:**
   - FTDI `VCC (5V)` $\rightarrow$ ESP32-CAM `5V`
   - FTDI `GND` $\rightarrow$ ESP32-CAM `GND`
   - FTDI `TX` $\rightarrow$ ESP32-CAM `U0R (GPIO 3)`
   - FTDI `RX` $\rightarrow$ ESP32-CAM `U0T (GPIO 1)`
   - **Crucial:** Jumper **GPIO 0** to **GND** before clicking Upload in Arduino IDE!
2. After uploading finishes:
   - Disconnect **GPIO 0** from **GND**.
   - Press the **RESET** button on the back of the camera board.
3. Open Serial Monitor at `115200` baud to confirm WiFi connection and image upload logs.
