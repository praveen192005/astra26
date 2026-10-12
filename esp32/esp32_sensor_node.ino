/*
 * =====================================================================================
 *  PROJECT ASTRA26: ESP32 MULTI-SENSOR TELEMETRY NODE
 * =====================================================================================
 *  Microcontroller : ESP32 Dev Module / NodeMCU-32S
 *  Sensors:
 *   1. DS18B20 Waterproof Temperature Sensor (OneWire Data -> GPIO 4)
 *   2. Raindrop / Water Level Sensor         (Analog Output -> GPIO 34 / ADC1)
 *   3. LDR (Light Dependent Resistor) Module (Analog Output -> GPIO 35 / ADC1)
 *   4. DC Voltage Divider Sensor (0-25V)     (Signal Pin    -> GPIO 32 / ADC1)
 *   5. Tilt Sensor Switch (SW-520D / Ball)   (Digital Pin   -> GPIO 27 / Pullup)
 *   6. Status LED                            (Onboard LED   -> GPIO 2)
 *
 *  * IMPORTANT ESP32 ADC NOTE:
 *    GPIO 32, 34, 35 belong to ADC1. ADC1 is 100% compatible with WiFi.
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
#define PIN_LDR_ANALOG    35   // ADC1 - LDR Ambient light analog input
#define PIN_VOLTAGE_ADC   32   // ADC1 - Voltage sensor divider signal input
#define PIN_TILT_DIGITAL  27   // Tilt sensor digital input (active low / pull-up)
#define PIN_LED_STATUS    2    // Onboard Blue LED indicator

// =====================================================================================
// 3. SENSOR CALIBRATION PARAMETERS
// =====================================================================================
// Standard 0-25V voltage divider module has R1 = 30k ohm, R2 = 7.5k ohm (Factor: 5.0)
// ESP32 ADC nominal reference is 3.3V with 12-bit resolution (4095 levels).
// Tune VOLTAGE_CALIBRATION_FACTOR against a digital multimeter if needed.
const float VOLTAGE_DIVIDER_RATIO      = 5.0;   // 30k / 7.5k divider
const float ESP32_ADC_REFERENCE_VOLTS  = 3.3;   // ESP32 nominal 3.3V rail
const float VOLTAGE_CALIBRATION_FACTOR = 1.04;  // Fine-tuning adjustment factor

// Rain sensor calibration
// When completely dry, typical analog reading is ~ 4095. When soaked in water, reading drops to ~ 1000 - 1500.
const int RAIN_ADC_DRY = 4095;
const int RAIN_ADC_WET = 1200;

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
    // Invert reading: Dry (high ADC) -> 0%, Wet (low ADC) -> 100%
    float rainLevelPct = map(constrain(rainRaw, RAIN_ADC_WET, RAIN_ADC_DRY), RAIN_ADC_DRY, RAIN_ADC_WET, 0, 100);
    rainLevelPct = constrain(rainLevelPct, 0.0, 100.0);

    // 3. Read LDR Sensor (ADC1 GPIO 35)
    int ldrRaw = readAveragedADC(PIN_LDR_ANALOG);
    // Map LDR to 0-100% illuminance
    float ldrPct = (ldrRaw / 4095.0) * 100.0;
    ldrPct = constrain(ldrPct, 0.0, 100.0);

    // 4. Read Tilt Sensor (GPIO 27)
    // When tilted, switch contacts open or close based on module orientation.
    // Reading 1 = tilted (tamper), 0 = level (normal)
    int tiltReading = digitalRead(PIN_TILT_DIGITAL);
    int tiltState = (tiltReading == LOW) ? 1 : 0;

    // 5. Read DC Voltage Sensor (ADC1 GPIO 32)
    int voltRaw = readAveragedADC(PIN_VOLTAGE_ADC);
    float pinVolts = (voltRaw / 4095.0) * ESP32_ADC_REFERENCE_VOLTS;
    float measuredVoltage = pinVolts * VOLTAGE_DIVIDER_RATIO * VOLTAGE_CALIBRATION_FACTOR;

    // Calculate Uptime
    unsigned long uptimeSeconds = (millis() - bootTime) / 1000;
    int rssi = WiFi.RSSI();

    // Debug print to Serial Monitor
    Serial.println("\n----------------- TELEMETRY PACKET -----------------");
    Serial.printf(" Temperature   : %.2f °C (%.2f °F)\n", tempC, (tempC * 9.0 / 5.0 + 32.0));
    Serial.printf(" Rain Level    : %.1f %% (ADC: %d)\n", rainLevelPct, rainRaw);
    Serial.printf(" LDR Light     : %.1f %% (ADC: %d)\n", ldrPct, ldrRaw);
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
    jsonPayload += "\"ldrValue\":" + String(ldrPct, 1) + ",";
    jsonPayload += "\"ldrRaw\":" + String(ldrRaw) + ",";
    jsonPayload += "\"tiltState\":" + String(tiltState) + ",";
    jsonPayload += "\"voltage\":" + String(measuredVoltage, 2) + ",";
    jsonPayload += "\"rssi\":" + String(rssi) + ",";
    jsonPayload += "\"uptime\":" + String(uptimeSeconds);
    jsonPayload += "}";

    // Transmit over HTTP POST to Cloud / Local Server
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      
      // Determine if HTTPS or HTTP
      if (String(SERVER_URL).startsWith("https")) {
        WiFiClientSecure *client = new WiFiClientSecure;
        if (client) {
          client->setInsecure(); // Accept Render's TLS certificate without hardcoding root CA
          http.begin(*client, SERVER_URL);
        }
      } else {
        WiFiClient client;
        http.begin(client, SERVER_URL);
      }

      http.addHeader("Content-Type", "application/json");
      http.setTimeout(4000); // 4 second timeout

      // Quick LED blink to indicate transmission
      digitalWrite(PIN_LED_STATUS, LOW);
      int httpResponseCode = http.POST(jsonPayload);
      digitalWrite(PIN_LED_STATUS, HIGH);

      if (httpResponseCode > 0) {
        Serial.printf("[HTTP] POST Response Code: %d\n", httpResponseCode);
        if (httpResponseCode == 200 || httpResponseCode == 201) {
          String response = http.getString();
          Serial.println("[HTTP] Server Response: " + response);
        }
      } else {
        Serial.printf("[HTTP] POST Failed! Error: %s\n", http.errorToString(httpResponseCode).c_str());
      }
      http.end();
    } else {
      Serial.println("[HTTP] WiFi not connected. Skipping telemetry upload.");
    }
  }
}
