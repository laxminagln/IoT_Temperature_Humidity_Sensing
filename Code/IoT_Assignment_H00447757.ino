#include <WiFi.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

// Configuration
#define MQTT_HOST "broker.mqtt.cool"
#define MQTT_PORT 1883
#define MQTT_DEVICEID "d:hwu:esp32:H00447757"
#define MQTT_USER ""
#define MQTT_TOKEN ""
#define MQTT_TOPIC "d:hwu:esp32:H00447757/evt/status/fmt/json"
#define MQTT_TOPIC_DISPLAY "d:hwu:esp32:H00447757/cmd/display/fmt/json"
#define MQTT_TOPIC_INTERVAL "d:hwu:esp32:H00447757/cmd/interval"
#define MQTT_TOPIC_LED_CONTROL "d:hwu:esp32:H00447757/cmd/led_control"

// GPIO Pins
#define RED_PIN 4
#define GREEN_PIN 0
#define BLUE_PIN 2
#define DHT_PIN 17
#define DHTTYPE DHT11

// Temperature thresholds
#define ALARM_COLD 0.0
#define ALARM_HOT 30.0
#define WARN_COLD 10.0
#define WARN_HOT 25.0

// Default interval in milliseconds
unsigned long PUBLISH_INTERVAL = 10000;

char ssid[] = "11i HC";
char pass[] = "123454321";

DHT dht(DHT_PIN, DHTTYPE);
WiFiClient wifiClient;
void callback(char* topic, byte* payload, unsigned int length);
PubSubClient mqtt(MQTT_HOST, MQTT_PORT, callback, wifiClient);
StaticJsonDocument<100> jsonDoc;
JsonObject payload = jsonDoc.to<JsonObject>();
JsonObject status = payload.createNestedObject("d");
static char msg[50];

// Variables for temperature and LED state
float h = 0.0;
float t = 0.0;
unsigned long lastPublishTime = 0;
bool manualLEDControl = false;  // Flag for manual LED control

void setLED(int red, int green, int blue) {
  analogWrite(RED_PIN, red);
  analogWrite(GREEN_PIN, green);
  analogWrite(BLUE_PIN, blue);
}

void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = 0; // Null-terminate the payload
  String message = String((char*)payload);

  // Print the topic and message received for debugging
  Serial.print("Message arrived on topic: ");
  Serial.println(topic);
  Serial.print("Message payload: ");
  Serial.println(message);

  if (String(topic) == MQTT_TOPIC_INTERVAL) {
    // Handle interval topic for setting publish interval
    StaticJsonDocument<100> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      if (doc.containsKey("Interval")) {
        int newInterval = doc["Interval"];
        if (newInterval > 0) {
          PUBLISH_INTERVAL = newInterval * 1000;
          Serial.print("New interval set to: ");
          Serial.println(PUBLISH_INTERVAL);
        } else {
          Serial.println("Invalid interval received, must be > 0.");
        }
      } else {
        Serial.println("No 'interval' key found in JSON payload.");
      }
    } else {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
    }
  } else if (String(topic) == MQTT_TOPIC_LED_CONTROL) {
    // Handle LED control topic
    StaticJsonDocument<100> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      if (doc.containsKey("LED")) {
        JsonObject led = doc["LED"];
        const char* state = led["state"];
        
        if (strcmp(state, "on") == 0) {
          int red = led["red"];
          int green = led["green"];
          int blue = led["blue"];
          setLED(red, green, blue); // Set the LED to specified color
          manualLEDControl = true;  // Enable manual control
          Serial.println("LED turned on with specified color (manual mode).");
        } else if (strcmp(state, "off") == 0) {
          setLED(0, 0, 0); // Turn off the LED
          manualLEDControl = true;  // Enable manual control
          Serial.println("LED turned off (manual mode).");
        }
      } else {
        Serial.println("No 'LED' key found in JSON payload.");
      }
    } else {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.println("Received message on an unexpected topic.");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(2000);
  while (!Serial) { }
  Serial.println("ESP8266 Sensor Application");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi Connected");

  dht.begin();
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  if (mqtt.connect(MQTT_DEVICEID, MQTT_USER, MQTT_TOKEN)) {
    Serial.println("MQTT Connected");
    mqtt.subscribe(MQTT_TOPIC_DISPLAY);
    mqtt.subscribe(MQTT_TOPIC_INTERVAL);
    mqtt.subscribe(MQTT_TOPIC_LED_CONTROL);
  } else {
    Serial.println("MQTT Failed to connect!");
    ESP.restart();
  }
}

void loop() {
  mqtt.loop();
  while (!mqtt.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (mqtt.connect(MQTT_DEVICEID, MQTT_USER, MQTT_TOKEN)) {
      Serial.println("MQTT Connected");
      mqtt.subscribe(MQTT_TOPIC_DISPLAY);
      mqtt.subscribe(MQTT_TOPIC_INTERVAL);
      mqtt.subscribe(MQTT_TOPIC_LED_CONTROL);
      mqtt.loop();
    } else {
      Serial.println("MQTT Failed to connect!");
      delay(5000);
    }
  }

  if (millis() - lastPublishTime >= PUBLISH_INTERVAL) {
    lastPublishTime = millis();

    h = dht.readHumidity();
    t = dht.readTemperature();

    if (isnan(h) || isnan(t)) {
      Serial.println("Failed to read from DHT sensor!");
    } else {
      // If manual control is off, apply temperature-based LED logic
      if (!manualLEDControl) {
        if (t < ALARM_COLD) {
          setLED(0, 0, 254);  // Blue for cold alarm
        } else if (t < WARN_COLD) {
          setLED(0, 0, 150);  // Blue for cold warning
        } else if (t > ALARM_HOT) {
          setLED(254, 0, 0);  // Red for hot alarm
        } else if (t > WARN_HOT) {
          setLED(150, 0, 0);  // Red for hot warning
        } else {
          setLED(0, 254, 0);  // Green for normal
        }
      }

      // Publish sensor data to MQTT topic
      status["temp"] = t;
      status["humidity"] = h;
      serializeJson(jsonDoc, msg, 50);
      Serial.println(msg);
      Serial.println("Reporting Interval :",PUBLISH_INTERVAL)
      if (!mqtt.publish(MQTT_TOPIC, msg)) {
        Serial.println("MQTT Publish failed");
      }
    }
  }
}
