# FreshAgro combined sensor monitor

Open `FreshAgro.ino` in Arduino IDE. Board: **ESP32C3 Dev Module**, **USB CDC On Boot enabled**. Serial Monitor: **115200 baud**. Both hardware UARTs are used by gas sensors; USB CDC is required for debug output.

## Reading interval

Edit the global variable near the top of the sketch:

```cpp
uint32_t readingCycleMinutes = 2;
```

Use whole minutes from 1 to 1440. Invalid settings fall back to one minute. The first cycle runs immediately after startup; subsequent starts are scheduled by elapsed time from the previous start, using rollover-safe `millis()` subtraction. Changing the source requires recompiling/uploading. Reads are sequential within a cycle, not simultaneous. Timeouts can extend acquisition duration; the console reports that duration.

## Wiring

| Device | ESP32-C3 connection |
| --- | --- |
| SHT3x SDA | GPIO1 |
| SHT3x SCL | GPIO0 |
| SHT3x VCC / GND | 3.3V / common GND; I2C pull-ups to 3.3V |
| LOX-02-S TX / RX | GPIO4 / GPIO5 |
| GSS CO2 TX / RX | GPIO6 / GPIO7 |
| Onboard LED | GPIO8; independent of I2C |

Gas UARTs: 9600, 8N1. Retain the already-tested gas sensor supplies. LOX-02-S requires 5V supply with 3.3V UART logic; GSS supply depends on exact model.

## Validation and indication

- O2: polling-mode acknowledgement, valid concentration in 0–25% vol and status `e 0000` are all required.
- CO2: re-query sensor scale each cycle; only reported multipliers 1, 10 or 100 enable ppm conversion. Concentration also prints as volume percent. Raw readings without a verified scale are marked unverified. No GSS calibration or operating-mode changes are sent.
- SHT3x: check addresses 0x44/0x45, use single-shot measurement with heater off, verify both CRCs.
- Every cycle has fresh values and per-sensor validity. Unavailable readings do not reuse old values.
- LED: two-second RGB status pulse on GPIO8 after every cycle: green = 3/3 valid sensors, orange = 1–2/3, red = 0/3, then off.

Sensors remain powered between cycles. This version does not implement deep sleep, gas-sensor power switching, storage or a network dashboard. Communication checks do not establish calibration accuracy or guarantee perfect measurements.

Individual test sketches remain in `O2_Test`, `GSS_CO2`, `SHT3x_Test`, and `LED_Test`.

## Combined hardware validation

Combined sketch compiled and uploaded through Arduino IDE. All three sensors now pass with SHT3x SDA=GPIO1/SCL=GPIO0. Earlier failures below are historical.

2026-10-16: Combined sketch compiled and uploaded through Arduino IDE; flash hash verified. Live cycle 2 at uptime 64022 ms reported O2 19.81% (e 0000) and CO2 2500 ppm (scale 10), but SHT3x was unavailable. Cycle correctly reported INCOMPLETE and commanded LED off. Full three-sensor validation is pending SHT3x connection/power checks.

Latest diagnostic build: 343178 bytes flash / 15348 bytes global RAM, uploaded and running. At 12:43:52 Asia/Karachi time, both SHT addresses returned NACK (I2C result 2) after startup bus recovery. Idle SDA/SCL were both high. O2: 19.87% with e 0000; CO2: 2350 ppm at scale 10. Cycle correctly marked INCOMPLETE with LED off. Previous cycles started at uptime 4018 and 64020 ms, consistent with the configured one-minute interval.

The user confirmed SHT3x was moved to GPIO0/1. It still does not acknowledge; full hardware validation requires checking power at the sensor and continuity of SDA/SCL/GND. The startup recovery releases nine clock pulses followed by STOP before initializing I2C, and diagnostics distinguish address failures from command/CRC failures.

## Retry — 2026-10-18

Arduino IDE Serial Monitor at 115200 baud received a fresh combined cycle at 23:42:13 Asia/Karachi time (cycle 4, uptime 184024 ms, interval 1 minute). O2: 19.85% with e 0000; CO2: 3410 ppm using scale 10. SHT3x address probes 0x44 and 0x45 both returned NACK (result 2); idle SDA/SCL were high. Temperature/humidity remain unavailable. Cycle reported INCOMPLETE and LED off. No firmware change or reflash was needed for this retry.

## SHT3x raw diagnosis — 2026-10-18

The extended diagnostic identified reversed signal wiring: SDA=GPIO1 and SCL=GPIO0 respond at address 0x44; SDA=GPIO0/SCL=GPIO1 finds no devices. Software-I2C raw frame at 23:52:44.883: `63 C5 4B D0 6B 14`, both CRCs pass, rawT=25541, rawRH=53355, 23.20 C / 81.41 %RH. Four measurement modes/reset attempts returned CRC-valid measurements. Combined source now uses the working SDA=1/SCL=0 mapping; earlier failures above are historical.

## Combined success after pin correction — 2026-10-18

Corrected firmware compiled (343186 bytes flash, 15348 bytes global RAM) and uploaded through Arduino IDE with flash hash verification. At 23:56:38.774 Asia/Karachi time, scheduled cycle 2 started at uptime 64021 ms, after cycle 1 at 4018 ms (60003 ms apart). Results:

```text
O2: 19.84 % vol | status e 0000 | VALID
CO2: 3090 ppm (0.3090 % vol) | raw=309 scale=10 | VALID
Temperature: 22.74 C | Humidity: 81.75 %RH | CRC OK | VALID
Cycle: PASS | acquisition duration 97 ms
LED: valid-cycle pulse, then OFF
```

The hardware-I2C combined monitor works with SDA=GPIO1 and SCL=GPIO0; no software-I2C fallback is needed in the main sketch. Current ESP32 firmware is the combined FreshAgro monitor, not the raw diagnostic.

## Two-minute cycle — 2026-10-19

Changed `readingCycleMinutes` to 2 and uploaded through Arduino IDE. Live cycle 1 at 00:17:33.976 Asia/Karachi time reported `interval 2 minute(s)` and PASS: O2 19.86%, CO2 3110 ppm (scale 10), temperature 22.99 C, humidity 80.87 %RH with valid CRC. LED pulse/off sequence logged. First cycle still runs immediately after startup; later cycle starts use the configured 120000 ms interval. The full two-minute gap was not timed during this upload check.

## LED visibility retest — 2026-10-19

User reported the earlier 250 ms RGB pulse was not visible. Live cycles were PASS, so the validity gate was not the cause. Changed successful-cycle indication to explicit GPIO8 HIGH for 2000 ms, followed by the existing off command and GPIO8 LOW. This tests a direct GPIO-driven indicator; the board LED circuitry remains unidentified. Uploaded through Arduino IDE. At 00:33:17.081, all sensors passed and `LED: GPIO8 HIGH for 2 seconds` was logged; at 00:33:19.094, `LED: GPIO8 LOW / OFF` was logged. User visual confirmation pending. Two-minute reading interval retained.


## RGB LED retry — 2026-10-19

User reported no visible flash with direct GPIO HIGH control. Replaced it with RGB white level 64 for two seconds, followed by RGB off, without remapping GPIO8 to ordinary GPIO. Uploaded through Arduino IDE. Fresh cycle at 00:39:28 passed: O2 19.85%, CO2 2800 ppm (scale 10), temperature 23.09 C, humidity 79.90 %RH with CRC OK. New RGB white/off commands logged two seconds apart. User subsequently reset the board and visually confirmed: “Turns on, then stays off.” The RGB indication is now visually verified. Reading interval remains two minutes.


## Sensor status colors

After each two-minute reading cycle, GPIO8 RGB LED lights for two seconds, then turns off: green for 3/3 valid sensors, orange for 1/3 or 2/3, red for 0/3. SHT3x temperature/humidity counts as one sensor; validity checks are unchanged. Brightness level is 64; orange uses red 64 and green 16. Colored physical indication awaits visual verification.

Color firmware compiled and uploaded through Arduino IDE. Live cycle at 00:43:54 on 2026-10-19: all three sensors valid; GREEN (3/3) logged, then RGB OFF two seconds later. Physical color confirmation and live failure-color tests remain unverified.
