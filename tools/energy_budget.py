#!/usr/bin/env python3
"""
Energy budget and LoRa airtime estimator for the smart fill-level node.

All current/duration values below are ASSUMPTIONS based on datasheet typicals
and must be replaced with measurements from the prototype (see docs).

Usage:
    python3 tools/energy_budget.py            # default: SF9, 30-min reporting
    python3 tools/energy_budget.py --sf 12    # worst-case coverage edge
"""
import argparse
import math

LORAWAN_OVERHEAD = 13  # MHDR(1) + FHDR(7) + FPort(1) + MIC(4)
PAYLOAD_BYTES = 11


def lora_airtime_s(phy_len, sf, bw_hz=125_000, cr=1, preamble=8,
                   explicit_header=True, crc=True):
    """Semtech AN1200.13 time-on-air formula."""
    t_sym = (2 ** sf) / bw_hz
    de = 1 if (sf >= 11 and bw_hz == 125_000) else 0  # low data rate optimise
    ih = 0 if explicit_header else 1
    num = 8 * phy_len - 4 * sf + 28 + 16 * int(crc) - 20 * ih
    n_payload = 8 + max(math.ceil(num / (4 * (sf - 2 * de))) * (cr + 4), 0)
    t_preamble = (preamble + 4.25) * t_sym
    return t_preamble + n_payload * t_sym


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sf", type=int, default=9, help="spreading factor 7-12")
    ap.add_argument("--period-min", type=float, default=30.0)
    ap.add_argument("--battery-mah", type=float, default=17_000,
                    help="nominal capacity (default: D-size Li-SOCl2)")
    ap.add_argument("--derating", type=float, default=0.70,
                    help="usable fraction after temperature, passivation "
                         "and end-of-life cut-off margin")
    ap.add_argument("--self-discharge", type=float, default=0.01,
                    help="fraction of nominal capacity lost per year")
    a = ap.parse_args()

    cycles_per_day = 24 * 60 / a.period_min
    toa = lora_airtime_s(PAYLOAD_BYTES + LORAWAN_OVERHEAD, a.sf)

    # (name, current mA, duration s per cycle) -- assumptions
    events = [
        ("MCU active (measure + process)",  1.5, 1.5),
        ("A02YYUW ultrasonic, 5 samples",   8.0, 1.2),
        ("SHT40 measurement",               0.4, 0.01),
        ("LDC0851 metal check",             2.0, 0.05),
        ("SX1262 TX @ +14 dBm",            45.0, toa),
        ("SX1262 RX1/RX2 windows",          6.0, 0.20),
    ]
    sleep_ua = 10.0  # MCU STOP + LIS2DW12 low-power + regulator Iq + leakage

    active_mas = sum(i * t for _, i, t in events)       # mA*s per cycle
    active_s = sum(t for _, _, t in events)
    sleep_mas = sleep_ua / 1000 * (a.period_min * 60 - active_s)
    load_mah_day = (active_mas + sleep_mas) * cycles_per_day / 3600
    self_dis_mah_day = a.battery_mah * a.self_discharge / 365
    mah_per_day = load_mah_day + self_dis_mah_day
    usable = a.battery_mah * a.derating
    years = usable / mah_per_day / 365

    duty = toa * cycles_per_day / 86_400 * 100

    print(f"SF{a.sf}/125 kHz, {PAYLOAD_BYTES}-byte payload "
          f"({PAYLOAD_BYTES + LORAWAN_OVERHEAD} B PHY)")
    print(f"  time on air      : {toa * 1000:7.1f} ms")
    print(f"  duty cycle used  : {duty:7.3f} %   (EU868 limit: 1 %)")
    print("\nPer-cycle charge")
    for name, i, t in events:
        print(f"  {name:34s} {i * t:8.2f} mA*s")
    print(f"  {'sleep (' + str(sleep_ua) + ' uA)':34s} {sleep_mas:8.2f} mA*s")
    print(f"\n  load consumption  : {load_mah_day:6.2f} mAh/day "
          f"(avg {load_mah_day / 24 * 1000:.1f} uA)")
    print(f"  self-discharge    : {self_dis_mah_day:6.2f} mAh/day "
          f"({a.self_discharge:.0%}/year)")
    print(f"  usable capacity   : {usable:6.0f} mAh "
          f"({a.battery_mah:.0f} mAh x {a.derating:.0%})")
    print(f"  estimated life    : {years:6.1f} years")
    if years > 10:
        print("  note: beyond ~10 years, cell ageing, passivation and enclosure wear "
              "dominate; treat the result as >10 years.")


if __name__ == "__main__":
    main()
