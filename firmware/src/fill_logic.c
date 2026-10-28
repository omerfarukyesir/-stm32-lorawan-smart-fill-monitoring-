/**
 * @file    fill_logic.c
 * @brief   Hardware-independent decision logic for the smart fill-level node.
 *          See fill_logic.h and docs/en/technical-concept.md (section 3).
 */
#include "fill_logic.h"

#include <string.h>

/* ------------------------------------------------------------------------ */
/* Configuration                                                            */
/* ------------------------------------------------------------------------ */

void fl_default_config(fl_config_t *cfg)
{
    cfg->d_empty_mm                = 2600u; /* sensor mounted ~10 cm above rim */
    cfg->d_full_mm                 = 200u;
    cfg->min_valid_samples         = 3u;
    cfg->max_spread_mm             = 150u;
    cfg->high_fill_pct             = 90u;
    cfg->critical_fill_pct         = 95u;
    cfg->fill_hysteresis_pct       = 5u;
    cfg->fast_fill_delta_pct       = 25u;
    cfg->min_fill_for_material_pct = 5u;
    cfg->low_battery_mv            = 3300u;
    cfg->temp_alarm_c_x10          = 600;   /* 60.0 C: possible fire / heat */
}

void fl_state_init(fl_state_t *st)
{
    st->prev_fill_pct = FL_FILL_INVALID;
    st->latched_fill  = ALARM_NONE;
    st->prev_alarms   = ALARM_NONE;
}

/* ------------------------------------------------------------------------ */
/* Measurement filtering                                                    */
/* ------------------------------------------------------------------------ */

uint16_t fl_median_valid(const uint16_t *samples, uint8_t n,
                         uint8_t *n_valid, uint16_t *spread_mm)
{
    uint16_t v[FL_MAX_SAMPLES];
    uint8_t  k = 0;

    if (n > FL_MAX_SAMPLES) {
        n = FL_MAX_SAMPLES;
    }

    /* Keep only physically plausible samples. */
    for (uint8_t i = 0; i < n; i++) {
        if (samples[i] >= FL_SENSOR_MIN_MM && samples[i] <= FL_SENSOR_MAX_MM) {
            v[k++] = samples[i];
        }
    }

    if (n_valid)   { *n_valid = k; }
    if (spread_mm) { *spread_mm = 0; }
    if (k == 0)    { return 0; }

    /* Insertion sort: n <= 16, cheaper than qsort on a Cortex-M0+. */
    for (uint8_t i = 1; i < k; i++) {
        uint16_t x = v[i];
        int8_t   j = (int8_t)(i - 1);
        while (j >= 0 && v[j] > x) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }

    if (spread_mm) { *spread_mm = (uint16_t)(v[k - 1] - v[0]); }

    if (k & 1u) {
        return v[k / 2];
    }
    return (uint16_t)(((uint32_t)v[k / 2 - 1] + v[k / 2]) / 2u);
}

uint8_t fl_calc_fill(uint16_t distance_mm, const fl_config_t *cfg)
{
    if (cfg->d_empty_mm <= cfg->d_full_mm) {
        return FL_FILL_INVALID;              /* bad calibration */
    }
    if (distance_mm >= cfg->d_empty_mm) { return 0u; }
    if (distance_mm <= cfg->d_full_mm)  { return 100u; }

    uint32_t span = (uint32_t)cfg->d_empty_mm - cfg->d_full_mm;
    uint32_t used = (uint32_t)cfg->d_empty_mm - distance_mm;
    return (uint8_t)((used * 100u + span / 2u) / span); /* rounded */
}

/* ------------------------------------------------------------------------ */
/* Decision cycle                                                           */
/* ------------------------------------------------------------------------ */

/** Set/clear one latched alarm with hysteresis. */
static void latch(uint16_t *latched, uint16_t bit, uint8_t fill,
                  uint8_t threshold, uint8_t hyst)
{
    uint8_t clear_below = (threshold > hyst) ? (uint8_t)(threshold - hyst) : 0u;

    if (fill >= threshold) {
        *latched |= bit;
    } else if (fill < clear_below) {
        *latched &= (uint16_t)~bit;
    }
    /* In the hysteresis band the previous state is kept. */
}

void fl_evaluate(const sensor_snapshot_t *in, const fl_config_t *cfg,
                 fl_state_t *st, node_report_t *out)
{
    uint8_t  n_valid = 0;
    uint16_t spread  = 0;
    uint16_t alarms  = ALARM_NONE;

    memset(out, 0, sizeof(*out));

    /* 1. Distance -> fill ------------------------------------------------ */
    uint16_t d = fl_median_valid(in->samples_mm, in->n_samples, &n_valid, &spread);
    bool distance_ok = (n_valid >= cfg->min_valid_samples);
    bool confident   = distance_ok && (spread <= cfg->max_spread_mm);

    uint8_t fill = distance_ok ? fl_calc_fill(d, cfg) : FL_FILL_INVALID;
    if (fill == FL_FILL_INVALID) {
        distance_ok = false;
        confident   = false;
    }

    if (!distance_ok) {
        alarms |= ALARM_SENSOR_FAULT;
    } else if (!confident) {
        alarms |= ALARM_LOW_CONFIDENCE;
    }

    /* 2. Fill alarms ------------------------------------------------------
     * Only a confident reading may change the latched state, so a single
     * bad echo can neither raise nor clear an overflow alarm.            */
    if (confident) {
        latch(&st->latched_fill, ALARM_CRITICAL_FILL, fill,
              cfg->critical_fill_pct, cfg->fill_hysteresis_pct);
        latch(&st->latched_fill, ALARM_HIGH_FILL, fill,
              cfg->high_fill_pct, cfg->fill_hysteresis_pct);

        if (st->prev_fill_pct != FL_FILL_INVALID &&
            fill > st->prev_fill_pct &&
            (uint8_t)(fill - st->prev_fill_pct) >= cfg->fast_fill_delta_pct) {
            alarms |= ALARM_FAST_FILL;
        }
        st->prev_fill_pct = fill;
    }

    /* CRITICAL supersedes HIGH in the report. */
    if (st->latched_fill & ALARM_CRITICAL_FILL) {
        alarms |= ALARM_CRITICAL_FILL;
    } else if (st->latched_fill & ALARM_HIGH_FILL) {
        alarms |= ALARM_HIGH_FILL;
    }

    /* 3. Content class ------------------------------------------------- */
    material_t material;
    if (!in->metal_reading_ok || !distance_ok ||
        fill < cfg->min_fill_for_material_pct) {
        material = MATERIAL_UNKNOWN;   /* no reading, or nothing to classify */
    } else {
        material = in->metal_detected ? MATERIAL_METAL : MATERIAL_NON_METAL;
    }

    /* 4. Events and device health --------------------------------------- */
    if (in->tilt_irq)  { alarms |= ALARM_TILT; }
    if (in->shock_irq) { alarms |= ALARM_SHOCK; }
    if (in->battery_mv <= cfg->low_battery_mv) { alarms |= ALARM_LOW_BATTERY; }
    if (in->temp_c_x10 >= cfg->temp_alarm_c_x10) { alarms |= ALARM_TEMP_ANOMALY; }

    /* 5. Priority: only on the rising edge of a priority alarm ----------- */
    uint16_t newly_raised = (uint16_t)(alarms & (uint16_t)~st->prev_alarms);
    st->prev_alarms = alarms;

    out->fill_pct     = fill;
    out->distance_mm  = distance_ok ? d : 0u;
    out->material     = material;
    out->alarms       = alarms;
    out->priority     = (newly_raised & FL_PRIORITY_ALARMS) != 0u;
    out->temp_c_x10   = in->temp_c_x10;
    out->humidity_pct = in->humidity_pct;
    out->battery_mv   = in->battery_mv;
}

/* ------------------------------------------------------------------------ */
/* Payload encoding (big-endian, see payload/PAYLOAD_FORMAT.md)             */
/* ------------------------------------------------------------------------ */

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFu);
}

size_t fl_encode_payload(const node_report_t *r, uint8_t *buf, size_t buf_len)
{
    if (buf_len < FL_PAYLOAD_SIZE) {
        return 0;
    }

    buf[0] = (uint8_t)((FL_PAYLOAD_VERSION << 4) |
                       (r->priority ? 0x08u : 0x00u) |
                       ((uint8_t)r->material & 0x03u));
    buf[1] = r->fill_pct;
    put_u16(&buf[2], r->alarms);
    put_u16(&buf[4], (uint16_t)r->temp_c_x10);   /* two's complement */
    buf[6] = r->humidity_pct;
    put_u16(&buf[7], r->battery_mv);
    put_u16(&buf[9], r->distance_mm);

    return FL_PAYLOAD_SIZE;
}
