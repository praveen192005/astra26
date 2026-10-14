/*
 * =====================================================================================
 *  PROJECT ASTRA26: ESP32-CAM ROBUST LIVE FRAME UPLOADER
 * =====================================================================================
 *  Board: AI-Thinker ESP32-CAM (OV2640 Camera Module)
 *  Function: Periodically captures snapshot frames and uploads them to Astra26 Hub:
 *            Endpoint: /api/camera/upload
 *
 *  HARDWARE PREREQUISITES & FLASHING GUIDE:
 *  1. Arduino IDE Board Selection:
 *     - Board: "AI Thinker ESP32-CAM" (or "ESP32 Wrover Module")
 *     - Partition Scheme: "Huge APP (3MB No OTA/1MB SPIFFS)"
 *     - PSRAM: "Enabled" (Tools -> PSRAM -> Enabled)
 *     - Flash Frequency: 80MHz | Flash Mode: QIO
 *
 *  2. Flashing Connections with FTDI / USB-TTL Adapter:
 *     - FTDI VCC (5V)  -> ESP32-CAM 5V (DO NOT USE 3.3V pin - camera requires up to 500mA!)
 *     - FTDI GND       -> ESP32-CAM GND
 *     - FTDI TX        -> ESP32-CAM U0R (GPIO 3)
 *     - FTDI RX        -> ESP32-CAM U0T (GPIO 1)
 *     - JUMPER GPIO 0 to GND before plugging in USB to enter flash mode.
 *     - Press RESET button on ESP32-CAM, then click "Upload" in Arduino IDE.
 *
 *  3. After Upload Completes:
 *     - CRITICAL: DISCONNECT GPIO 0 from GND!
 *     - Press the RST (Reset) button on the ESP32-CAM to begin normal boot.
 *     - Open Serial Monitor at 115200 baud.
 * =====================================================================================
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

// Disable hardware brownout detector during high-current camera + WiFi bursts
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// =====================================================================================
// 1. NETWORK & SERVER CONFIGURATION
// =====================================================================================
// Replace with your WiFi credentials:
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Target Upload URL:
// For Render Cloud deployment:
const char* SERVER_UPLOAD_URL = "https://astra26.onrender.com/api/camera/upload";
// For Local Testing (if running backend on your PC):
// const char* SERVER_UPLOAD_URL = "http://192.168.1.100:8000/api/camera/upload";

// Capture interval in milliseconds (e.g. 15 seconds)
const unsigned long CAPTURE_INTERVAL_MS = 15000;
const char* CAM_DEVICE_ID = "esp32-cam-01";

// =====================================================================================
// 2. AI-THINKER ESP32-CAM PIN DEFINITIONS
// =====================================================================================
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

#define FLASH_LED_PIN      4   // Bright flash LED (active HIGH)
#define STATUS_LED_PIN    33   // Small red status LED on back (active LOW)

// Timer tracker
unsigned long lastCaptureTime = 0;
bool cameraInitialized = false;

// URL Parser structure
struct ServerEndpoint {
  bool isHttps;
  String host;
  int port;
  String path;
};

ServerEndpoint parseServerUrl(const char* urlStr) {
  ServerEndpoint ep;
  String url = String(urlStr);
  ep.isHttps = url.startsWith("https://");
  
  int protoEnd = url.indexOf("://");
  String rem = (protoEnd != -1) ? url.substring(protoEnd + 3) : url;
  
  int slashIdx = rem.indexOf('/');
  String hostPort = (slashIdx != -1) ? rem.substring(0, slashIdx) : rem;
  ep.path = (slashIdx != -1) ? rem.substring(slashIdx) : "/";
  
  int colonIdx = hostPort.indexOf(':');
  if (colonIdx != -1) {
    ep.host = hostPort.substring(0, colonIdx);
    ep.port = hostPort.substring(colonIdx + 1).toInt();
  } else {
    ep.host = hostPort;
    ep.port = ep.isHttps ? 443 : 80;
  }
  return ep;
}

// =====================================================================================
// 3. CAMERA HARDWARE SETUP
// =====================================================================================
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
  
  // Standard OV2640 clock frequency
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Frame resolution & buffer configuration:
  // CAMERA_GRAB_LATEST strictly requires fb_count > 1.
  // If PSRAM is enabled, allocate 2 buffers with CAMERA_GRAB_LATEST.
  // If PSRAM is disabled, allocate 1 buffer with CAMERA_GRAB_WHEN_EMPTY to prevent error 0x102.
  if (psramFound()) {
    config.frame_size   = FRAMESIZE_VGA;  // 640x480 crisp resolution
    config.jpeg_quality = 12;             // 10-63, lower is higher quality
    config.fb_count     = 2;
    config.grab_mode    = CAMERA_GRAB_LATEST;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    Serial.println("[Camera] PSRAM detected! Dual frame buffers allocated in PSRAM.");
  } else {
    config.frame_size   = FRAMESIZE_QVGA; // 320x240 for internal DRAM safety
    config.jpeg_quality = 15;
    config.fb_count     = 1;
    config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location  = CAMERA_FB_IN_DRAM;
    Serial.println("[Camera] WARNING: PSRAM not detected/enabled!");
    Serial.println("[Camera] Tip: Enable PSRAM in Arduino IDE: Tools -> PSRAM -> Enabled");
    Serial.println("[Camera] Operating in DRAM fallback mode (QVGA 320x240).");
  }

  // Camera initialization with clock fallback
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[Camera] Init failed at 20MHz (0x%x). Retrying at 10MHz...\n", err);
    config.xclk_freq_hz = 10000000;
    err = esp_camera_init(&config);
  }

  if (err != ESP_OK) {
    Serial.printf("[Camera] FATAL: Camera initialization failed with error 0x%x\n", err);
    Serial.println("[Camera] Check camera ribbon cable connection and 5V power supply.");
    cameraInitialized = false;
    return;
  }

  // Optimize sensor orientation & clarity
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, 1);       // Flip vertical (matches standard upright mount)
    s->set_hmirror(s, 0);     // Normal horizontal
    s->set_brightness(s, 1);  // Slight clarity boost (+1)
    s->set_saturation(s, 0);  // Balanced color saturation
    s->set_contrast(s, 0);
  }

  cameraInitialized = true;
  Serial.println("[Camera] OV2640 camera sensor initialized successfully!");
}

// =====================================================================================
// 4. IMAGE CAPTURE & ZERO-RAM STREAMING UPLOADER
// =====================================================================================
void uploadPhoto() {
  if (!cameraInitialized) {
    Serial.println("[Camera] Cannot upload: Camera sensor is not initialized!");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Camera] WiFi disconnected. Attempting reconnect...");
    WiFi.reconnect();
    return;
  }

  Serial.println("\n[Camera] === CAPTURING SNAPSHOT ===");
  digitalWrite(STATUS_LED_PIN, LOW); // Status LED ON (active low)

  // In single-buffer DRAM mode, drain any prior frame and wait for sensor sync
  if (!psramFound()) {
    camera_fb_t *stale_fb = esp_camera_fb_get();
    if (stale_fb) {
      esp_camera_fb_return(stale_fb);
      delay(80); // Sensor scan interval
    }
  }

  // Capture current frame with retry loop
  camera_fb_t *fb = NULL;
  for (int attempt = 1; attempt <= 3; attempt++) {
    fb = esp_camera_fb_get();
    if (fb && fb->buf && fb->len > 100) {
      break;
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
    }
    Serial.printf("[Camera] Frame capture attempt %d timed out, retrying in 100ms...\n", attempt);
    delay(100);
  }

  if (!fb) {
    Serial.println("[Camera] ERROR: Frame capture failed after 3 attempts!");
    digitalWrite(STATUS_LED_PIN, HIGH); // Status LED OFF
    return;
  }

  Serial.printf("[Camera] Snapshot captured: %u bytes (%dx%d, format: %d)\n",
                fb->len, fb->width, fb->height, fb->format);

  // Parse upload URL
  ServerEndpoint server = parseServerUrl(SERVER_UPLOAD_URL);
  Serial.printf("[HTTP] Connecting to %s:%d (%s)...\n",
                server.host.c_str(), server.port, server.isHttps ? "HTTPS" : "HTTP");

  WiFiClient *client = nullptr;
  WiFiClientSecure *secureClient = nullptr;

  if (server.isHttps) {
    secureClient = new WiFiClientSecure;
    if (!secureClient) {
      Serial.println("[HTTP] ERROR: Failed to allocate WiFiClientSecure!");
      esp_camera_fb_return(fb);
      digitalWrite(STATUS_LED_PIN, HIGH);
      return;
    }
    secureClient->setInsecure();
    secureClient->setTimeout(15000); // 15 second timeout for cloud proxy
    client = secureClient;
  } else {
    client = new WiFiClient;
    if (!client) {
      Serial.println("[HTTP] ERROR: Failed to allocate WiFiClient!");
      esp_camera_fb_return(fb);
      digitalWrite(STATUS_LED_PIN, HIGH);
      return;
    }
    client->setTimeout(10000);
  }

  // Connect to target host
  if (!client->connect(server.host.c_str(), server.port)) {
    Serial.printf("[HTTP] ERROR: Connection to %s:%d failed!\n", server.host.c_str(), server.port);
    esp_camera_fb_return(fb);
    delete client;
    digitalWrite(STATUS_LED_PIN, HIGH);
    return;
  }

  Serial.println("[HTTP] Connected! Streaming multipart frame directly...");

  String boundary = "----WebKitFormBoundaryAstra26ESP32";
  String head = "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"deviceId\"\r\n\r\n";
  head += String(CAM_DEVICE_ID) + "\r\n";
  head += "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"file\"; filename=\"snapshot.jpg\"\r\n";
  head += "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";
  uint32_t totalLen = head.length() + fb->len + tail.length();

  // 1. Send HTTP POST headers
  client->print(String("POST ") + server.path + " HTTP/1.1\r\n");
  client->print(String("Host: ") + server.host + "\r\n");
  client->print(String("Content-Type: multipart/form-data; boundary=") + boundary + "\r\n");
  client->print(String("Content-Length: ") + String(totalLen) + "\r\n");
  client->print("User-Agent: ESP32-CAM-Astra26\r\n");
  client->print("Connection: close\r\n\r\n");

  // 2. Send multipart header
  client->print(head);

  // 3. Stream raw JPEG buffer directly over socket in 1024-byte chunks
  // (Zero memory allocation! Eliminates DRAM exhaustion & duplicate header bugs!)
  const uint8_t *bufPtr = fb->buf;
  size_t remaining = fb->len;
  const size_t CHUNK_SIZE = 1024;

  while (remaining > 0) {
    size_t toWrite = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
    size_t written = client->write(bufPtr, toWrite);
    if (written == 0) {
      Serial.println("[HTTP] ERROR: Socket stream interrupted!");
      break;
    }
    bufPtr += written;
    remaining -= written;
  }

  // 4. Send multipart tail
  client->print(tail);
  client->flush();

  // 5. Release hardware camera frame immediately
  esp_camera_fb_return(fb);
  fb = NULL;

  Serial.println("[HTTP] Payload uploaded. Awaiting server response...");

  // 6. Read HTTP response code
  int httpCode = 0;
  unsigned long respStart = millis();
  while (client->connected() && (millis() - respStart < 15000)) {
    if (client->available()) {
      String line = client->readStringUntil('\n');
      if (line.startsWith("HTTP/1.1 ") || line.startsWith("HTTP/1.0 ")) {
        httpCode = line.substring(9, 12).toInt();
        break;
      }
    }
    delay(10);
  }

  if (httpCode == 200 || httpCode == 201) {
    Serial.printf("[HTTP] SUCCESS! Image received by server (Code: %d, Free Heap: %u bytes)\n",
                  httpCode, ESP.getFreeHeap());
  } else if (httpCode > 0) {
    Serial.printf("[HTTP] Server returned status code: %d (Free Heap: %u bytes)\n",
                  httpCode, ESP.getFreeHeap());
  } else {
    Serial.printf("[HTTP] Upload failed: No response / timeout from server (Free Heap: %u bytes)\n",
                  ESP.getFreeHeap());
  }

  client->stop();
  delete client;

  digitalWrite(STATUS_LED_PIN, HIGH); // Status LED OFF
}

// =====================================================================================
// 5. ARDUINO SETUP & LOOP
// =====================================================================================
void setup() {
  // Disable brownout detector to prevent reboot loops during high-current camera/WiFi initialization
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  delay(1000);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, HIGH); // Active LOW -> OFF

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);   // Active HIGH -> OFF

  Serial.println("\n==============================================");
  Serial.println("  PROJECT ASTRA26: ESP32-CAM SNAPSHOT ENGINE  ");
  Serial.println("==============================================");
  Serial.printf("[System] Free Heap: %u bytes | PSRAM: %s\n",
                ESP.getFreeHeap(), psramFound() ? "ENABLED" : "NOT DETECTED");

  // Initialize camera hardware
  setupCamera();

  // Connect to WiFi
  Serial.print("[WiFi] Connecting to: ");
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 40) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WiFi] Connected successfully!");
    Serial.print("[WiFi] IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WiFi] Signal Strength (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n[WiFi] Connection failed! Will retry in main loop.");
  }

  // Take first snapshot immediately upon startup
  if (WiFi.status() == WL_CONNECTED && cameraInitialized) {
    uploadPhoto();
    lastCaptureTime = millis();
  }
}

void loop() {
  unsigned long currentMillis = millis();

  // Periodically capture and upload
  if (currentMillis - lastCaptureTime >= CAPTURE_INTERVAL_MS) {
    lastCaptureTime = currentMillis;
    uploadPhoto();
  }

  // Handle reconnect if WiFi drops
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastReconnectAttempt = 0;
    if (currentMillis - lastReconnectAttempt > 10000) {
      lastReconnectAttempt = currentMillis;
      Serial.println("[WiFi] Lost connection. Reconnecting...");
      WiFi.reconnect();
    }
  }
}
