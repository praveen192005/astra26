/*
 * =====================================================================================
 *  PROJECT ASTRA26: ESP32 MULTI-SENSOR TELEMETRY NODE
 * =====================================================================================
 *  Microcontroller : ESP32 Dev Module / NodeMCU-32S
 *  Sensors:
 *   1. DS18B20 Waterproof Temperature Sensor (OneWire Data -> GPIO 4)
 *   2. Raindrop / Water Level Sensor         (Analog Output -> GPIO 34 / ADC1)
 *   3. Soil Moisture Sensor (Capacitive/Res) (Analog Output -> GPIO 33 / ADC1)
 *      * Resolution: 12-bit ADC -> 4096 discrete levels (Range: 0 to 4095)
 *   4. LDR (Light Dependent Resistor) Module (Analog Output -> GPIO 35 / ADC1)
 *   5. DC Voltage Divider Sensor (0-25V)     (Signal Pin    -> GPIO 32 / ADC1)
 *   6. Tilt Sensor Switch (SW-520D / Ball)   (Digital Pin   -> GPIO 27 / Pullup)
 *   7. MPU-6050 6-Axis Motion Sensor / IMU   (I2C SDA -> GPIO 21, SCL -> GPIO 22)
 *      * Measures inclination angles (Pitch/Roll) & dynamic vibrations (g / °/s)
 *   8. Status LED                            (Onboard LED   -> GPIO 2)
 *
 *  * IMPORTANT ESP32 ADC NOTE:
 *    GPIO 32, 33, 34, 35 belong to ADC1. ADC1 is 100% compatible with WiFi.
 *    (Never use ADC2 pins for analog sensors while WiFi is transmitting).
 *
 *  Libraries Required (Install in Arduino IDE Library Manager):
 *   - OneWire by Jim Studt, Paul Stoffregen
 *   - DallasTemperature by Miles Burton
 * =====================================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>

// =====================================================================================
// 1. NETWORK & SERVER CONFIGURATION
// =====================================================================================
// Replace with your WiFi credentials
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Target Server URL on Render:
const char* SERVER_URL    = "https://astra26.onrender.com/api/telemetry";

// Telemetry interval (in milliseconds)
const unsigned long SEND_INTERVAL_MS = 3000; // Send telemetry every 3 seconds
const char* DEVICE_ID = "esp32-node-01";

// =====================================================================================
// 2. HARDWARE PIN DEFINITIONS
// =====================================================================================
#define PIN_TEMP_DS18B20  4    // OneWire Bus (requires 4.7k pull-up resistor between 3.3V and Data)
#define PIN_RAIN_ANALOG   34   // ADC1 - Rain / Water level analog input
#define PIN_SOIL_ANALOG   33   // ADC1 - Soil Moisture analog input (12-bit: 0-4095)
#define PIN_LDR_ANALOG    35   // ADC1 - LDR Ambient light analog input
#define PIN_VOLTAGE_ADC   32   // ADC1 - Voltage sensor divider signal input
#define PIN_TILT_DIGITAL  27   // Tilt sensor digital input (active low / pull-up)
#define PIN_LED_STATUS    2    // Onboard Blue LED indicator
#define PIN_I2C_SDA       21   // I2C SDA for MPU6050 6-Axis Motion Sensor
#define PIN_I2C_SCL       22   // I2C SCL for MPU6050 6-Axis Motion Sensor
#define MPU6050_I2C_ADDR  0x68 // Default I2C Address (0x68 when AD0 -> GND, 0x69 when AD0 -> 3.3V)

// =====================================================================================
// 3. SENSOR CALIBRATION PARAMETERS
// =====================================================================================
// Standard 0-25V voltage divider module has R1 = 30k ohm, R2 = 7.5k ohm (Factor: 5.0)
// ESP32 ADC nominal reference is 3.3V with 12-bit resolution (4095 levels).
// Tune VOLTAGE_CALIBRATION_FACTOR against a digital multimeter if needed.
const float VOLTAGE_DIVIDER_RATIO      = 5.0;   // 30k / 7.5k divider
const float ESP32_ADC_REFERENCE_VOLTS  = 3.3;   // ESP32 nominal 3.3V rail
const float VOLTAGE_CALIBRATION_FACTOR = 1.04;  // Fine-tuning adjustment factor

// Soil Moisture Sensor Calibration & 12-Bit ADC Scale
// -------------------------------------------------------------------------------------
// ESP32 12-bit ADC provides 4096 discrete levels: Range from 0 to 4095.
//
// Sensor Type A: Capacitive Soil Moisture Sensor v1.2 (Corrosion resistant, recommended):
//   - Air / Bone Dry: Output voltage ~ 2.8V - 3.0V -> Raw ADC ~ 3200 to 3500 (0% moisture)
//   - Submerged in Water: Output voltage ~ 1.2V - 1.4V -> Raw ADC ~ 1200 to 1400 (100% moisture)
//   - Inverted scale: Lower ADC = Higher Moisture.
//
// Sensor Type B: Resistive Soil Moisture Sensor (LM393 probe module):
//   - Dry Soil: Low conductance -> Raw ADC ~ 0 to 200 (0% moisture)
//   - Wet / Saturated Soil: High conductance -> Raw ADC ~ 2500 to 3000 (100% moisture)
//   - Direct scale: Higher ADC = Higher Moisture.
// -------------------------------------------------------------------------------------
const bool SOIL_IS_CAPACITIVE = true; // Set true for Capacitive v1.2, false for direct Resistive

// Capacitive scale calibration (Inverted)
const int SOIL_ADC_AIR_DRY   = 3200;  // ADC reading when sensor probe is in air (0% dry)
const int SOIL_ADC_WATER_WET = 1400;  // ADC reading when submerged in water (100% saturated)

// Resistive scale calibration (Direct)
const int SOIL_RESISTIVE_DRY = 150;   // Dry soil lower ADC limit (0%)
const int SOIL_RESISTIVE_WET = 2900;  // Wet soil upper ADC limit (100%)

// Water Level / Rain Sensor Calibration
// -------------------------------------------------------------------------------------
// Standard Water Level Sensor (resistive striped probe):
//   - When completely DRY in air: Raw ADC is ~ 0 - 80 (pulled to GND) -> 0%
//   - When immersed in WATER: Water conducts across traces, ADC rises to ~1500 - 3000 -> 100%
// Inverted Raindrop Sensor (LM393 module AO pin):
//   - When completely DRY: Raw ADC is ~ 4095 -> 0%
//   - When soaked in water: Raw ADC drops to ~ 1200 -> 100%
// -------------------------------------------------------------------------------------
// Set SENSOR_DIRECT_SCALE to true for standard Water Level Sensors (recommended default).
// Set to false only if using an inverted Raindrop module.
const bool SENSOR_DIRECT_SCALE = true;

// Direct scale calibration (Standard Water Level Sensor)
const int WATER_ADC_DRY_THRESHOLD = 80;    // ADC values below this are treated as 0% (Dry)
const int WATER_ADC_MAX_SUBMERGED = 2800;  // Typical max ADC reading when fully submerged (100%)

// Inverted scale calibration (Inverted Raindrop plate module)
const int RAIN_ADC_DRY = 4095;
const int RAIN_ADC_WET = 1200;

// LDR Ambient Light Sensor Calibration
// -------------------------------------------------------------------------------------
// Standard LDR photoresistor modules come in two circuit types:
// 1. Direct Scale: Dark (~0-100 ADC) -> 0%, Bright (~3800 ADC) -> 100%
// 2. Inverted Scale: Dark (~3900 ADC) -> 0%, Bright (~400 ADC) -> 100%
// -------------------------------------------------------------------------------------
// Set LDR_DIRECT_SCALE to true if your module reads low in dark and high in light.
// Set LDR_DIRECT_SCALE to false if your module reads high in dark and drops in light.
const bool LDR_DIRECT_SCALE = true;

// Direct scale calibration thresholds (Standard LDR)
const int LDR_ADC_DARK       = 100;   // ADC values below this = 0% (Dark / Covered)
const int LDR_ADC_BRIGHT     = 3800;  // ADC values near full scale = 100% (Bright light)

// Inverted scale calibration thresholds (Inverted LDR modules)
const int LDR_ADC_INV_DARK   = 3900;  // ADC when covered = 0%
const int LDR_ADC_INV_BRIGHT = 400;   // ADC in bright light = 100%

// MPU6050 6-Axis Motion & IMU Telemetry State
bool mpuAvailable = false;
float accelX = 0.0, accelY = 0.0, accelZ = 1.0; // In g
float gyroX = 0.0, gyroY = 0.0, gyroZ = 0.0;    // In deg/s
float pitchAngle = 0.0, rollAngle = 0.0;        // In degrees (Inclination angles)
float totalAccelG = 1.0;                        // Total acceleration vector magnitude
float vibrationG = 0.0;                         // Dynamic vibration amplitude |totalG - 1.0g|
float gyroMag = 0.0;                            // Rotational speed/vibration in deg/s
float mpuTempC = 0.0;                           // IMU internal die temp

// Setup OneWire & Dallas Temperature instance
OneWire oneWire(PIN_TEMP_DS18B20);
DallasTemperature tempSensors(&oneWire);

// Time tracking
unsigned long lastSendTime = 0;
unsigned long bootTime = 0;

// =====================================================================================
// HELPER FUNCTIONS
// =====================================================================================

// Perform 10-sample rolling average on ADC pin for noise immunity
int readAveragedADC(int pin, int samples = 10) {
  long total = 0;
  for (int i = 0; i < samples; i++) {
    total += analogRead(pin);
    delay(2);
  }
  return (int)(total / samples);
}

// =====================================================================================
// MPU6050 I2C DRIVER (Native Wire.h - no extra library needed)
// =====================================================================================

// Initialize MPU6050 via Wire I2C
bool initMPU6050() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(100000); // 100 kHz standard I2C clock
  
  // Try wake up at primary address 0x68
  Wire.beginTransmission(MPU6050_I2C_ADDR);
  Wire.write(0x6B); // PWR_MGMT_1 register
  Wire.write(0x00); // Set to 0 to wake up device (clears sleep bit)
  byte error = Wire.endTransmission();
  
  if (error == 0) {
    Serial.println("[MPU6050] Sensor detected & initialized at I2C 0x68!");
    return true;
  }
  
  // Try alternate address 0x69 (if AD0 pin is tied to 3.3V)
  Wire.beginTransmission(0x69);
  Wire.write(0x6B);
  Wire.write(0x00);
  if (Wire.endTransmission() == 0) {
    Serial.println("[MPU6050] Sensor detected & initialized at I2C 0x69!");
    return true;
  }

  Serial.println("[MPU6050] Notice: Not detected on I2C bus (SDA:21, SCL:22). Telemetry running in standby.");
  return false;
}

// Read 6-axis acceleration, gyro, calculate Pitch/Roll angles and Vibration intensity
void readMPU6050() {
  if (!mpuAvailable) {
    static unsigned long lastMpuRetry = 0;
    if (millis() - lastMpuRetry > 5000) {
      lastMpuRetry = millis();
      mpuAvailable = initMPU6050();
    }
    if (!mpuAvailable) {
      pitchAngle = 0.0;
      rollAngle = 0.0;
      vibrationG = 0.0;
      gyroMag = 0.0;
      totalAccelG = 1.0;
      return;
    }
  }

  Wire.beginTransmission(MPU6050_I2C_ADDR);
  Wire.write(0x3B); // Starting register for Accel X High byte
  if (Wire.endTransmission(false) != 0) {
    mpuAvailable = false;
    vibrationG = 0.0;
    gyroMag = 0.0;
    return;
  }

  // Request 14 bytes: Accel X,Y,Z (6B) + Temp (2B) + Gyro X,Y,Z (6B)
  if (Wire.requestFrom((uint8_t)MPU6050_I2C_ADDR, (size_t)14, true) == 14) {
    int16_t rawAx = (Wire.read() << 8) | Wire.read();
    int16_t rawAy = (Wire.read() << 8) | Wire.read();
    int16_t rawAz = (Wire.read() << 8) | Wire.read();
    int16_t rawT  = (Wire.read() << 8) | Wire.read();
    int16_t rawGx = (Wire.read() << 8) | Wire.read();
    int16_t rawGy = (Wire.read() << 8) | Wire.read();
    int16_t rawGz = (Wire.read() << 8) | Wire.read();

    // Scale Accelerometer (+/- 2g range: 16384 LSB/g)
    accelX = rawAx / 16384.0;
    accelY = rawAy / 16384.0;
    accelZ = rawAz / 16384.0;

    // Scale Gyroscope (+/- 250 deg/s range: 131 LSB/(deg/s))
    gyroX = rawGx / 131.0;
    gyroY = rawGy / 131.0;
    gyroZ = rawGz / 131.0;

    // Internal chip temperature
    mpuTempC = (rawT / 340.0) + 36.53;

    // 1. Calculate Pitch and Roll Angles (in degrees)
    pitchAngle = atan2(-accelX, sqrt(accelY * accelY + accelZ * accelZ)) * 180.0 / PI;
    rollAngle  = atan2(accelY, accelZ) * 180.0 / PI;

    // 2. Total Acceleration Magnitude (in g)
    totalAccelG = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);

    // 3. Vibration Level (Dynamic acceleration amplitude deviation from stationary 1.0g gravity)
    vibrationG = abs(totalAccelG - 1.0);

    // 4. Rotational Vibration / Angular Velocity Magnitude (deg/s)
    gyroMag = sqrt(gyroX * gyroX + gyroY * gyroY + gyroZ * gyroZ);
  }
}

// Connect to WiFi with visual LED heartbeat
void connectWiFi() {
  Serial.println();
  Serial.print("[WiFi] Connecting to: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    digitalWrite(PIN_LED_STATUS, !digitalRead(PIN_LED_STATUS)); // Toggle LED
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(PIN_LED_STATUS, HIGH); // Solid ON when connected
    Serial.println("\n[WiFi] Connected successfully!");
    Serial.print("[WiFi] ESP32 Local IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WiFi] Signal Strength (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    digitalWrite(PIN_LED_STATUS, LOW);
    Serial.println("\n[WiFi] Warning: Connection timed out. Will retry in loop.");
  }
}

// =====================================================================================
// SETUP
// =====================================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n========================================================");
  Serial.println("      ASTRA26 - ESP32 MULTI-SENSOR TELEMETRY HUB       ");
  Serial.println("========================================================");

  // Configure Pin Modes
  pinMode(PIN_LED_STATUS, OUTPUT);
  pinMode(PIN_TILT_DIGITAL, INPUT_PULLUP);
  
  // Configure ADC resolution (12-bit: 0 - 4095) & attenuation for 0-3.3V range
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db); // Full-scale range ~ 0V - 3.3V

  // Initialize Dallas Temperature Sensor
  Serial.println("[Sensors] Initializing DS18B20 waterproof temperature probe...");
  tempSensors.begin();
  int deviceCount = tempSensors.getDeviceCount();
  Serial.print("[Sensors] DS18B20 probes detected: ");
  Serial.println(deviceCount);

  // Initialize MPU6050 6-Axis Motion & IMU Sensor
  Serial.println("[Sensors] Initializing MPU6050 (I2C SDA: GPIO 21, SCL: GPIO 22)...");
  mpuAvailable = initMPU6050();

  // Connect to WiFi
  connectWiFi();
  bootTime = millis();
}

// =====================================================================================
// MAIN LOOP
// =====================================================================================
void loop() {
  unsigned long currentMillis = millis();

  // Maintain WiFi Connection
  if (WiFi.status() != WL_CONNECTED && (currentMillis - lastSendTime > 10000)) {
    Serial.println("[WiFi] Reconnecting...");
    WiFi.disconnect();
    WiFi.reconnect();
  }

  // Periodic Telemetry Transmission
  if (currentMillis - lastSendTime >= SEND_INTERVAL_MS) {
    lastSendTime = currentMillis;

    // 1. Read Waterproof Temperature Sensor (DS18B20)
    tempSensors.requestTemperatures();
    float tempC = tempSensors.getTempCByIndex(0);
    // If sensor disconnected, DS18B20 returns -127.0
    if (tempC < -55.0 || tempC > 125.0) {
      Serial.println("[Warn] DS18B20 probe disconnected or read error! (Check 4.7k resistor)");
      tempC = 0.0;
    }

    // 2. Read Rain / Water Level Sensor (ADC1 GPIO 34)
    int rainRaw = readAveragedADC(PIN_RAIN_ANALOG);
    float rainLevelPct = 0.0;

    if (SENSOR_DIRECT_SCALE) {
      // Direct Scale (Standard Water Level Sensor):
      // Dry in air produces low ADC (0 - 80). Submerged in water produces higher ADC (up to ~2800).
      if (rainRaw <= WATER_ADC_DRY_THRESHOLD) {
        rainLevelPct = 0.0;
      } else {
        rainLevelPct = ((float)(rainRaw - WATER_ADC_DRY_THRESHOLD) / 
                        (float)(WATER_ADC_MAX_SUBMERGED - WATER_ADC_DRY_THRESHOLD)) * 100.0;
        rainLevelPct = constrain(rainLevelPct, 0.0, 100.0);
      }
    } else {
      // Inverted Scale (Raindrop module with pull-up):
      // Dry plate is high ADC (~4095). Soaked in water drops to ~1200.
      if (rainRaw >= RAIN_ADC_DRY) {
        rainLevelPct = 0.0;
      } else {
        rainLevelPct = ((float)(RAIN_ADC_DRY - rainRaw) / 
                        (float)(RAIN_ADC_DRY - RAIN_ADC_WET)) * 100.0;
        rainLevelPct = constrain(rainLevelPct, 0.0, 100.0);
      }
    }

    // 3. Read Soil Moisture Sensor (ADC1 GPIO 33)
    // ESP32 12-bit ADC provides 4096 discrete levels (0 to 4095).
    int soilRaw = readAveragedADC(PIN_SOIL_ANALOG);
    float soilMoisturePct = 0.0;

    if (SOIL_IS_CAPACITIVE) {
      // Capacitive Soil Moisture Sensor v1.2 (Corrosion-free):
      // In dry air / dry soil: High ADC (~3200 to 3500) -> 0% moisture
      // In water / saturated soil: Low ADC (~1200 to 1400) -> 100% moisture
      if (soilRaw >= SOIL_ADC_AIR_DRY) {
        soilMoisturePct = 0.0;
      } else if (soilRaw <= SOIL_ADC_WATER_WET) {
        soilMoisturePct = 100.0;
      } else {
        soilMoisturePct = ((float)(SOIL_ADC_AIR_DRY - soilRaw) / 
                           (float)(SOIL_ADC_AIR_DRY - SOIL_ADC_WATER_WET)) * 100.0;
        soilMoisturePct = constrain(soilMoisturePct, 0.0, 100.0);
      }
    } else {
      // Resistive Soil Moisture Sensor (Direct scale):
      // Dry in air produces low ADC (0 - 150) -> 0% moisture
      // Submerged in wet soil produces higher ADC (up to ~2900) -> 100% moisture
      if (soilRaw <= SOIL_RESISTIVE_DRY) {
        soilMoisturePct = 0.0;
      } else if (soilRaw >= SOIL_RESISTIVE_WET) {
        soilMoisturePct = 100.0;
      } else {
        soilMoisturePct = ((float)(soilRaw - SOIL_RESISTIVE_DRY) / 
                           (float)(SOIL_RESISTIVE_WET - SOIL_RESISTIVE_DRY)) * 100.0;
        soilMoisturePct = constrain(soilMoisturePct, 0.0, 100.0);
      }
    }

    // 4. Read LDR Ambient Light Sensor (ADC1 GPIO 35)
    int ldrRaw = readAveragedADC(PIN_LDR_ANALOG);
    float ldrPct = 0.0;
    if (LDR_DIRECT_SCALE) {
      // Direct Scale: Dark is low ADC (~0 - 100), Bright light is high ADC (~3800)
      if (ldrRaw <= LDR_ADC_DARK) {
        ldrPct = 0.0;
      } else {
        ldrPct = ((float)(ldrRaw - LDR_ADC_DARK) / 
                  (float)(LDR_ADC_BRIGHT - LDR_ADC_DARK)) * 100.0;
        ldrPct = constrain(ldrPct, 0.0, 100.0);
      }
    } else {
      // Inverted Scale: Dark is high ADC (~3900), Bright light drops towards ~400
      if (ldrRaw >= LDR_ADC_INV_DARK) {
        ldrPct = 0.0;
      } else {
        ldrPct = ((float)(LDR_ADC_INV_DARK - ldrRaw) / 
                  (float)(LDR_ADC_INV_DARK - LDR_ADC_INV_BRIGHT)) * 100.0;
        ldrPct = constrain(ldrPct, 0.0, 100.0);
      }
    }

    // 5. Read MPU6050 6-Axis Motion Sensor (I2C SDA: GPIO 21, SCL: GPIO 22)
    // Measures live Angles (Pitch, Roll) & dynamic Vibrations
    readMPU6050();

    // 6. Read Tilt Sensor (SW-520D Ball Switch + MPU6050 Dynamic Incline Trigger)
    int tiltReading = digitalRead(PIN_TILT_DIGITAL);
    int tiltState = (tiltReading == LOW) ? 1 : 0;
    // Trigger tilt alert if mechanical switch flips OR if MPU6050 pitch/roll exceeds 25°
    if (mpuAvailable && (abs(pitchAngle) > 25.0 || abs(rollAngle) > 25.0)) {
      tiltState = 1;
    }

    // 7. Read DC Voltage Sensor (ADC1 GPIO 32)
    int voltRaw = readAveragedADC(PIN_VOLTAGE_ADC);
    float pinVolts = (voltRaw / 4095.0) * ESP32_ADC_REFERENCE_VOLTS;
    float measuredVoltage = pinVolts * VOLTAGE_DIVIDER_RATIO * VOLTAGE_CALIBRATION_FACTOR;

    // Calculate Uptime
    unsigned long uptimeSeconds = (millis() - bootTime) / 1000;
    int rssi = WiFi.RSSI();

    // Debug print to Serial Monitor
    Serial.println("\n----------------- TELEMETRY PACKET -----------------");
    Serial.printf(" Temperature   : %.2f °C (%.2f °F)\n", tempC, (tempC * 9.0 / 5.0 + 32.0));
    Serial.printf(" Water / Rain  : %.1f %% (ADC: %d) [%s]\n", 
                  rainLevelPct, rainRaw, SENSOR_DIRECT_SCALE ? "Direct Scale" : "Inverted Scale");
    Serial.printf(" Soil Moisture : %.1f %% (ADC: %d / 4095) [%s | 12-bit: 0-4095, 4096 levels]\n",
                  soilMoisturePct, soilRaw, SOIL_IS_CAPACITIVE ? "Capacitive Scale" : "Resistive Scale");
    Serial.printf(" LDR Light     : %.1f %% (ADC: %d) [%s]\n", 
                  ldrPct, ldrRaw, LDR_DIRECT_SCALE ? "Direct Scale" : "Inverted Scale");
    if (mpuAvailable) {
      Serial.printf(" MPU6050 Angles: Pitch: %.1f° | Roll: %.1f°\n", pitchAngle, rollAngle);
      Serial.printf(" MPU6050 Vib   : Vibration: %.2f g | Gyro Rate: %.1f °/s | Total G: %.2f g\n", 
                    vibrationG, gyroMag, totalAccelG);
      Serial.printf(" MPU6050 Accel : (%.2f, %.2f, %.2f) g | Gyro: (%.1f, %.1f, %.1f) °/s | Die: %.1f °C\n",
                    accelX, accelY, accelZ, gyroX, gyroY, gyroZ, mpuTempC);
    } else {
      Serial.println(" MPU6050 IMU   : Standby (Connect SDA->GPIO 21, SCL->GPIO 22)");
    }
    Serial.printf(" Tilt State    : %s (Raw: %d)\n", (tiltState ? "TILT TRIGGERED!" : "Level Normal"), tiltReading);
    Serial.printf(" Voltage       : %.2f V (ADC: %d)\n", measuredVoltage, voltRaw);
    Serial.printf(" WiFi Signal   : %d dBm | Uptime: %lu s\n", rssi, uptimeSeconds);
    Serial.println("----------------------------------------------------");

    // Construct JSON Payload
    String jsonPayload = "{";
    jsonPayload += "\"deviceId\":\"" + String(DEVICE_ID) + "\",";
    jsonPayload += "\"temperature\":" + String(tempC, 2) + ",";
    jsonPayload += "\"rainLevel\":" + String(rainLevelPct, 1) + ",";
    jsonPayload += "\"rainRaw\":" + String(rainRaw) + ",";
    jsonPayload += "\"soilMoisture\":" + String(soilMoisturePct, 1) + ",";
    jsonPayload += "\"soilRaw\":" + String(soilRaw) + ",";
    jsonPayload += "\"soilMaxRaw\":4095,";
    jsonPayload += "\"ldrValue\":" + String(ldrPct, 1) + ",";
    jsonPayload += "\"ldrRaw\":" + String(ldrRaw) + ",";
    jsonPayload += "\"tiltState\":" + String(tiltState) + ",";
    jsonPayload += "\"voltage\":" + String(measuredVoltage, 2) + ",";
    jsonPayload += "\"rssi\":" + String(rssi) + ",";
    jsonPayload += "\"uptime\":" + String(uptimeSeconds) + ",";
    jsonPayload += "\"mpuConnected\":" + String(mpuAvailable ? "true" : "false") + ",";
    jsonPayload += "\"pitch\":" + String(pitchAngle, 1) + ",";
    jsonPayload += "\"roll\":" + String(rollAngle, 1) + ",";
    jsonPayload += "\"vibration\":" + String(vibrationG, 2) + ",";
    jsonPayload += "\"vibrationPct\":" + String(constrain((vibrationG / 1.5) * 100.0, 0.0, 100.0), 1) + ",";
    jsonPayload += "\"accelX\":" + String(accelX, 2) + ",";
    jsonPayload += "\"accelY\":" + String(accelY, 2) + ",";
    jsonPayload += "\"accelZ\":" + String(accelZ, 2) + ",";
    jsonPayload += "\"totalG\":" + String(totalAccelG, 2) + ",";
    jsonPayload += "\"gyroX\":" + String(gyroX, 1) + ",";
    jsonPayload += "\"gyroY\":" + String(gyroY, 1) + ",";
    jsonPayload += "\"gyroZ\":" + String(gyroZ, 1) + ",";
    jsonPayload += "\"gyroMag\":" + String(gyroMag, 1);
    jsonPayload += "}";

    // Transmit over HTTP POST to Cloud / Local Server
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      WiFiClientSecure *secureClient = nullptr;
      WiFiClient *plainClient = nullptr;
      
      // Determine if HTTPS or HTTP
      if (String(SERVER_URL).startsWith("https")) {
        secureClient = new WiFiClientSecure;
        if (secureClient) {
          secureClient->setInsecure(); // Accept Render's TLS certificate without hardcoding root CA
          http.begin(*secureClient, SERVER_URL);
        }
      } else {
        plainClient = new WiFiClient;
        if (plainClient) {
          http.begin(*plainClient, SERVER_URL);
        }
      }

      http.addHeader("Content-Type", "application/json");
      http.setTimeout(10000); // 10 second timeout for Render cloud

      // Quick LED blink to indicate transmission
      digitalWrite(PIN_LED_STATUS, LOW);
      int httpResponseCode = http.POST(jsonPayload);
      digitalWrite(PIN_LED_STATUS, HIGH);

      if (httpResponseCode > 0) {
        Serial.printf("[HTTP] POST Response Code: %d (Free Heap: %u bytes)\n", httpResponseCode, ESP.getFreeHeap());
        if (httpResponseCode == 200 || httpResponseCode == 201) {
          String response = http.getString();
          Serial.println("[HTTP] Server Response: " + response);
        }
      } else {
        Serial.printf("[HTTP] POST Failed! Error: %s (Free Heap: %u bytes)\n", http.errorToString(httpResponseCode).c_str(), ESP.getFreeHeap());
      }
      http.end();

      // Clean up client to eliminate heap memory leak
      if (secureClient) {
        delete secureClient;
        secureClient = nullptr;
      }
      if (plainClient) {
        delete plainClient;
        plainClient = nullptr;
      }
    } else {
      Serial.println("[HTTP] WiFi not connected. Skipping telemetry upload.");
    }
  }
}
