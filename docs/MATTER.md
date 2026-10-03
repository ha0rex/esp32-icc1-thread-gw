# Matter integration

## Current state (v0.1.0)

`matter_bridge` exposes **portal-facing** commissioning fields (PIN / discriminator placeholders) so the UX matches a Matter gateway. It does **not** yet link the CHIP stack.

## Recommended path: ESP-Matter

1. Install [ESP-Matter](https://github.com/espressif/esp-matter) beside ESP-IDF (follow Espressif’s version matrix).
2. Export `ESP_MATTER_PATH`.
3. Add Matter as a dependency from this project’s `main` / `matter_bridge` (see Espressif `light` / `bridge` examples).
4. Keep **OpenThread** radio on the **external ICC RCP** (`OT_RADIO_SPINEL_UART` / custom port using `icc_uart`), not an on-chip 802.15.4 (S3 has none).
5. Commission with Apple Home / Google Home / chip-tool using the real QR / manual code from CHIP.

## Feature parity goals (vs Zigbee HomeKit twin)

| Zigbee / HomeKit gateway | Matter / Thread gateway |
| --- | --- |
| SoftAP + STA portal | Same (`wifi_manager` + `web`) |
| Device inventory / rename / expose | Matter dynamic endpoints + NVS labels |
| Groups / thermostats | Matter Bridged device types + Aggregator |
| HomeKit setup code | Matter discriminator + passcode / QR |
| Zigbee sniffer | Thread energy scan / neighbor table (OT APIs) |

## Standards

Use CSA Matter / CHIP open source and ESP-Matter. Do not vendor proprietary Matter controller binaries.
