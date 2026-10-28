# Bill of Materials (concept stage)

Prices are indicative single-to-low-volume estimates collected at concept stage.
They must be re-quoted for a 2,000-unit build.

| # | Function | Part | Interface | Est. unit cost | Why this part |
|---|----------|------|-----------|---------------:|---------------|
| 1 | Fill level | DFRobot **A02YYUW** ultrasonic | UART | $15.9 | IP67, 3–450 cm range covers a ~2.5 m container, simple integration |
| 2 | Metal detection | TI **LDC0851** + PCB coil | GPIO | ~$1.5 | Metal / non-metal decision, 3.3 V, very low power |
| 3 | Temperature / humidity | Sensirion **SHT40** | I²C | ~$2.4 (with #4) | Compensation and heat anomaly detection |
| 4 | Tilt / shock | ST **LIS2DW12** accelerometer | I²C + INT | (incl. above) | Wakes the MCU via interrupt, no polling needed |
| 5 | MCU | ST **STM32U083KCU6** | — | ~$2.1 | Ultra-low-power Cortex-M0+, enough resources for LoRaWAN stack |
| 6 | Radio | Seeed **Wio-SX1262** (Semtech SX1262) | SPI | ~$4.9 | Ready-made EU868 RF module, cuts development time and RF design risk |
| 7 | Power | 3.6 V **Li-SOCl₂** cell + low-Iq regulator + buffer capacitor | ADC (monitor) | ~$18–20 | Very low self-discharge; capacitor supplies TX current peaks |
| 8 | Mechanics | 2-layer PCB, assembly, antenna, IP enclosure, connectors | — | ~$16–20 | |
| | **Total per field unit** | | | **~$61–67** | |

## Project-level estimate

| Item | Estimate |
|------|---------:|
| 2,000 field units | ~$122k–134k |
| 4–6 LoRaWAN gateways | ~$2k–4k |

## Cost reduction levers

- **Battery size.** The [energy budget](../tools/energy_budget.py) shows load
  consumption is in the same order as the cell's own self-discharge. A C-size
  cell (~8.5 Ah) still reaches ~10 years at the SF12 worst case, so the D-size
  cell is a candidate for downsizing after prototype current measurements.
- **Environmental sensors.** SHT40 can be dropped if heat/fire detection is not
  required; the MCU's internal temperature sensor gives a coarse fallback.
- **Volume pricing** for the ultrasonic sensor, the largest single item.
