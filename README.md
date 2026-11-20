# FreshAgro project handoff

This folder collects the deployable FreshAgro source in one place. The latest cloud firmware identified in the project’s upload and serial-test records is `Arduino/FreshAgro_Cloud/FreshAgro_Cloud.ino`. It is the ten-minute wake/read/upload/deep-sleep sketch with three LED status checks. Its matching headers are beside it. The separately preserved `Arduino/Hardware_Validated/FreshAgro.ino` is the known hardware-only rollback sketch with two-minute readings.

## Project flow

![FreshAgro sensor-to-dashboard flow chart](Docs/PROJECT_FLOW.svg)

[Open the flow chart notes](Docs/PROJECT_FLOW.md).

## Hardware photos

| Sensor and controller side | Battery and reverse side |
|:--:|:--:|
| ![FreshAgro sensor and ESP32-C3 assembly](Docs/hardware/hardware-sensor-side.jpeg) | ![FreshAgro battery pack and board reverse side](Docs/hardware/hardware-battery-side.jpeg) |

## Project parts

- `Arduino/FreshAgro_Cloud/`: cloud-connected ESP32-C3 firmware. Open the `.ino` in Arduino IDE with ESP32-C3 Dev Module and USB CDC On Boot enabled. Set `readingCycleMinutes` near the top of the sketch to change the interval.
- `Web/`: static Vercel dashboard. Production URL: https://freshagro-bice.vercel.app/
- `Supabase/`: database configuration/migration and authenticated ingestion Edge Function. The Supabase project ref is `ucqfewqkwvmfitkibcvh`.
- `Tests/`: Node tests for dashboard, ingest handler and payload validation.

## Arduino configuration

Copy `Arduino/FreshAgro_Cloud/config.local.example.h` to `config.local.h` in the same sketch directory and fill in your Wi-Fi, ingestion URL, device token, and verified TLS root certificate locally. The raw device token must match the SHA-256 token hash configured as the Supabase Edge Function secret. Do not commit `config.local.h`, Wi-Fi credentials, EAP credentials, device tokens, or service-role keys. `config.h` uses empty safe defaults when the local file is absent.

The sketch can use WPA2-Personal or WPA2-Enterprise; institution-specific EAP settings and certificates belong only in your private local configuration. Verify those settings before relying on eduroam.

## Offline queue

The cloud firmware keeps a CRC-checked, fixed-slot queue in the existing SPIFFS partition, with small progress metadata in NVS. The compact ring holds **2,016 readings (14 days at the default 10-minute cycle)**. Faster cycles retain fewer days. A previous, larger ring remains readable while it has pending data and is retired on a later boot after it drains. The older 72-reading NVS queue is migrated on first boot, and its old copy remains until the flash records and metadata are saved. Uploads contain at most 72 samples and send up to four batches per wake. Server HTTP 200 advances the saved queue cursor; unacknowledged records are retried with stable IDs. If the ring fills, the oldest unacknowledged samples expire.

## Supabase deployment

The deployed project is `ucqfewqkwvmfitkibcvh`. The migration in `Supabase/migrations/001_readings.sql` creates the telemetry table with public read-only access. The Edge Function verifies the device token and writes with the server-only service-role key. Keep the service-role key in Supabase secrets only. Deployment notes and a combined function source are in `Supabase/ingest-deploy.ts`.

## Validation and current status

Run the available tests with `node --test Tests/*.test.mjs`. The dashboard is deployed. On 2026-11-20, the ESP32-C3 connected through the 2.4 GHz Pie hotspot, uploaded its saved backlog to Supabase, migrated to the compact ring, and acknowledged a newly captured reading with zero pending records. Supabase contained 1,062 readings after that check. One incomplete old flash record could not be recovered; its raw bytes remain in the private flash backup. The 10-minute battery-input current and a sustained unattended sleep/wake cycle have not been measured/verified.

The board was flashed with the clean cloud firmware and tested through a physical reset. The source and required sketch headers are included here. No private local configuration or compiled firmware binary is included; recreate local settings from the example before uploading.
