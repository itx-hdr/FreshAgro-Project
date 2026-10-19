// FreshAgro: ESP32-C3, LOX-02-S oxygen, GSS CO2, SHT3x temperature/RH.
// Native USB CDC must be enabled. No third-party libraries.
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <stdlib.h>

#if !defined(ARDUINO_USB_CDC_ON_BOOT) || !ARDUINO_USB_CDC_ON_BOOT
#error "Enable USB CDC On Boot: UART0 and UART1 are both used by sensors."
#endif

// Change this global variable to set the interval between cycle STARTS.
uint32_t readingCycleMinutes = 2; // Valid range: 1..1440 minutes.
constexpr int SHT_SDA = 1, SHT_SCL = 0;
constexpr int O2_RX = 4, O2_TX = 5;
constexpr int CO2_RX = 6, CO2_TX = 7;
constexpr int LED_PIN = 8;
constexpr uint32_t LED_ON_MS = 2000;
constexpr uint8_t LED_LEVEL = 64;
constexpr uint32_t UART_TIMEOUT_MS = 2000;
HardwareSerial oxygenPort(0);
HardwareSerial co2Port(1);
uint32_t lastCycleStart = 0;
uint32_t cycleNumber = 0;
bool firstCycle = true;

void ledOff() {
  rgbLedWrite(LED_PIN, 0, 0, 0);
  // Leave GPIO8 owned by the RGB driver; do not remap it to ordinary GPIO.
  delay(1); // Allow the off frame to latch.
}

void indicateCycle(uint8_t validSensors) {
  // Count sensor devices: SHT temperature/humidity together count as one.
  const char *color;
  if (validSensors == 3) {
    rgbLedWrite(LED_PIN, 0, LED_LEVEL, 0);
    color = "GREEN";
  } else if (validSensors > 0) {
    rgbLedWrite(LED_PIN, LED_LEVEL, LED_LEVEL / 4, 0);
    color = "ORANGE";
  } else {
    rgbLedWrite(LED_PIN, LED_LEVEL, 0, 0);
    color = "RED";
  }
  Serial.printf("LED: %s for 2 seconds (%u/3 sensors valid)\n", color, unsigned(validSensors));
  delay(LED_ON_MS);
  ledOff();
  Serial.println("LED: RGB OFF");
}

void drainPort(HardwareSerial &port) {
  for (int i = 0; i < 256 && port.available(); ++i) port.read();
}

bool parseNumber(const char *line, char tag, bool multiField, float &value) {
  for (const char *p = line; *p; ++p) {
    if ((p != line && p[-1] != ' ') || p[0] != tag || p[1] != ' ') continue;
    const char *start = p + 2;
    while (*start == ' ') ++start;
    if (!(*start == '+' || *start == '-' || *start == '.' || (*start >= '0' && *start <= '9'))) return false;
    char *end;
    float n = strtof(start, &end);
    if (end == start || !isfinite(n)) return false;
    if (*end && *end != ' ') return false;
    while (*end == ' ') ++end;
    if (*end && !multiField) return false;
    value = n;
    return true;
  }
  return false;
}

bool queryNumber(HardwareSerial &port, const char *command, char tag,
                 bool multiField, float &value) {
  drainPort(port);
  port.print(command); port.print("\r\n");
  char line[128];
  size_t used = 0;
  bool overflow = false;
  uint32_t started = millis();
  while (uint32_t(millis() - started) < UART_TIMEOUT_MS) {
    for (int i = 0; i < 256 && port.available(); ++i) {
      char c = port.read();
      if (c == '\n') {
        bool complete = used && line[used - 1] == '\r';
        if (complete) --used;
        line[used] = '\0';
        if (!overflow && complete && parseNumber(line, tag, multiField, value)) return true;
        used = 0; overflow = false;
      } else if (!overflow) {
        if (used < sizeof(line) - 1) line[used++] = c;
        else overflow = true;
      }
    }
    delay(1);
  }
  return false;
}

bool readOxygen(float &oxygen, int &status) {
  float mode, candidate, state;
  status = -1;
  if (!queryNumber(oxygenPort, "M 1", 'M', false, mode) || mode != 1) return false;
  bool gotOxygen = queryNumber(oxygenPort, "%", '%', false, candidate);
  bool gotStatus = queryNumber(oxygenPort, "e", 'e', false, state);
  if (gotStatus && state >= 0 && state <= 9999 && floorf(state) == state) status = int(state);
  if (!gotOxygen || status != 0 || candidate < 0 || candidate > 25) return false;
  oxygen = candidate;
  return true;
}

bool readCO2(uint32_t &raw, uint32_t &ppm, uint32_t &scale, bool &gotRaw) {
  // Reconfirm scale each cycle; never reuse a scale after a sensor reset.
  float reportedScale, measurement;
  scale = 0;
  gotRaw = false;
  if (queryNumber(co2Port, ".", '.', false, reportedScale) &&
      (reportedScale == 1 || reportedScale == 10 || reportedScale == 100))
    scale = uint32_t(reportedScale);
  if (!queryNumber(co2Port, "Z", 'Z', true, measurement) ||
      measurement < 0 || measurement > 1000000 || floorf(measurement) != measurement) return false;
  raw = uint32_t(measurement);
  gotRaw = true;
  if (!scale || uint64_t(raw) * scale > 1000000ULL) return false;
  ppm = raw * scale;
  return true;
}

uint8_t shtCRC(const uint8_t *data) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < 2; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b)
      crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x31) : uint8_t(crc << 1);
  }
  return crc;
}

bool shtCommand(uint8_t address, uint16_t command) {
  Wire.beginTransmission(address);
  Wire.write(uint8_t(command >> 8)); Wire.write(uint8_t(command));
  return Wire.endTransmission() == 0;
}

void startShtBus() {
  Wire.end();
  digitalWrite(SHT_SDA, HIGH);
  digitalWrite(SHT_SCL, HIGH);
  pinMode(SHT_SDA, OUTPUT_OPEN_DRAIN);
  pinMode(SHT_SCL, OUTPUT_OPEN_DRAIN);
  // Release an interrupted slave transfer, then generate an explicit STOP.
  for (int i = 0; i < 9; ++i) {
    digitalWrite(SHT_SCL, LOW); delayMicroseconds(10);
    digitalWrite(SHT_SCL, HIGH); delayMicroseconds(10);
  }
  digitalWrite(SHT_SCL, LOW);
  digitalWrite(SHT_SDA, LOW); delayMicroseconds(10);
  digitalWrite(SHT_SCL, HIGH); delayMicroseconds(10);
  digitalWrite(SHT_SDA, HIGH); delayMicroseconds(10);
  Wire.begin(SHT_SDA, SHT_SCL);
  Wire.setClock(100000); Wire.setTimeOut(100);
}

bool readSHT(float &temperature, float &humidity) {
  Serial.printf("SHT I2C SDA=GPIO1 SCL=GPIO0 idle: SDA=%d SCL=%d\n", digitalRead(SHT_SDA), digitalRead(SHT_SCL));
  const uint8_t addresses[] = {0x44, 0x45};
  for (uint8_t address : addresses) {
    Wire.beginTransmission(address);
    uint8_t error = Wire.endTransmission();
    Serial.printf("SHT probe 0x%02X: I2C result %u (0=ACK, 2=NACK, 5=timeout)\n", address, error);
    if (error != 0) continue;
    if (!shtCommand(address, 0x3093)) { Serial.println("SHT: break command failed"); continue; }
    delay(2);
    if (!shtCommand(address, 0x3066)) { Serial.println("SHT: heater-off failed"); continue; }
    delay(2);
    if (!shtCommand(address, 0x2400)) { Serial.println("SHT: measurement command failed"); continue; }
    delay(20);
    if (Wire.requestFrom(address, uint8_t(6)) != 6) { Serial.println("SHT: incomplete response"); continue; }
    uint8_t data[6];
    for (uint8_t &b : data) b = uint8_t(Wire.read());
    if (shtCRC(data) != data[2] || shtCRC(data + 3) != data[5]) { Serial.println("SHT: CRC mismatch"); continue; }
    uint16_t t = (uint16_t(data[0]) << 8) | data[1];
    uint16_t h = (uint16_t(data[3]) << 8) | data[4];
    temperature = -45.0f + 175.0f * t / 65535.0f;
    humidity = 100.0f * h / 65535.0f;
    return true;
  }
  return false;
}

uint32_t cycleIntervalMs() {
  if (readingCycleMinutes < 1 || readingCycleMinutes > 1440) {
    Serial.println("Invalid readingCycleMinutes; using 1 minute (allowed: 1..1440).");
    readingCycleMinutes = 1;
  }
  return readingCycleMinutes * 60000UL;
}

void captureCycle() {
  // Every cycle owns fresh values: failed reads cannot display old samples.
  float oxygen = NAN, temperature = NAN, humidity = NAN;
  int o2Status = -1;
  uint32_t raw = 0, ppm = 0, scale = 0;
  bool gotRaw = false;
  uint32_t started = millis();
  ledOff();
  Serial.printf("\n=== Cycle %lu | uptime %lu ms | interval %lu minute(s) ===\n",
                (unsigned long)++cycleNumber, (unsigned long)started,
                (unsigned long)readingCycleMinutes);
  bool shtOK = readSHT(temperature, humidity);
  bool oxygenOK = readOxygen(oxygen, o2Status);
  bool co2OK = readCO2(raw, ppm, scale, gotRaw);

  if (oxygenOK) Serial.printf("O2: %.2f %% vol | status e 0000 | VALID\n", oxygen);
  else Serial.printf("O2: UNAVAILABLE | status=%d (-1=no valid status)\n", o2Status);
  if (co2OK) Serial.printf("CO2: %lu ppm (%.4f %% vol) | raw=%lu scale=%lu | VALID\n",
      (unsigned long)ppm, ppm / 10000.0, (unsigned long)raw, (unsigned long)scale);
  else if (gotRaw) Serial.printf("CO2: raw=%lu | ppm UNVERIFIED (scale/range invalid)\n", (unsigned long)raw);
  else Serial.println("CO2: UNAVAILABLE");
  if (shtOK) Serial.printf("Temperature: %.2f C | Humidity: %.2f %%RH | CRC OK | VALID\n", temperature, humidity);
  else Serial.println("Temperature/Humidity: UNAVAILABLE (I2C/CRC failure)");

  bool complete = shtOK && oxygenOK && co2OK;
  Serial.printf("Cycle: %s | acquisition duration %lu ms\n", complete ? "PASS" : "INCOMPLETE",
                (unsigned long)(millis() - started));
  indicateCycle(uint8_t(shtOK) + uint8_t(oxygenOK) + uint8_t(co2OK));
}

void setup() {
  ledOff();
  Serial.begin(115200);
  delay(2000);
  Serial.println("\nFreshAgro COMBINED: SHT SDA=1 SCL=0; O2 RX=4 TX=5; CO2 RX=6 TX=7; LED=8");
  cycleIntervalMs();
  oxygenPort.setRxBufferSize(256); co2Port.setRxBufferSize(256);
  oxygenPort.begin(9600, SERIAL_8N1, O2_RX, O2_TX);
  co2Port.begin(9600, SERIAL_8N1, CO2_RX, CO2_TX);
  startShtBus();
  delay(2000);
  Serial.println("First cycle immediately; sensors remain powered between cycles.");
}

void loop() {
  uint32_t interval = cycleIntervalMs();
  uint32_t now = millis();
  if (firstCycle || uint32_t(now - lastCycleStart) >= interval) {
    firstCycle = false;
    lastCycleStart = now;
    captureCycle();
    Serial.println("Waiting for next scheduled cycle; LED OFF.");
  }
  // Prevent streamed data from accumulating during the interval.
  drainPort(oxygenPort); drainPort(co2Port);
  delay(5);
}
