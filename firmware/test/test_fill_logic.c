/**
 * @file    test_fill_logic.c
 * @brief   Host-side unit tests for fill_logic (no framework required).
 *          Build and run with:  make test
 */
#include "fill_logic.h"

#include <stdio.h>
#include <string.h>

static int g_run = 0, g_fail = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        g_run++;                                                          \
        if (!(cond)) {                                                    \
            g_fail++;                                                     \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                 \
    } while (0)

/* Snapshot with 5 identical samples at distance d and healthy device. */
static sensor_snapshot_t snap(uint16_t d)
{
    sensor_snapshot_t s;
    memset(&s, 0, sizeof(s));
    for (uint8_t i = 0; i < 5; i++) { s.samples_mm[i] = d; }
    s.n_samples        = 5;
    s.metal_reading_ok = true;
    s.temp_c_x10       = 282;   /* 28.2 C */
    s.humidity_pct     = 45;
    s.battery_mv       = 3580;
    return s;
}

/* Distance that gives the requested fill with the default config. */
static uint16_t d_for(const fl_config_t *c, uint8_t pct)
{
    return (uint16_t)(c->d_empty_mm -
                      (uint32_t)(c->d_empty_mm - c->d_full_mm) * pct / 100u);
}

static void test_median(void)
{
    puts("median_valid");
    uint16_t a[] = {1000, 0, 1010, 9999, 990, 1005, 20};
    uint8_t n; uint16_t spread;
    uint16_t m = fl_median_valid(a, 7, &n, &spread);
    CHECK(n == 4);                 /* 0, 9999 and 20 rejected */
    CHECK(m == 1002);              /* (1000 + 1005) / 2       */
    CHECK(spread == 20);

    uint16_t none[] = {0, 0, 5000};
    CHECK(fl_median_valid(none, 3, &n, NULL) == 0 && n == 0);
}

static void test_fill_calc(void)
{
    puts("calc_fill");
    fl_config_t c; fl_default_config(&c);
    CHECK(fl_calc_fill(c.d_empty_mm, &c) == 0);
    CHECK(fl_calc_fill(c.d_empty_mm + 100, &c) == 0);
    CHECK(fl_calc_fill(c.d_full_mm, &c) == 100);
    CHECK(fl_calc_fill(c.d_full_mm - 50, &c) == 100);
    CHECK(fl_calc_fill(1400, &c) == 50);

    c.d_full_mm = c.d_empty_mm;    /* broken calibration */
    CHECK(fl_calc_fill(1000, &c) == FL_FILL_INVALID);
}

static void test_fill_alarms_and_hysteresis(void)
{
    puts("fill alarms + hysteresis");
    fl_config_t c; fl_default_config(&c);
    fl_state_t st; fl_state_init(&st);
    node_report_t r;
    sensor_snapshot_t s;

    s = snap(d_for(&c, 80)); fl_evaluate(&s, &c, &st, &r);
    CHECK(r.alarms == ALARM_NONE);

    s = snap(d_for(&c, 91)); fl_evaluate(&s, &c, &st, &r);
    CHECK(r.alarms & ALARM_HIGH_FILL);
    CHECK(!r.priority);            /* HIGH is not a priority alarm */

    s = snap(d_for(&c, 96)); fl_evaluate(&s, &c, &st, &r);
    CHECK(r.alarms & ALARM_CRITICAL_FILL);
    CHECK(!(r.alarms & ALARM_HIGH_FILL));  /* CRITICAL supersedes HIGH */
    CHECK(r.priority);             /* newly raised -> send now */

    s = snap(d_for(&c, 97)); fl_evaluate(&s, &c, &st, &r);
    CHECK(r.alarms & ALARM_CRITICAL_FILL);
    CHECK(!r.priority);            /* still active, not newly raised */

    s = snap(d_for(&c, 92)); fl_evaluate(&s, &c, &st, &r);
    CHECK(r.alarms & ALARM_CRITICAL_FILL); /* inside hysteresis band */

    s = snap(d_for(&c, 88)); fl_evaluate(&s, &c, &st, &r);
    CHECK(!(r.alarms & ALARM_CRITICAL_FILL));
    CHECK(r.alarms & ALARM_HIGH_FILL);     /* 88 >= 90 - 5 */

    s = snap(d_for(&c, 10)); fl_evaluate(&s, &c, &st, &r);  /* emptied */
    CHECK(!(r.alarms & (ALARM_HIGH_FILL | ALARM_CRITICAL_FILL)));
}

static void test_single_bad_echo_does_not_alarm(void)
{
    puts("noisy reading cannot raise fill alarm");
    fl_config_t c; fl_default_config(&c);
    fl_state_t st; fl_state_init(&st);
    node_report_t r;

    sensor_snapshot_t s = snap(d_for(&c, 50));
    fl_evaluate(&s, &c, &st, &r);

    /* Widely scattered samples whose median looks "full". */
    s = snap(0);
    uint16_t noisy[] = {250, 260, 240, 900, 1600};
    memcpy(s.samples_mm, noisy, sizeof(noisy));
    fl_evaluate(&s, &c, &st, &r);
    CHECK(r.alarms & ALARM_LOW_CONFIDENCE);
    CHECK(!(r.alarms & (ALARM_HIGH_FILL | ALARM_CRITICAL_FILL | ALARM_FAST_FILL)));
    CHECK(st.prev_fill_pct == 50); /* baseline not overwritten */
}

static void test_sensor_fault(void)
{
    puts("sensor fault");
    fl_config_t c; fl_default_config(&c);
    fl_state_t st; fl_state_init(&st);
    node_report_t r;

    sensor_snapshot_t s = snap(0);           /* no echoes at all */
    fl_evaluate(&s, &c, &st, &r);
    CHECK(r.alarms & ALARM_SENSOR_FAULT);
    CHECK(r.fill_pct == FL_FILL_INVALID);
    CHECK(r.material == MATERIAL_UNKNOWN);
}

static void test_fast_fill(void)
{
    puts("fast fill");
    fl_config_t c; fl_default_config(&c);
    fl_state_t st; fl_state_init(&st);
    node_report_t r;

    sensor_snapshot_t s = snap(d_for(&c, 20)); fl_evaluate(&s, &c, &st, &r);
    CHECK(!(r.alarms & ALARM_FAST_FILL));    /* no baseline yet */
    s = snap(d_for(&c, 30)); fl_evaluate(&s, &c, &st, &r);
    CHECK(!(r.alarms & ALARM_FAST_FILL));
    s = snap(d_for(&c, 60)); fl_evaluate(&s, &c, &st, &r);
    CHECK(r.alarms & ALARM_FAST_FILL);
}

static void test_material(void)
{
    puts("material");
    fl_config_t c; fl_default_config(&c);
    fl_state_t st; fl_state_init(&st);
    node_report_t r;

    sensor_snapshot_t s = snap(d_for(&c, 40));
    s.metal_detected = true;  fl_evaluate(&s, &c, &st, &r);
    CHECK(r.material == MATERIAL_METAL);
    s.metal_detected = false; fl_evaluate(&s, &c, &st, &r);
    CHECK(r.material == MATERIAL_NON_METAL);
    s.metal_reading_ok = false; fl_evaluate(&s, &c, &st, &r);
    CHECK(r.material == MATERIAL_UNKNOWN);

    s = snap(c.d_empty_mm);                  /* empty container */
    fl_evaluate(&s, &c, &st, &r);
    CHECK(r.material == MATERIAL_UNKNOWN);
}

static void test_events_and_health(void)
{
    puts("events + health");
    fl_config_t c; fl_default_config(&c);
    fl_state_t st; fl_state_init(&st);
    node_report_t r;

    sensor_snapshot_t s = snap(d_for(&c, 40));
    s.tilt_irq = true; s.shock_irq = true;
    s.battery_mv = 3250; s.temp_c_x10 = 655;
    fl_evaluate(&s, &c, &st, &r);
    CHECK(r.alarms & ALARM_TILT);
    CHECK(r.alarms & ALARM_SHOCK);
    CHECK(r.alarms & ALARM_LOW_BATTERY);
    CHECK(r.alarms & ALARM_TEMP_ANOMALY);
    CHECK(r.priority);
}

static void test_payload(void)
{
    puts("payload encoding");
    node_report_t r;
    memset(&r, 0, sizeof(r));
    r.priority = false; r.material = MATERIAL_METAL; r.fill_pct = 87;
    r.alarms = ALARM_HIGH_FILL; r.temp_c_x10 = 282; r.humidity_pct = 45;
    r.battery_mv = 3580; r.distance_mm = 512;

    uint8_t b[FL_PAYLOAD_SIZE];
    CHECK(fl_encode_payload(&r, b, sizeof(b)) == FL_PAYLOAD_SIZE);
    const uint8_t expect[] = {0x11, 0x57, 0x00, 0x01, 0x01, 0x1A,
                              0x2D, 0x0D, 0xFC, 0x02, 0x00};
    CHECK(memcmp(b, expect, sizeof(expect)) == 0);

    r.temp_c_x10 = -125; r.priority = true;        /* -12.5 C */
    fl_encode_payload(&r, b, sizeof(b));
    CHECK(b[0] == 0x19);
    CHECK(b[4] == 0xFF && b[5] == 0x83);

    CHECK(fl_encode_payload(&r, b, 4) == 0);       /* buffer too small */
}

int main(void)
{
    test_median();
    test_fill_calc();
    test_fill_alarms_and_hysteresis();
    test_single_bad_echo_does_not_alarm();
    test_sensor_fault();
    test_fast_fill();
    test_material();
    test_events_and_health();
    test_payload();

    printf("\n%d checks, %d failed\n", g_run, g_fail);
    return g_fail ? 1 : 0;
}
