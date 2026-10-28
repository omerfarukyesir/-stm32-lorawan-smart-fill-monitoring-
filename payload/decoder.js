/**
 * LoRaWAN payload decoder for the smart fill-level node (payload v1).
 *
 * Compatible with The Things Stack (v3) and ChirpStack (v4) JavaScript
 * codecs: both call decodeUplink({ bytes, fPort }).
 * Format specification: PAYLOAD_FORMAT.md
 */

var MATERIALS = ["NON_METAL", "METAL", "UNKNOWN"];

var ALARM_BITS = [
  [0x0001, "HIGH_FILL"],
  [0x0002, "CRITICAL_FILL"],
  [0x0004, "FAST_FILL"],
  [0x0008, "TILT"],
  [0x0010, "SHOCK"],
  [0x0020, "LOW_BATTERY"],
  [0x0040, "SENSOR_FAULT"],
  [0x0080, "TEMP_ANOMALY"],
  [0x0100, "LOW_CONFIDENCE"]
];

function u16(b, i) { return (b[i] << 8) | b[i + 1]; }
function s16(b, i) { var v = u16(b, i); return v & 0x8000 ? v - 0x10000 : v; }

function decodeUplink(input) {
  var b = input.bytes;
  var warnings = [];

  if (!b || b.length < 11) {
    return { errors: ["payload too short: expected 11 bytes, got " + (b ? b.length : 0)] };
  }

  var version = b[0] >> 4;
  if (version !== 1) {
    return { errors: ["unsupported payload version " + version] };
  }

  var materialCode = b[0] & 0x03;
  var alarmMask = u16(b, 2);
  var alarms = [];
  for (var i = 0; i < ALARM_BITS.length; i++) {
    if (alarmMask & ALARM_BITS[i][0]) alarms.push(ALARM_BITS[i][1]);
  }
  if (alarmMask & 0xFE00) warnings.push("unknown alarm bits set: 0x" + alarmMask.toString(16));

  var fill = b[1] === 0xFF ? null : b[1];
  if (fill === null) warnings.push("fill level unavailable (sensor fault)");

  return {
    data: {
      version: version,
      priority: (b[0] & 0x08) !== 0,
      fill_pct: fill,
      material: MATERIALS[materialCode] || "UNKNOWN",
      alarms: alarms,
      alarm_mask: alarmMask,
      temperature_c: s16(b, 4) / 10,
      humidity_pct: b[6],
      battery_v: u16(b, 7) / 1000,
      distance_mm: u16(b, 9) || null
    },
    warnings: warnings
  };
}

/* Allow `require()` in Node.js tests; ignored by network-server runtimes. */
if (typeof module !== "undefined" && module.exports) {
  module.exports = { decodeUplink: decodeUplink };
}
