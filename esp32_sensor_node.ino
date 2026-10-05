#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// WiFi credentials - Change these to match your network or set ESP32 as Access Point
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

WebServer server(80);

// --- JSN-SR04T (Ultrasonic Water Level Sensor) ---
#define TRIG_PIN 5
#define ECHO_PIN 18
// Assume the sensor is mounted at 42.0m MSL (above the dam crest)
const float SENSOR_ELEVATION_MSL = 42.0;

// --- YF-S201 (Hall Effect Water Flow Sensor) ---
#define FLOW_SENSOR_PIN 19
volatile int pulseCount = 0;
float flowRateLPM = 0.0;
unsigned long oldTime = 0;

// Interrupt Service Routine for Flow Sensor
void IRAM_ATTR pulseCounter() {
  pulseCount++;
}

void setup() {
  Serial.begin(115200);
  
  // Setup JSN-SR04T pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  // Setup YF-S201 pin and interrupt
  pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, FALLING);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP Address: ");
  Serial.println(WiFi.localIP());

  // Setup HTTP Endpoint for the Web Simulator
  server.on("/data", HTTP_GET, []() {
    // 1. Read Ultrasonic Distance
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
    float distance_m = (duration * 0.0343) / 2.0 / 100.0;
    
    // Calculate actual water stage (Elevation - distance to water)
    float stage_m = SENSOR_ELEVATION_MSL - distance_m;
    if (distance_m == 0 || stage_m < 0) {
      stage_m = 38.5; // Fallback default if misreading
    }

    // 2. Read Flow Rate
    if ((millis() - oldTime) > 1000) { // Update every 1 sec
      detachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN));
      
      // YF-S201 flow rate calibration: Pulse frequency (Hz) / 7.5 = flow rate in L/min.
      flowRateLPM = ((1000.0 / (millis() - oldTime)) * pulseCount) / 7.5;
      oldTime = millis();
      pulseCount = 0;
      
      attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), pulseCounter, FALLING);
    }

    // 3. Send JSON response
    StaticJsonDocument<200> doc;
    doc["stage_m"] = stage_m;
    doc["flow_lpm"] = flowRateLPM;
    
    String response;
    serializeJson(doc, response);
    
    // Set CORS headers so the local HTML file can fetch it
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", response);
  });

  server.begin();
}

void loop() {
  server.handleClient();
}
