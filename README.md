# 🚨 Fall Detection System with ESP32-CAM + Vital Monitoring

This project is a **Wearable Fall Detection + Heart Rate Alert System** using an **ESP32-CAM (OV2640)** and sensors.  
The system detects sudden motion (fall), confirms presence using PIR, checks heart rate, and **triggers local alerts** (LED + Buzzer).  
A built-in **local web server** shows the camera feed using the ESP32 IP and allows photo capture.

> ✅ No cloud server required for basic testing  
> ✅ View live camera feed on browser  
> ✅ Designed as wearable prototype for elderly / medical monitoring

---

## 📌 Features

| Feature | Status |
|--------|--------|
| Fall detection (MPU6050 + PIR combo logic) | ✅ |
| Heart rate monitoring (MAX30102) | ✅ |
| Camera live stream / snapshot using ESP32 IP | ✅ |
| Local alert on fall (Buzzer + LED) | ✅ |
| No server needed (but optional server support exists) | ✅ |

---

## 🛠 Components Used

| Component | Qty | Purpose |
|----------|-----|---------|
| **ESP32-CAM (OV2640)** | 1 | Main controller + camera module |
| **FTDI Programmer (USB-to-TTL, 3.3V/5V switchable)** | 1 | Flash ESP32-CAM |
| **MPU6050 Gyroscope + Accelerometer (I2C)** | 1 | Detect sudden acceleration / fall |
| **MAX30102** Heart Rate / SpO2 sensor | 1 | Heart rate monitoring |
| **PIR Motion Sensor** | 1 | Confirms human presence on fall |
| **Buzzer** | 1 | Alert tone |
| **LED + 220Ω resistor** | 1 | Visual feedback when fall detected |
| **Jumper Wires (Male-Female / Female-Female)** | 1 set | Wiring |
| **Breadboard / PCB** | 1 | Prototype |
| **Power Source (Li-ion / LiPo battery / Power bank)** | 1 | Portable power |

---

## 🚦 Pin Connections (Wiring)

### ✅ ESP32-CAM — Power

| ESP32-CAM Pin | Connects To |
|--------------|-------------|
| **5V** | Power bank / battery +5V |
| **GND** | Ground |

> ⚠ Use **at least 2A** power if using power bank  
> ESP32-CAM consumes high current when camera starts

---

### ✅ ESP32-CAM — Programming Mode (FTDI)

| FTDI Programmer | ESP32-CAM |
|----------------|-----------|
| **5V** | 5V |
| **GND** | GND |
| **TXD** | U0R |
| **RXD** | U0T |

> ⚠ **Add a jumper between IO0 → GND** only while flashing.  
> Remove it after upload to run the program.

---

### ✅ MPU6050 Wiring (Gyro / Accelerometer)

| MPU6050 | ESP32-CAM |
|---------|-----------|
| **VCC (3.3V)** | 3.3V |
| **GND** | GND |
| **SDA** | GPIO **15** |
| **SCL** | GPIO **14** |

---

### ✅ MAX30102 (Heart Rate Sensor)

| MAX30102 | ESP32-CAM |
|----------|-----------|
| **VIN (3.3V)** | 3.3V |
| **GND** | GND |
| **SDA** | GPIO **15** *(shared I²C)* |
| **SCL** | GPIO **14** *(shared I²C)* |

> ⚠ MPU6050 + MAX30102 share I²C bus → connect them in parallel  
> ESP32-CAM GPIO used for I²C:
> - SDA → GPIO15  
> - SCL → GPIO14  

---

### ✅ PIR Sensor (Human confirmation)

| PIR Motion | ESP32-CAM |
|------------|-----------|
| VCC | 5V |
| GND | GND |
| OUT | GPIO **13** |

---

### ✅ Buzzer + LED (Alert outputs)

| Component | ESP32-CAM Pin |
|-----------|----------------|
| Buzzer (+) | GPIO **2** |
| Buzzer (–) | GND |
| LED (through 220Ω resistor) | GPIO **4** |
| LED (other pin) | GND |

---

## 📸 Live Camera Access (No server required)

Once code is uploaded and ESP32-CAM connects to Wi-Fi:

Open **Serial Monitor @ 115200 baud**, copy the printed IP

Open Browser

