# Architecture — ESP32 + ICC-1-A Thread / Matter Gateway

Independent project from `esp32-icc1-zigbee-gw`. Same *product shape* (SoftAP portal, device inventory UX, dual-board radio), different stack: **OpenThread + Matter** instead of **Zigbee EZSP + HomeKit**.

## Confirmed vs experimental

| Claim | Status |
| --- | --- |
| ICC-1 / ICC-A-1 = **EFR32MG1P…F256** (256 KB flash, 32 KB RAM) | **Confirmed** (community + this family’s Zigbee docs) |
| OpenThread **RCP** fits MG1 F256 (~100 KB flash / ~16 KB RAM used on similar Lidl MG1B builds) | **Confirmed for MG1 class**; ICC-A-1 pinout must match UART in the RCP image |
| Multi-PAN Zigbee+Thread on one MG1 | **Possible** (Silabs CPC / multi-PAN) but **out of scope** here — this gateway is Thread-only on its ICC |
| ESP32-S3 hosts OTBR + Matter + portal | **Feasible with PSRAM**; C3 **not recommended** |
| Standalone Matter commissioning | **Implemented** with ESP-Matter controller/commissioner; local development fabric |

## Target topology

```
Phone / Matter controller / browser
        │
   Wi‑Fi (portal :80, Matter/CHIP when linked)
        │
   ESP32-S3 application
   (Wi‑Fi · SoftAP · portal · OpenThread host · Matter bridge)
        │
      Spinel / HDLC → UART (default 460800 8N1)
        │
   ICC-1-A / EFR32MG1  OpenThread RCP
        │
     Thread mesh (IEEE 802.15.4)
```

## Component map

| Component | Role |
| --- | --- |
| `icc_uart` | UART driver to ICC |
| ESP-IDF `openthread` | OpenThread host and OTBR over the ICC UART RCP |
| ESP-Matter controller | Local fabric, BLE commissioner, interaction commands |
| `main/gateway_nodes` | NVS sensor registry and contact/motion subscriptions |
| Legacy components | Previous portal/scaffold retained but not started by the MVP |

## Incremental roadmap

1. **Done in 0.1.0:** UART probe, SoftAP/STA portal, and ICC flash guide.
2. **Done in 0.2.0:** ESP-IDF OTBR, ESP-Matter commissioner, persistent sensor registry, and contact/motion subscriptions.
3. **Next:** Flash and validate the ICC OpenThread RCP, commission the physical IKEA sensors, then expose pairing and live state in the web portal.

## Hardware notes (ICC UART)

IKEA ICC pads for **data UART** (same as Zigbee twin):

| ESP32 | ICC pad | EFR32 |
| --- | --- | --- |
| TX | 2 | **PB15** (RCP RX) |
| RX | 3 | **PB14** (RCP TX) |
| 3V3 | 11 | VDD |
| GND | 12 | GND |

**Critical:** OT-RCP images must be built for **PB14/PB15**. Lidl MG1 examples often use **PA0/PA1** — those will **not** talk on pads 2/3 until rebuilt for ICC pinmux.

Default baud **460800**. Zigbee NCP images use **115200** — do not mix.

## Licensing

Use Silicon Labs SDK / OpenThread (`BSD`/`Apache-2.0` as applicable), ESP-IDF, ESP-Matter. Do **not** copy proprietary IKEA application firmware or closed HomeKit stacks into this tree.
