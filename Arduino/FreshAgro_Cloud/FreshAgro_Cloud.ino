// FreshAgro: ESP32-C3, LOX-02-S oxygen, GSS CO2, SHT3x temperature/RH.
// Native USB CDC must be enabled. No third-party libraries.
#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <stdlib.h>
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <esp_eap_client.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>
#include "config.h"
#include "QueueTypes.h"

#if !defined(ARDUINO_USB_CDC_ON_BOOT) || !ARDUINO_USB_CDC_ON_BOOT
#error "Enable USB CDC On Boot: UART0 and UART1 are both used by sensors."
#endif

// Change this global variable to set the interval between cycle STARTS.
uint32_t readingCycleMinutes = 10; // Valid range: 1..1440 minutes.
constexpr int SHT_SDA = 1, SHT_SCL = 0;
constexpr int O2_RX = 4, O2_TX = 5;
constexpr int CO2_RX = 6, CO2_TX = 7;
constexpr int LED_PIN = 8;
constexpr uint32_t LED_ON_MS = 1000;
constexpr uint32_t LED_CHECK_GAP_MS = 300;
constexpr uint8_t LED_LEVEL = 64;
constexpr uint32_t UART_TIMEOUT_MS = 2000;
HardwareSerial oxygenPort(0);
HardwareSerial co2Port(1);


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
  Serial.printf("LED 1 sensors: %s for 1 second (%u/3 sensors valid)\n", color, unsigned(validSensors));
  delay(LED_ON_MS);
  ledOff();
  Serial.println("LED: RGB OFF");
}

void indicateCheck(const char *label, bool passed, uint32_t durationMs) {
  delay(LED_CHECK_GAP_MS);
  rgbLedWrite(LED_PIN, passed ? 0 : LED_LEVEL, passed ? LED_LEVEL : 0, 0);
  Serial.printf("LED %s: %s for %lu ms\n", label, passed ? "GREEN" : "RED", (unsigned long)durationMs);
  delay(durationMs);
  ledOff();
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

// A bounded NVS queue survives deep sleep and power loss. One blob per append/ack.
// Capacity = 12 hours at ten-minute cycles. On overflow drop the oldest sample.
Queue queue;
Preferences prefs;
bool storageReady = false;
bool internetReached = false;
bool dataStored = false;
volatile uint16_t lastDisconnectReason = 0;
RTC_DATA_ATTR volatile bool clockTrusted = false;
constexpr time_t BUILD_TIME_FLOOR = 1792354288; // Bootstrap EAP cert dates only, never sample time.
void clockSynced(struct timeval *) { clockTrusted = true; }

bool saveQueue() {
  return storageReady && prefs.putBytes("queue", &queue, sizeof(queue)) == sizeof(queue);
}

void loadQueue() {
  storageReady = prefs.begin("freshagro", false);
  memset(&queue, 0, sizeof(queue)); queue.magic = QUEUE_MAGIC;
  queue.nextId = (uint64_t(esp_random()) << 32) | esp_random();
  if (!storageReady) return;
  size_t bytes = prefs.getBytesLength("queue");
  if (bytes == sizeof(queue)) {
    static Queue candidate{};
    if (prefs.getBytes("queue", &candidate, sizeof(candidate)) == sizeof(candidate) &&
        candidate.magic == QUEUE_MAGIC && candidate.count <= QUEUE_CAPACITY) queue = candidate;
  } else if (bytes == sizeof(LegacyQueue)) {
    static LegacyQueue legacy{};
    if (prefs.getBytes("queue", &legacy, sizeof(legacy)) == sizeof(legacy) &&
        legacy.magic == 0xFA670004 && legacy.count <= QUEUE_CAPACITY) {
      queue.count = legacy.count; queue.dropped = legacy.dropped; queue.nextId = legacy.nextId;
      for (size_t i = 0; i < queue.count; ++i) {
        Sample &s = queue.samples[i]; const LegacySample &old = legacy.samples[i];
        char id[17]; memcpy(id, old.id, 16); id[16] = 0;
        s.id = strtoull(id, nullptr, 16); s.capturedAt = old.capturedAt;
        s.oxygen = old.oxygen; s.temperature = old.temperature; s.humidity = old.humidity;
        s.co2 = old.co2; s.valid = old.valid;
      }
      Serial.printf("Migrated %u saved sample(s) to compact queue.\n", queue.count);
    }
  }
}

Sample captureSample() {
  Sample s{};
  // Persisted sequence prevents ID reuse across wakes and power loss.
  s.id = queue.nextId++;
  time_t now = time(nullptr);
  s.capturedAt = clockTrusted && now >= 1704067200 ? now : 0;
  s.oxygen = s.temperature = s.humidity = NAN;
  int status; uint32_t raw, scale; bool gotRaw;
  bool shtOK = readSHT(s.temperature, s.humidity);
  bool o2OK = readOxygen(s.oxygen, status);
  bool co2OK = readCO2(raw, s.co2, scale, gotRaw);
  s.valid = (o2OK ? 1 : 0) | (co2OK ? 2 : 0) | (shtOK ? 4 : 0);
  Serial.printf("Sample %016llx: O2=%s CO2=%s SHT=%s\n", (unsigned long long)s.id, o2OK ? "valid" : "unavailable",
                co2OK ? "valid" : "unavailable", shtOK ? "valid" : "unavailable");
  indicateCycle(uint8_t(o2OK) + uint8_t(co2OK) + uint8_t(shtOK));
  return s;
}

String sampleJSON(const Sample &s) {
  char number[48];
  snprintf(number, sizeof(number), "%016llx", (unsigned long long)s.id);
  String json = "{\"sample_id\":\"" + String(number) + "\",\"sampled_at\":";
  if (s.capturedAt) { snprintf(number, sizeof(number), "%lld", (long long)s.capturedAt); json += number; }
  else json += "null";
  json += ",\"o2_valid\":"; json += (s.valid & 1) ? "true" : "false";
  json += ",\"co2_valid\":"; json += (s.valid & 2) ? "true" : "false";
  json += ",\"sht_valid\":"; json += (s.valid & 4) ? "true" : "false";
  json += ",\"oxygen\":"; json += (s.valid & 1) ? String(s.oxygen, 2) : "null";
  json += ",\"co2\":"; json += (s.valid & 2) ? String(s.co2) : "null";
  json += ",\"temperature\":"; json += (s.valid & 4) ? String(s.temperature, 2) : "null";
  json += ",\"humidity\":"; json += (s.valid & 4) ? String(s.humidity, 2) : "null";
  return json + ",\"firmware\":\"cloud-1.0\"}";
}

bool configured() {
  bool networkReady = strlen(WIFI_SSID);
#if WIFI_ENTERPRISE
  networkReady = networkReady && strlen(EAP_USERNAME) && strlen(EAP_PASSWORD) &&
                 strlen(EAP_CA) && strlen(EAP_SERVER_DOMAIN);
#endif
  return networkReady && strlen(DEVICE_TOKEN) >= 32 && strlen(ROOT_CA) &&
         String(INGEST_URL).startsWith("https://");
}

bool eapChecked(const char *stage, esp_err_t result) {
  // Log only operation/result, never identities, passwords or certificate contents.
  Serial.printf("EAP setup %s: %s\n", stage, esp_err_to_name(result));
  return result == ESP_OK;
}

void uploadQueue(uint32_t captureMillis) {
  if (!configured()) { Serial.println("Cloud not configured; queued locally, radio remains off."); return; }
  const uint32_t started = millis();
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED) {
      Serial.printf("Wi-Fi associated at %lu ms; awaiting IP address.\n", millis());
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      Serial.printf("Wi-Fi obtained IP at %lu ms.\n", millis());
    }
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      lastDisconnectReason = info.wifi_sta_disconnected.reason;
      Serial.printf("Wi-Fi disconnected, reason=%u\n", unsigned(lastDisconnectReason));
    }
  });
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(false);
#if WIFI_ENTERPRISE
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
    // Manual-start diagnostic only: regular battery cycles do not add a scan.
    int found = WiFi.scanNetworks(false, false, false, 120, 0, WIFI_SSID);
    Serial.printf("Configured SSID scan: %d access points\n", found);
    for (int i = 0; i < found; ++i) {
      Serial.printf("  AP channel=%ld RSSI=%ld auth=%d\n", long(WiFi.channel(i)), long(WiFi.RSSI(i)), int(WiFi.encryptionType(i)));
    }
    WiFi.scanDelete();
  }
  // Verify server identity before releasing inner credentials. No CA-free fallback.
  if (time(nullptr) < BUILD_TIME_FLOOR) {
    struct timeval bootstrap = {BUILD_TIME_FLOOR, 0}; settimeofday(&bootstrap, nullptr);
  }
  const char *outerIdentity = strlen(EAP_IDENTITY) ? EAP_IDENTITY : EAP_USERNAME;
  // Arduino's enterprise overload discards these return codes. Check each one.
  // The connected laptop confirms PEAP/MSCHAPv2 for this network.
  if (!eapChecked("PEAP method", esp_eap_client_set_eap_methods(ESP_EAP_TYPE_PEAP)) ||
      !eapChecked("server domain", esp_eap_client_set_domain_name(EAP_SERVER_DOMAIN)) ||
      !eapChecked("certificate dates", esp_eap_client_set_disable_time_check(false)) ||
      !eapChecked("CA", esp_eap_client_set_ca_cert(reinterpret_cast<const unsigned char *>(EAP_CA), strlen(EAP_CA))) ||
      !eapChecked("identity", esp_eap_client_set_identity(reinterpret_cast<const unsigned char *>(outerIdentity), strlen(outerIdentity))) ||
      !eapChecked("username", esp_eap_client_set_username(reinterpret_cast<const unsigned char *>(EAP_USERNAME), strlen(EAP_USERNAME))) ||
      !eapChecked("password", esp_eap_client_set_password(reinterpret_cast<const unsigned char *>(EAP_PASSWORD), strlen(EAP_PASSWORD))) ||
      !eapChecked("enable", esp_wifi_sta_enterprise_enable())) return;
  Serial.println("Joining enterprise Wi-Fi; PEAP only, CA/domain/date checks enabled.");
  WiFi.begin(WIFI_SSID);
#else
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#endif
  while (WiFi.status() != WL_CONNECTED && uint32_t(millis() - started) < WIFI_CONNECT_MS) delay(50);
  if (WiFi.status() != WL_CONNECTED) { Serial.printf("Wi-Fi timeout status=%d reason=%u; queue retained.\n", int(WiFi.status()), unsigned(lastDisconnectReason)); return; }
  Serial.printf("Wi-Fi connected; RSSI=%d.\n", WiFi.RSSI());
  // TLS requires a valid clock. The RTC clock normally survives timer deep sleep.
  sntp_set_time_sync_notification_cb(clockSynced);
  configTime(0, 0, "time.google.com", "time.cloudflare.com", "pool.ntp.org");
  uint32_t clockStart = millis();
  // lwIP may delay its first request by up to five seconds. Allow time for
  // DNS and the response as well; trusted RTC time skips this wait on timer wakes.
  while (!clockTrusted && uint32_t(millis() - clockStart) < 15000) delay(50);
  if (!clockTrusted || time(nullptr) < 1704067200) { Serial.println("Clock unavailable; no insecure TLS fallback."); return; }
  Serial.printf("Clock ready after %lu ms.\n", uint32_t(millis() - clockStart));
  if (!queue.samples[queue.count - 1].capturedAt) {
    queue.samples[queue.count - 1].capturedAt = time(nullptr) - uint32_t(millis() - captureMillis) / 1000;
    if (!saveQueue()) { Serial.println("Timestamp persistence failed; retaining queue."); return; }
  }
  String payload; payload.reserve(queue.count * 250 + 32); payload = "{\"samples\":[";
  for (size_t i = 0; i < queue.count; ++i) { if (i) payload += ','; payload += sampleJSON(queue.samples[i]); }
  payload += "]}";
  NetworkClientSecure client;
  client.setCACert(ROOT_CA); client.setHandshakeTimeout(6);
  // At most two attempts, fixed connection/read/handshake timeouts, no redirect forwarding.
  for (int attempt = 0; attempt < 2 && uint32_t(millis() - started) < NETWORK_BUDGET_MS; ++attempt) {
    HTTPClient http;
    http.setConnectTimeout(4000); http.setTimeout(5000);
    http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
    if (!http.begin(client, INGEST_URL)) break;
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-device-token", DEVICE_TOKEN);
    int code = http.POST(payload);
    // A verified HTTPS response proves the cloud endpoint is reachable.
    // Reuse the upload itself: no extra network probe or TLS handshake.
    if (code > 0) internetReached = true;
    // The ingestion contract returns 200 only after atomic database commit.
    http.end(); client.stop();
    if (code == 200) {
      dataStored = true; // Server contract: full batch committed (or deduplicated).
      uint16_t previousCount = queue.count; queue.count = 0;
      if (!saveQueue()) { queue.count = previousCount; Serial.println("Ack persistence failed; safe duplicate retry next wake."); }
      else Serial.println("Batch stored; local queue acknowledged.");
      return;
    }
    Serial.printf("Upload status %d; queue retained.\n", code);
    if (code >= 400 && code < 500 && code != 408 && code != 429) break;
    if (attempt == 0) delay(200);
  }
}

void setup() {
  const int64_t cycleStart = esp_timer_get_time();
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) clockTrusted = false;
  Serial.begin(115200);
  // Only manual/power resets offer a USB diagnostic window; timer wakes skip it.
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) delay(4000);
  Serial.printf("FreshAgro cloud boot: reset=%d wake=%d interval=%lu min\n", int(esp_reset_reason()),
                int(esp_sleep_get_wakeup_cause()), (unsigned long)readingCycleMinutes);
  ledOff();
  WiFi.mode(WIFI_OFF);
  const uint64_t intervalUs = uint64_t(cycleIntervalMs()) * 1000ULL;
  loadQueue();
  oxygenPort.setRxBufferSize(256); co2Port.setRxBufferSize(256);
  oxygenPort.begin(9600, SERIAL_8N1, O2_RX, O2_TX);
  co2Port.begin(9600, SERIAL_8N1, CO2_RX, CO2_TX);
  startShtBus();
  delay(SENSOR_SETTLE_MS);
  uint32_t captureMillis = millis();
  Sample sample = captureSample();
  if (queue.count == QUEUE_CAPACITY) {
    memmove(queue.samples, queue.samples + 1, (QUEUE_CAPACITY - 1) * sizeof(Sample));
    --queue.count; ++queue.dropped;
    Serial.printf("Queue full; oldest dropped, total dropped=%lu\n", (unsigned long)queue.dropped);
  }
  queue.samples[queue.count++] = sample;
  if (saveQueue()) uploadQueue(captureMillis);
  else Serial.println("NVS write failed; sample is not durably saved. Skipping upload.");
  WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
  // Show results with the radio already off to minimize energy use.
  indicateCheck("2 internet/cloud reachability", internetReached, 1000);
  indicateCheck("3 data stored", dataStored, 500);
  Wire.end(); oxygenPort.end(); co2Port.end();
  ledOff();
  if (storageReady) prefs.end();
  // Account for acquisition/network/LED time: ten-minute start-to-start target.
  uint64_t awakeUs = uint64_t(esp_timer_get_time() - cycleStart);
  uint64_t sleepUs = awakeUs < intervalUs ? intervalUs - awakeUs : intervalUs;
  Serial.printf("Queued=%u dropped=%lu; awake=%llu ms, deep sleep=%llu ms\n", queue.count,
                (unsigned long)queue.dropped, (unsigned long long)(awakeUs / 1000), (unsigned long long)(sleepUs / 1000));
  // External sensor power is controlled by the user's hardware, not this sketch.
  // RGB off is latched before sleep; do not hold an RMT waveform on GPIO8.
  delay(5);
  esp_sleep_enable_timer_wakeup(sleepUs);
  esp_deep_sleep_start();
}
void loop() {}
