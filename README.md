# ESP32 Motorcycle Motion Monitoring System

*A smart monitoring system with motion, climate sensing and web interface*

## 📋 Project Overview
This ESP32-based system monitors:
- Motion/orientation (MPU6050 accelerometer/gyroscope)
- Temperature and humidity (DHT11)
- Provides visual feedback via LED
- Offers remote monitoring through web interface
- Features low-power sleep mode
- Provides LED signals for power-on, deep sleep entry, and wake-up

## 🛠 Hardware Setup
| Component       | ESP32 Pin | Connection Notes          |
|-----------------|----------|--------------------------|
| MPU6050 (VCC)   | 3.3V     | Power supply             |
| MPU6050 (GND)   | GND      | Ground                   |
| MPU6050 (SDA)   | GPIO 21  | I2C data line            |
| MPU6050 (SCL)   | GPIO 22  | I2C clock line           |
| DHT11 (VCC)     | 5V       | Power supply             |
| DHT11 (DATA)    | GPIO 23  | Data pin with pull-up    |
| DHT11 (GND)     | GND      | Ground                   |
| LED (Anode)     | GPIO 5   | Through current-limiting resistor |
| Button          | GPIO 2   | Other side to GND        |

## 📝 Code Functionality
### Key Features
- **Sensor Monitoring**:
  - Continuous MPU6050 motion/orientation tracking
  - Periodic DHT11 climate measurements
- **Smart LED Control**:
  - LED turns on when motion is detected (configurable threshold)
  - LED turns on when humidity > 80% OR temperature < 10°C or > 25°C
  - LED briefly turns on at power-up (indicates ignition/power-on)
  - LED flashes during deep sleep entry and wake-up to indicate state transitions
- **Power Management**:
  - Deep sleep mode activated by button press
  - Wake-up from sleep via same button
- **Web Interface**:
  - Built-in HTTP server (port 80)
  - JSON API endpoint (`/data`)
  - Auto-refreshing web dashboard

### 🔄 Workflow
1. Initializes all sensors and WiFi connection
2. On startup:
- LED briefly turns on to indicate ignition
3. In main loop:
- Reads sensor data
- Checks button state
- Controls LED based on motion and environmental conditions
- Handles incoming web client requests
4. On button press:
- LED flashes, system enters deep sleep
5. On wake-up:
- LED flashes, system resumes normal operation

## 🚀 Getting Started
### Prerequisites
- ESP32 development board
- Arduino IDE or PlatformIO
- Required libraries:
  - `Wire.h` (I2C)
  - `WiFi.h`
  - `DHT.h`
  - `Arduino.h`

### Installation
1. Clone this repository
2. Open in Arduino IDE/PlatformIO
3. Update WiFi credentials in code:
   ```cpp
   const char *ssid = "YOUR_SSID";
   const char *pass = "YOUR_PASSWORD";
