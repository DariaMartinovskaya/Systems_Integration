#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <DHT.h>

#define BUTTON_DEBOUNCE_DELAY 50 

// --- MPU6050 ---
const int MPU_addr = 0x68;
int16_t AcX, AcY, AcZ, GyX, GyY, GyZ;

// --- DHT11 ---
#define DHTPIN 23      // DHT11 Data pin
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);
float temperature = 0.0;
float humidity = 0.0;

// --- Button ---
const int buttonPin = 4;
bool buttonPressed = false;
unsigned long lastDebounceTime = 0;

// --- LED ---
const int ledPin = 5;

// --- Wi-Fi ---
const char *ssid = "Alex";
const char *pass = "Darya3232";
WiFiServer server(80);

void mpu_read() {
  Wire.beginTransmission(MPU_addr);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_addr, (size_t)14, true);

  if (Wire.available() == 14) {
    AcX = Wire.read() << 8 | Wire.read();
    AcY = Wire.read() << 8 | Wire.read();
    AcZ = Wire.read() << 8 | Wire.read();
    GyX = Wire.read() << 8 | Wire.read();
    GyY = Wire.read() << 8 | Wire.read();
    GyZ = Wire.read() << 8 | Wire.read();
  }
}

void read_dht() {
  humidity = dht.readHumidity();
  temperature = dht.readTemperature();
  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Failed to read from DHT sensor!");
    humidity = 0;
    temperature = 0;
  }
}

void handle_button() {
  int reading = digitalRead(buttonPin);
  
  if (reading == LOW) {
    if ((millis() - lastDebounceTime) > BUTTON_DEBOUNCE_DELAY) {
      buttonPressed = true;
      Serial.println("Button pressed - preparing to sleep...");
      
      digitalWrite(ledPin, LOW);
      delay(1000);
      
      esp_sleep_enable_ext0_wakeup((gpio_num_t)buttonPin, 0);
      esp_deep_sleep_start();
    }
  } else {
    buttonPressed = false;
    lastDebounceTime = millis();
  }
}

void check_conditions() {
  float accelX = AcX / 16384.0;
  float gyroZ = GyZ / 131.0;

  bool motionDetected = (fabs(accelX) > 1.0 || fabs(gyroZ) > 100.0);
  bool tempHumCondition = (temperature < 10.0) || (temperature > 25.0) || (humidity > 80.0);

  digitalWrite(ledPin, (motionDetected || tempHumCondition) ? HIGH : LOW);

  if (tempHumCondition) {
    Serial.println("Climate condition triggered!");
    Serial.print("Temperature: "); Serial.print(temperature);
    Serial.print("°C, Humidity: "); Serial.print(humidity); Serial.println("%");
  }
}

void handle_client() {
  WiFiClient client = server.available();
  if (!client) return;

  String request = "";
  while (client.available()) {
    char c = client.read();
    request += c;
  }

  if (request.indexOf("/data") >= 0) {
    client.print("HTTP/1.1 200 OK\r\n");
    client.print("Content-Type: application/json\r\n\r\n");
    client.print("{");
    client.print("\"AcX\":" + String(AcX / 16384.0) + ",");
    client.print("\"AcY\":" + String(AcY / 16384.0) + ",");
    client.print("\"AcZ\":" + String(AcZ / 16384.0) + ",");
    client.print("\"GyX\":" + String(GyX / 131.0) + ",");
    client.print("\"GyY\":" + String(GyY / 131.0) + ",");
    client.print("\"GyZ\":" + String(GyZ / 131.0) + ",");
    client.print("\"Temp\":" + String(temperature) + ",");
    client.print("\"Hum\":" + String(humidity) + ",");
    client.print("\"Btn\":" + String(buttonPressed ? "true" : "false"));
    client.print("}");
  } else {
    client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n");
    client.print("<html><head><meta charset='UTF-8'><script>");
    client.print("function update() { fetch('/data').then(r => r.json()).then(d => {");
    client.print("document.getElementById('temp').innerText = d.Temp + ' °C';");
    client.print("document.getElementById('hum').innerText = d.Hum + ' %';");
    client.print("document.getElementById('btn').innerText = d.Btn ? 'Pressed' : 'Released';");
    client.print("}); } setInterval(update, 1000);");
    client.print("</script></head><body>");
    client.print("<h2>DHT11: Temperature = <span id='temp'>0</span>, Humidity = <span id='hum'>0</span></h2>");
    client.print("<h2>Button: <span id='btn'>Released</span></h2>");
    client.print("</body></html>");
  }

  client.stop();
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  dht.begin();
  
  // MPU6050 initialization
  Wire.beginTransmission(MPU_addr);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(buttonPin, INPUT_PULLUP);

  // Initial button check
  if (digitalRead(buttonPin) == LOW) {
    Serial.println("Button pressed at startup - entering deep sleep...");
    delay(100);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)buttonPin, 0);
    esp_deep_sleep_start();
  }

  // WiFi connection
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected.");
  Serial.println(WiFi.localIP());
  server.begin();
}

void loop() {
  mpu_read();
  read_dht();
  handle_button();  
  check_conditions();
  handle_client();
  
  delay(100);
}

