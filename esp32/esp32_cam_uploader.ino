/*
 * =====================================================================================
 *  PROJECT ASTRA26: ESP32-CAM LIVE FRAME UPLOADER
 * =====================================================================================
 *  Board: AI-Thinker ESP32-CAM (OV2640 Camera Module)
 *  Function: Periodically captures high-definition JPEG frames and uploads them
 *            to the Astra26 Telemetry Hub endpoint: /api/camera/upload
 *
 *  How to Flash ESP32-CAM in Arduino IDE:
 *  1. Board: "AI Thinker ESP32-CAM"
 *  2. CPU Frequency: 240MHz (WiFi/BT)
 *  3. Flash Frequency: 80MHz | Flash Mode: QIO
 *  4. Partition Scheme: Huge APP (3MB No OTA/1MB SPIFFS)
 *  5. Connect GPIO 0 to GND during flash upload, then disconnect GPIO 0 and press RESET.
 * =====================================================================================
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ---------------- NETWORK & SERVER CONFIG ----------------
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Target Upload URL on Render:
const char* SERVER_UPLOAD_URL = "https://astra26.onrender.com/api/camera/upload";

// Capture interval in milliseconds (e.g. 15 seconds)
const unsigned long CAPTURE_INTERVAL_MS = 15000;
const char* CAM_DEVICE_ID = "esp32-cam-01";

// ---------------- AI-THINKER PIN DEFINITIONS ----------------
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define FLASH_LED_PIN      4   // High-power onboard flash LED (optional)
#define STATUS_LED_PIN    33   // Small red status LED on back (active LOW)

unsigned long lastCaptureTime = 0;

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Frame resolution: VGA (640x480) or SVGA (800x600) for fast cloud transfer
  if (psramFound()) {
    config.frame_size   = FRAMESIZE_VGA;
    config.jpeg_quality = 12; // 0-63, lower means higher quality
    config.fb_count     = 2;
    Serial.println("[Camera] PSRAM detected! High performance buffer allocated.");
  } else {
    config.frame_size   = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count     = 1;
    Serial.println("[Camera] Warning: PSRAM not found. Using QVGA resolution.");
  }

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[Camera] Camera initialization failed with error 0x%x\n", err);
    return;
  }
  Serial.println("[Camera] OV2640 camera sensor initialized successfully!");
}

void uploadPhoto() {
  Serial.println("[Camera] Capturing snapshot frame...");
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[Camera] Frame capture failed!");
    return;
  }

  Serial.printf("[Camera] Frame captured: %u bytes\n", fb->len);

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    bool isHttps = String(SERVER_UPLOAD_URL).startsWith("https");

    if (isHttps) {
      WiFiClientSecure *client = new WiFiClientSecure;
      if (client) {
        client->setInsecure();
        http.begin(*client, SERVER_UPLOAD_URL);
      }
    } else {
      WiFiClient client;
      http.begin(client, SERVER_UPLOAD_URL);
    }

    String boundary = "----WebKitFormBoundaryAstra26ESP32";
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http.setTimeout(8000);

    // Build multipart body header
    String head = "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"deviceId\"\r\n\r\n";
    head += String(CAM_DEVICE_ID) + "\r\n";
    head += "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"file\"; filename=\"snapshot.jpg\"\r\n";
    head += "Content-Type: image/jpeg\r\n\r\n";

    String tail = "\r\n--" + boundary + "--\r\n";

    uint32_t totalLen = head.length() + fb->len + tail.length();
    http.addHeader("Content-Length", String(totalLen));

    // Allocate continuous buffer or send payload
    uint8_t *buffer = (uint8_t*)malloc(totalLen);
    if (buffer) {
      memcpy(buffer, head.c_str(), head.length());
      memcpy(buffer + head.length(), fb->buf, fb->len);
      memcpy(buffer + head.length() + fb->len, tail.c_str(), tail.length());

      digitalWrite(STATUS_LED_PIN, LOW); // LED ON
      int httpCode = http.POST(buffer, totalLen);
      digitalWrite(STATUS_LED_PIN, HIGH); // LED OFF

      free(buffer);

      if (httpCode > 0) {
        Serial.printf("[HTTP] Image Upload Success! Response: %d\n", httpCode);
      } else {
        Serial.printf("[HTTP] Image Upload Failed: %s\n", http.errorToString(httpCode).c_str());
      }
    } else {
      Serial.println("[Error] Memory allocation failed for multipart payload");
    }

    http.end();
  } else {
    Serial.println("[Camera] WiFi disconnected. Image upload skipped.");
  }

  // Release camera buffer
  esp_camera_fb_return(fb);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH); // Off

  Serial.println("\n--- ASTRA26 ESP32-CAM SNAPSHOT ENGINE ---");

  setupCamera();

  // Connect WiFi
  Serial.print("[WiFi] Connecting to: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] Connected! IP: " + WiFi.localIP().toString());
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastCaptureTime >= CAPTURE_INTERVAL_MS) {
    lastCaptureTime = currentMillis;
    uploadPhoto();
  }
}
