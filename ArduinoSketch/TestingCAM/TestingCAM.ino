/*
  FallDetect_ESP32CAM_withSD.ino
  - ESP32-CAM (AI-Thinker / OV2640) camera + WebServer
  - Saves every captured photo to microSD (SD_MMC)
  - MPU6050 (I2C SDA=GPIO15, SCL=GPIO14)
  - MAX30102 (I2C shared)
  - PIR -> GPIO13
  - Buzzer -> GPIO2
  - LED -> GPIO4
  - If SERVER_UPLOAD_URL set, it will POST the JPEG after saving to SD
*/

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include "MPU6050_light.h"
#include <MAX30105.h>
#include "heartRate.h"
#include <WebServer.h>
#include "FS.h"
#include "SD_MMC.h"

// ----------------- USER CONFIG -----------------
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASS";

// Leave empty if you do not want to POST externally
const char* SERVER_UPLOAD_URL = ""; // e.g. "https://your-server.example/upload"

// Pins (match your wiring)
#define I2C_SDA 15   // SDA -> GPIO15
#define I2C_SCL 14   // SCL -> GPIO14
#define PIR_PIN 13
#define BUZZER_PIN 2
#define LED_PIN 4

// Fall detection tuning
const float FALL_ACCEL_THRESHOLD = 2.2f;   // g (tune experimentally)
const unsigned long FALL_TIME_WINDOW_MS = 800;

// Heart monitor thresholds (bpm)
const int HR_LOW = 40;
const int HR_HIGH = 140;

// Camera config (AI-Thinker module)
camera_config_t camera_config = {
  .pin_pwdn       = 32,
  .pin_reset      = -1,
  .pin_xclk       = 0,
  .pin_sccb_sda   = 26,
  .pin_sccb_scl   = 27,
  .pin_d7         = 35,
  .pin_d6         = 34,
  .pin_d5         = 39,
  .pin_d4         = 36,
  .pin_d3         = 21,
  .pin_d2         = 19,
  .pin_d1         = 18,
  .pin_d0         = 5,
  .pin_vsync      = 25,
  .pin_href       = 23,
  .pin_pclk       = 22,
  .xclk_freq_hz   = 20000000,
  .ledc_timer     = LEDC_TIMER_0,
  .ledc_channel   = LEDC_CHANNEL_0,
  .pixel_format   = PIXFORMAT_JPEG,
  .frame_size     = FRAMESIZE_SVGA,
  .jpeg_quality   = 12,
  .fb_count       = 1
};

// ----------------- Globals -----------------
MPU6050 mpu(Wire);
MAX30105 particleSensor;
WebServer server(80);

unsigned long lastSpikeTime = 0;
bool spikeSeen = false;

unsigned long lastVitalCheck = 0;
float currentBPM = 0.0;
unsigned long lastBeatTime = 0;

// ----------------- SD Functions -----------------

bool initSD() {
  // Try to mount SD card via SD_MMC (built-in slot on many ESP32-CAM boards)
  if (!SD_MMC.begin()) {
    Serial.println("SD_MMC.begin() failed. Check if card is inserted and formatted (FAT32).");
    return false;
  }
  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("SD Card mounted. Size: %llu MB\n", cardSize);
  return true;
}

// Save camera framebuffer to SD card, return true if successful and set outPath
bool savePhotoToSD(camera_fb_t* fb, String &outPath) {
  if (!fb || fb->len == 0) {
    Serial.println("savePhotoToSD: invalid framebuffer");
    return false;
  }
  // Make filename with timestamp (millis since boot). If you want real time, integrate RTC or get time from NTP.
  String filename = "/img_" + String(millis()) + ".jpg";
  fs::FS &fs = SD_MMC;

  File file = fs.open(filename.c_str(), FILE_WRITE);
  if (!file) {
    Serial.printf("Failed to open file %s for writing\n", filename.c_str());
    return false;
  }

  size_t written = file.write(fb->buf, fb->len);
  file.close();
  if (written != fb->len) {
    Serial.printf("Warning: wrote %u of %u bytes to SD\n", written, fb->len);
    return false;
  }

  outPath = filename;
  Serial.printf("Saved file to SD: %s (size: %u bytes)\n", filename.c_str(), fb->len);
  return true;
}

// ----------------- Helper functions -----------------
void buzzOnce(int times, int onMs = 150, int offMs = 100) {
  for (int i=0; i<times; ++i) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(LED_PIN, HIGH);
    delay(onMs);
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_PIN, LOW);
    if(i < times - 1) delay(offMs);
  }
}

bool initCamera(){
  esp_err_t err = esp_camera_init(&camera_config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }
  return true;
}

// If SERVER_UPLOAD_URL is empty, skip POST step.
bool postJPEGtoServer(const uint8_t* buf, size_t len, const char* reason) {
  if (strlen(SERVER_UPLOAD_URL) == 0) {
    Serial.println("SERVER_UPLOAD_URL not set — skipping POST");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected — cannot POST");
    return false;
  }
  HTTPClient http;
  http.begin(SERVER_UPLOAD_URL);
  http.addHeader("Content-Type", "image/jpeg");
  if (reason) http.addHeader("X-Event-Reason", reason);

  // HTTPClient::sendRequest expects uint8_t* (non-const) so cast here
  int httpCode = http.sendRequest("POST", (uint8_t*)buf, len);

  Serial.printf("POST returned: %d\n", httpCode);
  http.end();
  return (httpCode >= 200 && httpCode < 300);
}

// Capture, save to SD, then post( optional )
void captureSaveAndOptionalPost(const char* reason) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed!");
    return;
  }

  // 1) Save to SD if available
  String savedPath = "";
  if (SD_MMC.cardType() != CARD_NONE) {
    if (savePhotoToSD(fb, savedPath)) {
      Serial.printf("Photo saved to SD: %s\n", savedPath.c_str());
    } else {
      Serial.println("Failed to save photo to SD");
    }
  } else {
    Serial.println("No SD card mounted; skipping save");
  }

  // 2) Optionally POST to server
  if (strlen(SERVER_UPLOAD_URL) > 0) {
    bool ok = postJPEGtoServer(fb->buf, fb->len, reason);
    if (ok) Serial.println("Image posted successfully");
    else Serial.println("Image post failed");
  }

  esp_camera_fb_return(fb);
}

// ----------------- HTTP handlers -----------------

// Root: simple HTML with links
void handleRoot() {
  String html = "<html><head><title>ESP32-CAM</title></head><body>";
  html += "<h2>ESP32-CAM (OV2640)</h2>";
  html += "<p><a href=\"/capture\">Capture single photo</a></p>";
  html += "<p><a href=\"/stream\">Live stream (MJPEG)</a></p>";
  html += "<p>SD status: ";
  if (SD_MMC.cardType() == CARD_NONE) html += "<b>Not mounted</b>";
  else {
    html += "<b>Mounted</b>";
    // Optionally show list of images
  }
  html += "</p></body></html>";
  server.send(200, "text/html", html);
}

// Capture single JPEG and send as image/jpeg (also saves to SD)
void handleCapture() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }

  // Try save to SD as well
  String savedPath = "";
  if (SD_MMC.cardType() != CARD_NONE) {
    savePhotoToSD(fb, savedPath);
  }

  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.send_P(200, "image/jpeg", (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// MJPEG streaming handler (multipart/x-mixed-replace)
void handleStream() {
  WiFiClient client = server.client();
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=esp32cam\r\n\r\n";
  server.sendContent(response);

  while (client.connected()) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Stream: Camera capture failed");
      server.sendContent("Camera capture failed");
      break;
    }
    String head = "--esp32cam\r\nContent-Type: image/jpeg\r\nContent-Length: " + String(fb->len) + "\r\n\r\n";
    server.sendContent(head);
    client.write(fb->buf, fb->len);
    server.sendContent("\r\n");
    esp_camera_fb_return(fb);

    // small delay between frames
    delay(100);
  }
}

// NotFound handler
void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ----------------- Setup & Loop -----------------
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);

  // Initialize I2C on chosen pins
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(50);

  // MPU6050 init
  Serial.println("Init MPU6050...");
  mpu.begin();
  delay(100);
  mpu.calcGyroOffsets();   // library-friendly no-arg

  // MAX30102 init
  Serial.println("Init MAX30102...");
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found — check wiring!");
  } else {
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x0A);
    particleSensor.setPulseAmplitudeGreen(0);
  }

  // Initialize SD card
  Serial.println("Initializing SD card (SD_MMC)...");
  if (initSD()) {
    Serial.println("SD mounted successfully.");
  } else {
    Serial.println("SD mount failed - continuing without SD.");
  }

  // Camera
  Serial.println("Init camera...");
  if (!initCamera()) {
    Serial.println("Camera init failed — continue without camera");
  } else {
    Serial.println("Camera ready");
  }

  // WiFi connect
  Serial.printf("Connecting to WiFi SSID: %s\n", ssid);
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("WiFi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connection failed (continue, will retry in background).");
  }

  // Start web server endpoints
  server.on("/", handleRoot);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/stream", HTTP_GET, handleStream);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started. Visit http://" + WiFi.localIP().toString() + "/ in your browser.");
}

unsigned long lastHRSampleMillis = 0;
const unsigned long HR_SAMPLE_INTERVAL = 25; // ms between reading samples from sensor
unsigned long lastLoopMillis = 0;

// Simple beat detection using SparkFun helper
void processMAX30102() {
  if (particleSensor.available()) {
    long irValue = particleSensor.getIR();
    if (irValue > 5000) { // threshold to ignore noise; tune as needed
      if (checkForBeat(irValue)) {
        unsigned long now = millis();
        if (lastBeatTime > 0) {
          unsigned long beatInterval = now - lastBeatTime;
          if (beatInterval > 200 && beatInterval < 2000) { // 30-300 bpm
            currentBPM = 60000.0 / (float)beatInterval;
            Serial.printf("Beat! BPM: %.1f\n", currentBPM);
          }
        }
        lastBeatTime = now;
      }
    }
    particleSensor.nextSample();
  }
}

void loop() {
  unsigned long now = millis();

  // Handle webserver client requests
  server.handleClient();

  // 1) MPU6050: update and compute accel magnitude in g
  mpu.update();
  float ax = mpu.getAccX();
  float ay = mpu.getAccY();
  float az = mpu.getAccZ();
  float mag = sqrt(ax*ax + ay*ay + az*az);

  // detect spike
  if (mag > FALL_ACCEL_THRESHOLD) {
    spikeSeen = true;
    lastSpikeTime = now;
    Serial.printf("Accel spike detected: %.2f g\n", mag);
  }

  // 2) PIR state
  bool pir = digitalRead(PIR_PIN) == HIGH;

  // If spike seen within window and PIR is HIGH, trigger fall event
  if (spikeSeen && (now - lastSpikeTime <= FALL_TIME_WINDOW_MS)) {
    if (pir) {
      Serial.println("Fall confirmed (spike + PIR). Triggering actions.");
      buzzOnce(3, 150, 120);
      // capture, save to SD, and optionally POST
      captureSaveAndOptionalPost("fall_detected");
      spikeSeen = false;
      delay(1200);
    }
  } else if (spikeSeen && (now - lastSpikeTime > FALL_TIME_WINDOW_MS)) {
    spikeSeen = false;
  }

  // 3) Heart rate processing (continuous)
  if (now - lastHRSampleMillis >= HR_SAMPLE_INTERVAL) {
    lastHRSampleMillis = now;
    processMAX30102();
  }

  // 4) Periodic vitals check (every 20s)
  if (now - lastVitalCheck > 20000) {
    lastVitalCheck = now;
    if (currentBPM > 0) {
      if (currentBPM < HR_LOW || currentBPM > HR_HIGH) {
        Serial.printf("HR out of range: %.1f bpm — local alert\n", currentBPM);
        buzzOnce(1, 200, 100);
        captureSaveAndOptionalPost("heart_rate_alert");
      } else {
        Serial.printf("HR OK: %.1f bpm\n", currentBPM);
      }
    } else {
      Serial.println("HR not stable yet");
    }
  }

  // tiny idle
  delay(10);
}
