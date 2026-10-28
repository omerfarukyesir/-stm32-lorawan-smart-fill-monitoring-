/* Run with:  node payload/test/decoder.test.js   (no dependencies) */
const assert = require("assert");
const { decodeUplink } = require("../decoder.js");

const hex = (s) => Array.from(Buffer.from(s.replace(/\s/g, ""), "hex"));
let n = 0;
const t = (name, fn) => { fn(); n++; console.log("ok  " + name); };

// Same vector as firmware/test/test_fill_logic.c -> test_payload()
t("normal report", () => {
  const r = decodeUplink({ bytes: hex("11 57 0001 011A 2D 0DFC 0200"), fPort: 1 });
  assert.deepStrictEqual(r.data, {
    version: 1, priority: false, fill_pct: 87, material: "METAL",
    alarms: ["HIGH_FILL"], alarm_mask: 1, temperature_c: 28.2,
    humidity_pct: 45, battery_v: 3.58, distance_mm: 512
  });
  assert.deepStrictEqual(r.warnings, []);
});

t("negative temperature + priority flag", () => {
  const r = decodeUplink({ bytes: hex("19 57 0001 FF83 2D 0DFC 0200") });
  assert.strictEqual(r.data.temperature_c, -12.5);
  assert.strictEqual(r.data.priority, true);
});

t("sensor fault", () => {
  const r = decodeUplink({ bytes: hex("12 FF 0040 011A 2D 0DFC 0000") });
  assert.strictEqual(r.data.fill_pct, null);
  assert.strictEqual(r.data.distance_mm, null);
  assert.strictEqual(r.data.material, "UNKNOWN");
  assert.deepStrictEqual(r.data.alarms, ["SENSOR_FAULT"]);
  assert.strictEqual(r.warnings.length, 1);
});

t("multiple alarms", () => {
  const r = decodeUplink({ bytes: hex("18 30 00B8 028F 20 0CB2 06A4") });
  assert.deepStrictEqual(r.data.alarms, ["TILT", "SHOCK", "LOW_BATTERY", "TEMP_ANOMALY"]);
});

t("rejects short payload", () => {
  assert.ok(decodeUplink({ bytes: [0x11, 0x57] }).errors);
});

t("rejects unknown version", () => {
  assert.ok(decodeUplink({ bytes: hex("21 57 0001 011A 2D 0DFC 0200") }).errors);
});

console.log(`\n${n} tests passed`);
