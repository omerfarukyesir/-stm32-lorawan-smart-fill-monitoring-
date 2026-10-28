# Uplink Payload Format (v1)

Every node sends one **11-byte** uplink per reporting cycle (default 30 min) and an
additional uplink immediately when a priority alarm is newly raised.
All multi-byte fields are **big-endian**.

| Byte | Field | Type | Unit / encoding |
|------|-------|------|-----------------|
| 0 | header | `uint8` | bits 7–4: payload version (`1`) · bit 3: priority flag · bit 2: reserved · bits 1–0: material |
| 1 | fill level | `uint8` | 0–100 %, `0xFF` = not available |
| 2–3 | alarm mask | `uint16` | see table below |
| 4–5 | temperature | `int16` | 0.1 °C, two's complement |
| 6 | humidity | `uint8` | %RH |
| 7–8 | battery voltage | `uint16` | mV, measured under load |
| 9–10 | median distance | `uint16` | mm, `0` = not available |

**Material code (byte 0, bits 1–0):** `0` NON_METAL · `1` METAL · `2` UNKNOWN

## Alarm bits

| Bit | Mask | Name | Raised when | Priority |
|-----|------|------|-------------|:--------:|
| 0 | `0x0001` | `HIGH_FILL` | fill ≥ 90 % (clears below 85 %) | |
| 1 | `0x0002` | `CRITICAL_FILL` | fill ≥ 95 % (clears below 90 %), supersedes `HIGH_FILL` | ✔ |
| 2 | `0x0004` | `FAST_FILL` | fill rose ≥ 25 points since the last confident reading | |
| 3 | `0x0008` | `TILT` | LIS2DW12 tilt / 6D interrupt | ✔ |
| 4 | `0x0010` | `SHOCK` | LIS2DW12 wake-up (impact) interrupt | |
| 5 | `0x0020` | `LOW_BATTERY` | battery ≤ 3.30 V under load | |
| 6 | `0x0040` | `SENSOR_FAULT` | fewer than 3 valid ultrasonic samples | |
| 7 | `0x0080` | `TEMP_ANOMALY` | temperature ≥ 60 °C | ✔ |
| 8 | `0x0100` | `LOW_CONFIDENCE` | valid samples spread > 150 mm | |
| 9–15 | — | reserved | must be `0` | |

The thresholds above are the defaults from `fl_default_config()` and are expected to
change after field calibration.

## Example

```
11 57 00 01 01 1A 2D 0D FC 02 00
```

```json
{
  "version": 1,
  "priority": false,
  "fill_pct": 87,
  "material": "METAL",
  "alarms": ["HIGH_FILL"],
  "alarm_mask": 1,
  "temperature_c": 28.2,
  "humidity_pct": 45,
  "battery_v": 3.58,
  "distance_mm": 512
}
```

The same message as JSON text would be ~120 bytes. At 11 bytes, the full LoRaWAN
frame (24 bytes incl. 13 bytes of MAC overhead) stays well inside the
51-byte limit of the slowest EU868 data rate (DR0 / SF12).

## Versioning

The version nibble allows the format to evolve without breaking deployed
decoders: the decoder rejects unknown versions instead of misinterpreting them.
New fields should be appended and the version incremented.

## Decoder

[`decoder.js`](decoder.js) implements `decodeUplink(input)` and can be pasted
directly into The Things Stack or ChirpStack as a payload formatter.
