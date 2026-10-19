#pragma once
#include <Arduino.h>
constexpr size_t QUEUE_CAPACITY = 72;
constexpr uint32_t QUEUE_MAGIC = 0xFA670005;
constexpr uint32_t WIFI_CONNECT_MS = 20000;
constexpr uint32_t SENSOR_SETTLE_MS = 2000; // Sensors remain externally powered.
constexpr uint32_t NETWORK_BUDGET_MS = 45000;
struct Sample {
  uint64_t id;
  int64_t capturedAt;
  float oxygen, temperature, humidity;
  uint32_t co2;
  uint8_t valid; // bit 0 O2, bit 1 CO2, bit 2 SHT
};
struct Queue {
  uint32_t magic;
  uint16_t count;
  uint32_t dropped;
  uint64_t nextId;
  Sample samples[QUEUE_CAPACITY];
};

// Previous 4,056-byte blob straddled NVS pages; migrate without erasing saved data.
struct LegacySample {
  char id[17]; int64_t capturedAt; float oxygen, temperature, humidity;
  uint32_t co2; uint8_t valid;
};
struct LegacyQueue {
  uint32_t magic; uint16_t count; uint32_t dropped; uint64_t nextId;
  LegacySample samples[QUEUE_CAPACITY];
};
static_assert(sizeof(Queue) < 3500, "Queue must fit within a single NVS data page");

static_assert(sizeof(LegacyQueue) == 4056, "Legacy NVS layout must match migration format");
