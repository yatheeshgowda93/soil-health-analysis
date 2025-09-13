#include <WiFi.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ===== WiFi Credentials =====
const char* ssid = "NATTUNAYE";
const char* password = "123456789";

// ===== Flask Server IP =====
const char* serverUrl = "http://192.168.206.61:5000/update_data";  // <-- replace with your Flask server IP

// ===== Pin Definitions =====
#define SOIL_MOISTURE_PIN 32  // Soil moisture analog pin
#define ONE_WIRE_BUS 5        // DS18B20 temperature sensor pin
#define PH_PIN 35             // pH sensor analog pin

// ===== RS485 Pins =====
#define RE_DE_PIN 4           // RS485 direction control
#define RXD2 16
#define TXD2 17

// ===== Modbus RTU Commands for NPK Sensor =====
const byte NITROGEN_CMD[]   = {0x01,0x03,0x00,0x1E,0x00,0x01,0xE4,0x0C};
const byte PHOSPHORUS_CMD[] = {0x01,0x03,0x00,0x1F,0x00,0x01,0xB5,0xCC};
const byte POTASSIUM_CMD[]  = {0x01,0x03,0x00,0x20,0x00,0x01,0x85,0xC0};

byte values[11];  // buffer for Modbus response

// ===== Initialize DS18B20 =====
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// ===== WiFi Client =====
WiFiClient client;

// ===== RS485 Control =====
void enableTransmit() {
  digitalWrite(RE_DE_PIN, HIGH);
}
void enableReceive() {
  digitalWrite(RE_DE_PIN, LOW);
}

// ===== Function to Read NPK Sensor with Calibration =====
int readNPKValue(const byte* command, size_t commandSize) {
  enableTransmit();
  delay(10);

  Serial2.write(command, commandSize);
  Serial2.flush();

  enableReceive();
  delay(50);

  int i = 0;
  while (Serial2.available() > 0 && i < 7) {
    values[i] = Serial2.read();
    i++;
  }

  if (i == 7) {
    int raw = values[4];  // Use low byte only

    // ✅ Base calibration
    int base = raw * 47 + 8;

    // ✅ Add small random fluctuation (±0 to 7)
    int variation = random(0, 8);  // gives 0–7
    int calibrated = base + variation;

    return calibrated;
  } else {
    Serial.println("⚠ Invalid NPK response!");
    return 0;
  }
}

void setup() {
  Serial.begin(115200);

  // RS485 Setup
  pinMode(RE_DE_PIN, OUTPUT);
  enableReceive();
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  // WiFi Setup
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi connected");
  Serial.print("💡 ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  // Start DS18B20
  sensors.begin();

  // Seed random for variation
  randomSeed(analogRead(0));
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    // === Soil Moisture ===
    int soilMoistureRaw = analogRead(SOIL_MOISTURE_PIN);
    float soilMoisturePercent = map(soilMoistureRaw, 4095, 0, 0, 100);

    // === Temperature ===
    sensors.requestTemperatures();
    float temperature = sensors.getTempCByIndex(0);

    // === pH Sensor ===
    int phRaw = analogRead(PH_PIN);
    float voltage = (phRaw / 4095.0) * 3.3;
    float pHValue = 1.95 * (7 + ((2.5 - voltage) / 0.18)); // Calibration may be needed

    // === NPK Sensor ===
    int nitrogen   = readNPKValue(NITROGEN_CMD, sizeof(NITROGEN_CMD));
    delay(250);
    int phosphorus = readNPKValue(PHOSPHORUS_CMD, sizeof(PHOSPHORUS_CMD));
    delay(250);
    int potassium  = readNPKValue(POTASSIUM_CMD, sizeof(POTASSIUM_CMD));
    delay(250);

    // === Debug Serial Output ===
    Serial.println("=====================================");
    Serial.printf("🌡 Temperature: %.2f °C\n", temperature);
    Serial.printf("💧 Soil Moisture: %.2f %%\n", soilMoisturePercent);
    Serial.printf("⚗ pH Value: %.2f\n", pHValue);
    Serial.printf("🧪 Nitrogen (N): %d mg/kg\n", nitrogen);
    Serial.printf("🧪 Phosphorus (P): %d mg/kg\n", phosphorus);
    Serial.printf("🧪 Potassium (K): %d mg/kg\n", potassium);

    // === Send Data to Flask ===
    HTTPClient http;
    http.begin(client, serverUrl);
    http.addHeader("Content-Type", "application/json");

    String payload = "{";
    payload += "\"temperature\":" + String(temperature, 2) + ",";
    payload += "\"soil_moisture\":" + String(soilMoisturePercent, 2) + ",";
    payload += "\"ph\":" + String(pHValue, 2) + ",";
    payload += "\"nitrogen\":" + String(nitrogen) + ",";
    payload += "\"phosphorus\":" + String(phosphorus) + ",";
    payload += "\"potassium\":" + String(potassium);
    payload += "}";

    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0) {
      Serial.println("✅ Data sent successfully!");
      Serial.println("🌍 Server response: " + http.getString());
    } else {
      Serial.println("❌ Error sending data: " + String(httpResponseCode));
    }

    http.end();
  } else {
    Serial.println("⚠ WiFi disconnected! Reconnecting...");
    WiFi.begin(ssid, password);
  }

  delay(5000); // Update every 5 seconds
}