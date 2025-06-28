#include <ArduinoJson.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

#define BUTTON_DEBOUNCE_DELAY 50

// --- MPU6050 ---
const int MPU_addr = 0x68;
int16_t AcX, AcY, AcZ, GyX, GyY, GyZ;

// --- DHT11 ---
#define DHTPIN 23
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
const char *pass = "Sacha3232";
WiFiServer server(80);

// --- MQTT ---
const char *mqtt_server = "192.168.0.127";
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMQTTSend = 0;

void setup_wifi() {
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected. IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect_mqtt() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client")) {
      Serial.println("connected to MQTT.");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" trying again in 5 seconds...");
      delay(5000);
    }
  }
}

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

      // Sending LED status as false before sleep
      StaticJsonDocument<128> sleepDoc;
      sleepDoc["state"]["led"] = false;
      sleepDoc["state"]["reason"] = "Deep sleep activated";
      char sleepBuffer[128];
      serializeJson(sleepDoc, sleepBuffer);
      client.publish("esp32/state", sleepBuffer);
      client.loop(); // To be checked that message is sent

      // Sending status before sleep
      client.publish("esp32/deepsleep", "ENTERING_DEEP_SLEEP");
      client.loop(); // Ensuring data sending
      delay(100); // Giving time for sending

      Serial.println("Button pressed - preparing to sleep...");
      client.publish("esp32/button", "pressed");

      for (int i = 0; i < 3; i++) {
        digitalWrite(ledPin, HIGH);
        delay(300);
        digitalWrite(ledPin, LOW);
        delay(300);
      }
      
      esp_sleep_enable_ext0_wakeup((gpio_num_t)buttonPin, 0);
      esp_deep_sleep_start();
    }
  } else {
    buttonPressed = false;
    lastDebounceTime = millis();
    client.publish("esp32/deepsleep", "AWAKE"); // Sending status "not in sleep"
  }
}

void check_conditions() {
  float accelX = AcX / 16384.0;
  float gyroZ = GyZ / 131.0;

  bool motionDetected = (fabs(accelX) > 0.3 || fabs(gyroZ) > 50.0);
  bool tempHumCondition = (temperature < 10.0) || (temperature > 25.0) || (humidity > 80.0);
  bool ledState = motionDetected || tempHumCondition;

  Serial.print("Motion: "); Serial.print(motionDetected);
  Serial.print(" | Climate: "); Serial.println(tempHumCondition);

  //digitalWrite(ledPin, (motionDetected || tempHumCondition) ? HIGH : LOW);
  digitalWrite(ledPin, ledState ? HIGH : LOW);

  // Creating alert reason
  String alertReason = "All normal";
  if (ledState) {
    alertReason = "";
    if (motionDetected) alertReason += "Unsafe Motion detected";
    if (tempHumCondition) {
      if (alertReason.length() > 0) alertReason += " + ";
      alertReason += "Climate alert";
    }
  }

  // Creating single message with all statuses
  StaticJsonDocument<256> doc;
  JsonObject state = doc.createNestedObject("state");
  state["led"] = ledState;
  state["motion"] = motionDetected;
  state["climateAlert"] = tempHumCondition;
  state["reason"] = alertReason;

  char buffer[256];
  serializeJson(doc, buffer);
  if (!client.publish("esp32/state", buffer)) {
    Serial.println("MQTT publish failed!");
  } else {
    Serial.println("Published state:");
    Serial.print("LED: "); Serial.println(ledState ? "ON" : "OFF");
    Serial.print("Alert: "); Serial.println(tempHumCondition ? "YES" : "NO");
    Serial.print("Reason: "); Serial.println(alertReason);
  }
}

void send_mqtt_data() {
  StaticJsonDocument<256> doc;
  doc["AcX"] = AcX / 16384.0;
  doc["AcY"] = AcY / 16384.0;
  doc["AcZ"] = AcZ / 16384.0;
  doc["GyX"] = GyX / 131.0;
  doc["GyY"] = GyY / 131.0;
  doc["GyZ"] = GyZ / 131.0;
  doc["Temp"] = temperature;
  doc["Hum"] = humidity;
  doc["Btn"] = buttonPressed;

  char buffer[256];
  serializeJson(doc, buffer);
  client.publish("esp32/sensors", buffer);
}

void handle_client() {
  WiFiClient clientWeb = server.available();
  if (!clientWeb) return;

  String request = "";
  while (clientWeb.available()) {
    char c = clientWeb.read();
    request += c;
  }

  if (request.indexOf("/data") >= 0) {
    clientWeb.print("HTTP/1.1 200 OK\r\n");
    clientWeb.print("Content-Type: application/json\r\n\r\n");
    clientWeb.print("{");
    clientWeb.print("\"AcX\":" + String(AcX / 16384.0) + ",");
    clientWeb.print("\"AcY\":" + String(AcY / 16384.0) + ",");
    clientWeb.print("\"AcZ\":" + String(AcZ / 16384.0) + ",");
    clientWeb.print("\"GyX\":" + String(GyX / 131.0) + ",");
    clientWeb.print("\"GyY\":" + String(GyY / 131.0) + ",");
    clientWeb.print("\"GyZ\":" + String(GyZ / 131.0) + ",");
    clientWeb.print("\"Temp\":" + String(temperature) + ",");
    clientWeb.print("\"Hum\":" + String(humidity) + ",");
    clientWeb.print("\"Btn\":" + String(buttonPressed ? "true" : "false"));
    clientWeb.print("}");
  } else {
    clientWeb.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n");
    clientWeb.print("<html><head><meta charset='UTF-8'><script>");
    clientWeb.print("function update() { fetch('/data').then(r => r.json()).then(d => {");
    clientWeb.print("document.getElementById('temp').innerText = d.Temp + ' °C';");
    clientWeb.print("document.getElementById('hum').innerText = d.Hum + ' %';");
    clientWeb.print("document.getElementById('btn').innerText = d.Btn ? 'Pressed' : 'Released';");
    clientWeb.print("}); } setInterval(update, 1000);");
    clientWeb.print("</script></head><body>");
    clientWeb.print("<h2>DHT11: Temperature = <span id='temp'>0</span>, Humidity = <span id='hum'>0</span></h2>");
    clientWeb.print("<h2>Button: <span id='btn'>Released</span></h2>");
    clientWeb.print("</body></html>");
  }

  clientWeb.stop();
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  dht.begin();

  // MPU6050 init
  Wire.beginTransmission(MPU_addr);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(buttonPin, INPUT_PULLUP);

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    client.publish("esp32/deepsleep", "AWAKE (WOKE UP)");
  } else {
    client.publish("esp32/deepsleep", "AWAKE (INITIAL)");
  }
  client.loop();
  delay(100);

  if (digitalRead(buttonPin) == LOW) {
    esp_sleep_enable_ext0_wakeup((gpio_num_t)buttonPin, 0);
    esp_deep_sleep_start();
  }

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  server.begin();
}

void loop() {
  if (!client.connected()) {
    reconnect_mqtt();
  }
  client.loop();

  mpu_read();
  read_dht();
  handle_button();
  check_conditions();
  handle_client();

  if (millis() - lastMQTTSend > 2000) {
    send_mqtt_data();
    lastMQTTSend = millis();
  }

  delay(100);
}

