#pragma once
#include <Arduino.h>

// The 14-day retention window scales with the configured sampling interval.
// At the default 10-minute cycle this is 2,016 samples; at 1 minute it is 20,160.
constexpr uint32_t OFFLINE_RETENTION_DAYS = 14;
constexpr uint32_t MAX_CYCLE_MINUTES = 1;
constexpr uint32_t MAX_STORAGE_RECORDS = OFFLINE_RETENTION_DAYS * 24 * 60 / MAX_CYCLE_MINUTES;
constexpr uint32_t LEGACY_QUEUE_CAPACITY = 72;
constexpr uint32_t UPLOAD_BATCH_SIZE = 72; // Supabase ingest accepts at most 72 rows.
constexpr uint32_t WIFI_CONNECT_MS = 20000;
constexpr uint32_t SENSOR_SETTLE_MS = 2000; // Sensors remain externally powered.
constexpr uint32_t NETWORK_BUDGET_MS = 45000;
constexpr uint8_t MAX_BATCHES_PER_WAKE = 4;
constexpr uint32_t DISK_RECORD_MAGIC = 0xFA670601;
constexpr uint32_t QUEUE_META_MAGIC = 0xFA670602;

struct Sample {
  uint64_t id;
  int64_t capturedAt;
  float oxygen, temperature, humidity;
  uint32_t co2;
  uint8_t valid; // bit 0 O2, bit 1 CO2, bit 2 SHT
};

// Runtime metadata only. Sensor records live in a fixed-size SPIFFS ring file.
struct Queue {
  uint32_t magic;
  uint32_t dropped;
  uint64_t firstOrder;
  uint64_t nextOrder;
  uint64_t ackedOrder;
  uint64_t nextId;
};

// Previous compact NVS representation, retained for one-time migration.
struct LegacyQueue {
  uint32_t magic;
  uint16_t count;
  uint32_t dropped;
  uint64_t nextId;
  Sample samples[LEGACY_QUEUE_CAPACITY];
};

// Previous pre-compaction NVS representation, retained for one-time migration.
struct LegacySample {
  char id[17];
  int64_t capturedAt;
  float oxygen, temperature, humidity;
  uint32_t co2;
  uint8_t valid;
};
struct OlderLegacyQueue {
  uint32_t magic;
  uint16_t count;
  uint32_t dropped;
  uint64_t nextId;
  LegacySample samples[LEGACY_QUEUE_CAPACITY];
};

// Fixed 48-byte on-flash format. CRC detects interrupted/corrupted slot writes.
struct DiskSample {
  uint64_t order;
  uint64_t id;
  int64_t capturedAt;
  float oxygen, temperature, humidity;
  uint32_t co2;
  uint8_t valid;
  uint8_t reserved[3];
  uint32_t crc;
};
static_assert(sizeof(DiskSample) == 48, "Disk record format changed; add a storage migration");
static_assert(sizeof(LegacyQueue) == 2904, "Compact NVS layout must match the deployed version");
static_assert(sizeof(OlderLegacyQueue) == 4056, "Legacy NVS layout must match the deployed version");
