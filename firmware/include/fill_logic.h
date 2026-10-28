/**
 * @file    fill_logic.h
 * @brief   Hardware-independent decision logic for the smart fill-level node.
 *
 * Everything in this module is plain C99 with no hardware dependencies, so it
 * can be unit-tested on a PC and linked unchanged into the STM32 firmware.
 * Sensor drivers (A02YYUW, LDC0851, SHT40, LIS2DW12) only need to fill a
 * sensor_snapshot_t; this module turns it into a node_report_t and an encoded
 * LoRaWAN payload.
 */
#ifndef FILL_LOGIC_H
#define FILL_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Constants                                                                */
/* ------------------------------------------------------------------------ */

#define FL_PAYLOAD_VERSION  1u
#define FL_PAYLOAD_SIZE     11u   /**< Encoded uplink size in bytes          */
#define FL_MAX_SAMPLES      16u   /**< Max ultrasonic samples per cycle      */
#define FL_FILL_INVALID     0xFFu /**< Fill value when no valid distance     */

/** A02YYUW physical range (datasheet: 3-450 cm). */
#define FL_SENSOR_MIN_MM    30u
#define FL_SENSOR_MAX_MM    4500u

/* ------------------------------------------------------------------------ */
/* Types                                                                    */
/* ------------------------------------------------------------------------ */

typedef enum {
    MATERIAL_NON_METAL = 0,
    MATERIAL_METAL     = 1,
    MATERIAL_UNKNOWN   = 2
} material_t;

/** Alarm bitmask (sent as uint16 on air). */
enum {
    ALARM_NONE           = 0x0000u,
    ALARM_HIGH_FILL      = 0x0001u,
    ALARM_CRITICAL_FILL  = 0x0002u,
    ALARM_FAST_FILL      = 0x0004u,
    ALARM_TILT           = 0x0008u,
    ALARM_SHOCK          = 0x0010u,
    ALARM_LOW_BATTERY    = 0x0020u,
    ALARM_SENSOR_FAULT   = 0x0040u,
    ALARM_TEMP_ANOMALY   = 0x0080u,
    ALARM_LOW_CONFIDENCE = 0x0100u
};

/** Alarms that trigger an immediate, high-priority uplink. */
#define FL_PRIORITY_ALARMS (ALARM_CRITICAL_FILL | ALARM_TILT | ALARM_TEMP_ANOMALY)

/** Per-installation calibration and thresholds (stored in flash). */
typedef struct {
    uint16_t d_empty_mm;                /**< Sensor-to-floor, empty bin        */
    uint16_t d_full_mm;                 /**< Sensor-to-surface at 100 % fill   */
    uint8_t  min_valid_samples;         /**< Fewer valid -> SENSOR_FAULT       */
    uint16_t max_spread_mm;             /**< Larger spread -> LOW_CONFIDENCE   */
    uint8_t  high_fill_pct;             /**< e.g. 90                           */
    uint8_t  critical_fill_pct;         /**< e.g. 95                           */
    uint8_t  fill_hysteresis_pct;       /**< Alarm clears at threshold - hyst. */
    uint8_t  fast_fill_delta_pct;       /**< Rise per cycle -> FAST_FILL       */
    uint8_t  min_fill_for_material_pct; /**< Below this, content is UNKNOWN    */
    uint16_t low_battery_mv;            /**< e.g. 3300                         */
    int16_t  temp_alarm_c_x10;          /**< e.g. 600 = 60.0 C                 */
} fl_config_t;

/** Raw inputs gathered by the drivers in one measurement cycle. */
typedef struct {
    uint16_t samples_mm[FL_MAX_SAMPLES]; /**< 0 = no echo / read error    */
    uint8_t  n_samples;
    bool     metal_detected;             /**< LDC0851 comparator output    */
    bool     metal_reading_ok;           /**< false if the LDC read failed */
    bool     tilt_irq;                   /**< LIS2DW12 6D / tilt interrupt */
    bool     shock_irq;                  /**< LIS2DW12 wake-up interrupt   */
    int16_t  temp_c_x10;                 /**< SHT40, 0.1 C resolution      */
    uint8_t  humidity_pct;               /**< SHT40, %RH                   */
    uint16_t battery_mv;                 /**< ADC, measured under load     */
} sensor_snapshot_t;

/** State kept between cycles (RAM retained through STOP mode). */
typedef struct {
    uint8_t  prev_fill_pct;  /**< FL_FILL_INVALID if unknown           */
    uint16_t latched_fill;   /**< Latched fill alarms (for hysteresis) */
    uint16_t prev_alarms;    /**< Alarms reported in the last cycle    */
} fl_state_t;

/** Output of one decision cycle. */
typedef struct {
    uint8_t    fill_pct;     /**< 0-100, or FL_FILL_INVALID            */
    uint16_t   distance_mm;  /**< Median of valid samples, 0 if none   */
    material_t material;
    uint16_t   alarms;
    bool       priority;     /**< A priority alarm was newly raised:
                                  send now, outside the 30-min slot   */
    int16_t    temp_c_x10;
    uint8_t    humidity_pct;
    uint16_t   battery_mv;
} node_report_t;

/* ------------------------------------------------------------------------ */
/* API                                                                      */
/* ------------------------------------------------------------------------ */

/** Fills @p cfg with defaults for a container of roughly 2.5 m depth. */
void fl_default_config(fl_config_t *cfg);

/** Initialises the inter-cycle state. */
void fl_state_init(fl_state_t *st);

/**
 * Median of the samples that fall inside the sensor's physical range.
 * @param[out] n_valid   number of samples used (may be NULL)
 * @param[out] spread_mm max - min of the valid samples (may be NULL)
 * @return median in mm, or 0 if there are no valid samples.
 */
uint16_t fl_median_valid(const uint16_t *samples, uint8_t n,
                         uint8_t *n_valid, uint16_t *spread_mm);

/** Fill % = 100 * (D_empty - D) / (D_empty - D_full), clamped to 0..100. */
uint8_t fl_calc_fill(uint16_t distance_mm, const fl_config_t *cfg);

/** Runs one full decision cycle and updates @p st. */
void fl_evaluate(const sensor_snapshot_t *in, const fl_config_t *cfg,
                 fl_state_t *st, node_report_t *out);

/**
 * Encodes a report into the 11-byte uplink defined in
 * payload/PAYLOAD_FORMAT.md.
 * @return number of bytes written, or 0 if @p buf_len is too small.
 */
size_t fl_encode_payload(const node_report_t *r, uint8_t *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* FILL_LOGIC_H */
