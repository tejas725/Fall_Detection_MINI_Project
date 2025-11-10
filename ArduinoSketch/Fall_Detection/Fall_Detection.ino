/*
  FallDetect_ESP32CAM_updated.ino
  - Uses MPU6050 (I2C on SDA=GPIO15, SCL=GPIO14)
  - MAX30102 heart-sensor (I2C shared)
  - PIR -> GPIO13
  - Buzzer -> GPIO2
  - LED -> GPIO4
  - Camera: built-in (AI-Thinker / OV2640 board)
  - When fall detected: capture JPEG and POST to SERVER_UPLOAD_URL
  - Continuous BPM monitoring; when HR is out of range, optional alert
*/

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include "MPU6050_light.h"
#include <MAX30105.h>
#include "heartRate.h"

// ----------------- USER CONFIG -----------------
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASS";
// POST endpoint that accepts raw JPEG (Content-Type: image/jpeg)
const char* SERVER_UPLOAD_URL = "https://your-server.example/upload";

// Pins (match wiring you confirmed)
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
  .jpeg_quality   = 12, // 0-63 lower is better quality
  .fb_count       = 1
};

// ----------------- Globals -----------------
MPU6050 mpu(Wire);
MAX30105 particleSensor;

unsigned long lastSpikeTime = 0;
bool spikeSeen = false;

unsigned long lastVitalCheck = 0;
float currentBPM = 0.0;
unsigned long lastBeatTime = 0;

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

// Posts raw JPEG buffer to SERVER_UPLOAD_URL (Content-Type image/jpeg)
bool postJPEGtoServer(const uint8_t* buf, size_t len, const char* reason) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected — cannot POST");
    return false;
  }
  HTTPClient http;
  http.begin(SERVER_UPLOAD_URL);
  http.addHeader("Content-Type", "image/jpeg");
  if (reason) http.addHeader("X-Event-Reason", reason);

  // NOTE: HTTPClient::sendRequest expects uint8_t* (non-const) so cast here
  int httpCode = http.sendRequest("POST", (uint8_t*)buf, len);   // <-- CAST FIX

  Serial.printf("POST returned: %d\n", httpCode);
  http.end();
  return (httpCode >= 200 && httpCode < 300);
}

void captureAndSend(const char* reason) {
  Serial.println("Capturing image...");
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed!");
    return;
  }
  Serial.printf("Captured %u bytes\n", fb->len);
  bool ok = postJPEGtoServer(fb->buf, fb->len, reason);
  if (ok) Serial.println("Image posted successfully");
  else Serial.println("Image post failed");
  esp_camera_fb_return(fb);
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
  mpu.calcGyroOffsets();   // <-- LIBRARY-FRIENDLY CALL (no arg)

  // MAX30102 init
  Serial.println("Init MAX30102...");
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found — check wiring!");
  } else {
    particleSensor.setup(); // default setup
    particleSensor.setPulseAmplitudeRed(0x0A); // tune LED brightness if needed
    particleSensor.setPulseAmplitudeGreen(0);  // green not used for HR
  }

  // Camera
  Serial.println("Init camera...");
  if (!initCamera()) {
    Serial.println("Camera init failed — continue without camera");
  } else {
    Serial.println("Camera ready");
  }

  // WiFi connect
  Serial.printf("Connecting to WiFi %s\n", ssid);
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connection failed (continue, will retry later)");
  }
}

unsigned long lastLoopMillis = 0;
unsigned long lastHRSampleMillis = 0;
const unsigned long HR_SAMPLE_INTERVAL = 25; // ms between reading samples from sensor

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

  // 1) MPU6050: update and compute accel magnitude in g
  mpu.update();
  float ax = mpu.getAccX();
  float ay = mpu.getAccY();
  float az = mpu.getAccZ();
  float mag = sqrt(ax*ax + ay*ay + az*az); // in g (if library returns g)

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
      // sound and LED: 3 beeps (per your requirement)
      buzzOnce(3, 150, 120);
      // capture & send image
      captureAndSend("fall_detected");
      spikeSeen = false;
      // small cooldown to avoid repeated triggers
      delay(1200);
    }
  } else if (spikeSeen && (now - lastSpikeTime > FALL_TIME_WINDOW_MS)) {
    spikeSeen = false; // timeout, ignore
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
        Serial.printf("HR out of range: %.1f bpm — sending alert\n", currentBPM);
        buzzOnce(1, 200, 100);
        captureAndSend("heart_rate_alert");
      } else {
        Serial.printf("HR OK: %.1f bpm\n", currentBPM);
      }
    } else {
      Serial.println("HR not stable yet");
    }
  }

  // small non-blocking delay
  delay(10);
}
