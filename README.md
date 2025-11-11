# ESP32-CAM: Image Capture and Upload to Server (HTTP POST)

This project demonstrates how to use the **ESP32-CAM** module to capture a JPEG image using the OV2640 camera and upload it to a server using **HTTP POST (multipart/form-data)**.

The code includes:
- WiFi connection handling
- Camera initialization
- JPEG image capture
- Upload to server via HTTP POST
- Detailed debugging logs on serial monitor

---

## ✅ Features

- Connects ESP32-CAM to Wi-Fi
- Captures an image in JPEG format
- Sends image to backend API (`multipart/form-data`)
- Provides debug output on Serial Monitor
- Configurable resolution (e.g., SVGA, UXGA, QVGA)

---

## 📦 Components Used

| Component        | Quantity | Notes |
|------------------|----------|-------|
| ESP32-CAM module | 1        | OV2640 supported |
| FTDI Programmer  | 1        | Configure to **3.3V** mode |
| Jumper wires     | 4–6      | RX/TX + Power |

---

## 🔌 Wiring Diagram

| ESP32-CAM Pin | FTDI Programmer Pin |
|---------------|----------------------|
| 5V            | VCC (5V)             |
| GND           | GND                  |
| U0R (RX)      | TX                   |
| U0T (TX)      | RX                   |
| IO0 → GND     | Required only while flashing |

Once code is uploaded:
➡️ Remove IO0 → GND  
➡️ Press **RESET**

---

## ⚙️ Required Libraries / Setup

In **Arduino IDE**:

1. Install ESP32 board support  
   `Boards Manager → Search "ESP32" → Install from Espressif`
2. Select board:  
   `Tools → Board → ESP32 Arduino → AI Thinker ESP32-CAM`
3. Set upload configuration:
   - Flash Mode: `QIO`
   - Baud Rate: `115200`
   - Partition Scheme: `Huge APP`

---

## 🛠 Configuration in Code

Edit these in `.ino` file:

```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* SERVER_UPLOAD_URL = "http://YOUR_SERVER_IP/upload";
