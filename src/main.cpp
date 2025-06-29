#include <ArduinoJson.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <queue>

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

// --- Data Queue ---
std::queue<String> dataQueue;
const int MAX_QUEUE_SIZE = 100; // Max queue size

// --- Sleep Control ---
bool shouldSleep = false;  // Flag to enter deep sleep if no alerts
bool wifiWasConnected = false;

// // Database-related topics (added for Node-RED database integration)
// const char* mqtt_topic_sensors = "esp32/sensors";       // Main sensor data topic
// const char* mqtt_topic_state = "esp32/state";          // System state topic
// const char* mqtt_topic_deepsleep = "esp32/deepsleep";   // Sleep notifications
// const char* mqtt_topic_button = "esp32/button";         // Button events

void setup_wifi() {
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);


  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected. IP address: ");
      Serial.println(WiFi.localIP());
      wifiWasConnected = true;
    } else {
      Serial.println("\nFailed to connect to WiFi");
    }
  }

// Function for adding data to queue
void addToQueue(const String& data) {
  if (dataQueue.size() >= MAX_QUEUE_SIZE) {
    // If queue is full, deleting the most old data
    dataQueue.pop();
  }
  dataQueue.push(data);
}

// Function to send all data from queue
void sendQueuedData() {
  while (!dataQueue.empty() && client.connected()) {
    String data = dataQueue.front();
    if (client.publish("esp32/sensors", data.c_str())) {
      dataQueue.pop();
      delay(50); // Small delay betweeen sendings
    } else {
      Serial.println("Failed to send queued data, will retry later");
      break;
    }
  }
}

void reconnect_mqtt() {
  if (!client.connected() && WiFi.status() == WL_CONNECTED) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32Client")) {
      Serial.println("connected to MQTT.");
      // After connecting to MQTT sending all data from queue 
      sendQueuedData();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" trying again in 5 seconds...");
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

      StaticJsonDocument<128> sleepDoc;
      sleepDoc["state"]["led"] = false;
      sleepDoc["state"]["reason"] = "Deep sleep activated";
      char sleepBuffer[128];
      serializeJson(sleepDoc, sleepBuffer);

      if (client.connected()) {
        client.publish("esp32/state", sleepBuffer);
        client.loop();
        delay(100);
        client.publish("esp32/deepsleep", "ENTERING_DEEP_SLEEP");
        client.loop();
      } else {
        addToQueue(sleepBuffer); // Saving into queue in case there is not connection
      }

      Serial.println("Button pressed - preparing to sleep...");

      if (client.connected()) {
        client.publish("esp32/button", "pressed");
      } else {
        addToQueue("{\"button\":\"pressed\"}");
      }

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
    if (client.connected()) {
      client.publish("esp32/deepsleep", "AWAKE");
    }
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

  digitalWrite(ledPin, ledState ? HIGH : LOW);

  String alertReason = "All normal";
  if (ledState) {
    alertReason = "";
    if (motionDetected) alertReason += "Unsafe Motion detected";
    if (tempHumCondition) {
      if (alertReason.length() > 0) alertReason += " + ";
      alertReason += "Climate alert";
    }
  }

  StaticJsonDocument<256> doc;
  JsonObject state = doc.createNestedObject("state");
  state["led"] = ledState;
  state["motion"] = motionDetected;
  state["climateAlert"] = tempHumCondition;
  state["reason"] = alertReason;

  char buffer[256];
  serializeJson(doc, buffer);

  if (WiFi.status() == WL_CONNECTED && client.connected()) {
    if (!client.publish("esp32/state", buffer)) {
      Serial.println("MQTT publish failed, adding to queue");
      addToQueue(buffer);
    } else {
      Serial.println("Published state:");
      Serial.print("LED: "); Serial.println(ledState ? "ON" : "OFF");
      Serial.print("Alert: "); Serial.println(tempHumCondition ? "YES" : "NO");
      Serial.print("Reason: "); Serial.println(alertReason);
    }
  } else {
    addToQueue(buffer);
    Serial.println("WiFi/MQTT not connected, data added to queue");
  }

  // Prepare for sleep if everything is OK
  shouldSleep = !ledState;
}

void send_mqtt_data() {
  StaticJsonDocument<256> doc;
  doc["timestamp"] = millis();
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


  if (WiFi.status() == WL_CONNECTED && client.connected()) {
    if (!client.publish("esp32/sensors", buffer)) {
      Serial.println("MQTT publish failed, adding to queue");
      addToQueue(buffer);
    }
  } else {
    addToQueue(buffer);
    Serial.println("WiFi/MQTT not connected, data added to queue");
  }
}

void handle_client() {
  if (WiFi.status() != WL_CONNECTED) return;

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
    clientWeb.print("<h3>Queue size: " + String(dataQueue.size()) + "</h3>");
    clientWeb.print("</body></html>");
  }

  clientWeb.stop();
}

void setup() {
  Serial.begin(115200);
  Wire.begin();
  dht.begin();

  Wire.beginTransmission(MPU_addr);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  pinMode(buttonPin, INPUT_PULLUP);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  server.begin();

  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    if (client.connected()) {
      client.publish("esp32/deepsleep", "AWAKE (WOKE UP)");
    } else {
      addToQueue("{\"deepsleep\":\"AWAKE (WOKE UP)\"}");
    }
  } else {
    if (client.connected()) {
      client.publish("esp32/deepsleep", "AWAKE (INITIAL)");
    } else {
      addToQueue("{\"deepsleep\":\"AWAKE (INITIAL)\"}");
    }
  }
  client.loop();
  delay(100);
}

void loop() {
  // Periodically trying to connect to WiFi in case there is no connection 
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasConnected || millis() > 30000) {
      // If there was a connection earlier or more than 30 seconds have passed since the start of work
      setup_wifi();
    }
  } else {
    if (!client.connected()) {
      reconnect_mqtt();
    }
    client.loop();
  }

  mpu_read();
  read_dht();
  handle_button();
  check_conditions();
  handle_client();


  if (millis() - lastMQTTSend > 2000) {
    send_mqtt_data();
    lastMQTTSend = millis();
  }

  // Deep Sleep if no alert
  if (shouldSleep) {
    Serial.println("System normal. Preparing to enter deep sleep...");

    if (client.connected()) {
      client.publish("esp32/deepsleep", "NO ALERTS – Sleeping...");
      client.loop();
    } else {
      addToQueue("{\"deepsleep\":\"NO ALERTS – Sleeping...\"}");
    }
    delay(100);

    for (int i = 0; i < 2; i++) {
      digitalWrite(ledPin, HIGH);
      delay(200);
      digitalWrite(ledPin, LOW);
      delay(200);
    }

    esp_sleep_enable_ext0_wakeup((gpio_num_t)buttonPin, 0);
    esp_deep_sleep_start();
  }

  delay(100);
}
