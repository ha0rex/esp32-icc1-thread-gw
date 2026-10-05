# ESP32 + IKEA ICC-1-A Thread → Matter Gateway

ESP-IDF firmware for an **ESP32** host that drives an **IKEA ICC-1 / ICC-A-1** OpenThread **RCP** (Silicon Labs EFR32MG1) and exposes a **Matter-ready** Thread border / bridge product experience.

This repository is **independent** of [`esp32-icc1-zigbee-gw`](https://github.com/ha0rex/esp32-icc1-zigbee-gw) (Zigbee + HomeKit). Same UX goals; **Thread + Matter** instead of Zigbee + HomeKit. Use a **separate ESP32** and a **separate ICC** module.

```
Phone (Home) / browser
        │
   Wi‑Fi  ·  portal :80  ·  HomeKit
        │
   ESP32 (Matter controller + OpenThread border router)
        │
      Spinel → HDLC → UART 460800 8N1
        │
   ICC-1-A / EFR32MG1 OT-RCP
        │
     Thread sensors
```

**Current firmware version:** `0.2.0` (see `PROJECT_VER` in `CMakeLists.txt`)

---

## Features (v0.2.0)

| Area | Status |
| --- | --- |
| Wi-Fi station setup from the portal or serial console | **Implemented** |
| ICC UART + OpenThread Border Router | **Implemented**. If the first start lands while the RCP is still resetting, it retries until Thread is attached. |
| Matter controller | **Implemented** |
| Subscriptions for paired sensors (contact, motion, buttons, and others) | **Implemented** |
| Persistent sensor registry | **Implemented** |
| Web portal | **Implemented**. Each device shows Thread RSSI and a Wi-Fi-style signal icon. |
| HomeKit bridge | **Implemented**. Paired sensors appear in Home and update while the app is open. |

Architecture decisions: **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)**  
ICC flashing (CMSIS-DAP over Wi‑Fi): **[docs/icc-ot-rcp-flash.md](docs/icc-ot-rcp-flash.md)**

---

## Hardware

### Supported ESP32 boards

| Status | Target | Notes |
| --- | --- | --- |
| **Recommended** | **Seeed XIAO ESP32-S3 (Plus)** | PSRAM for Matter later; UART **GPIO2 TX / GPIO4 RX** |
| Optional | ESP32-C3 | Builds may work; **not recommended** for Matter DRAM |

### ICC-1-A wiring (runtime UART)

| ESP32 | ICC-1-A |
| --- | --- |
| **UART TX** | pad **2** / **PB15** / RX |
| **UART RX** | pad **3** / **PB14** / TX |
| **3.3V** | pad **11** / VDD |
| **GND** | pad **12** / GND |

- UART: **460800 8N1**, no RTS/CTS by default  
- **Never power the ICC from 5V**  
- Flash **OpenThread RCP** for PB14/PB15 — not Zigbee NCP  

SWD pads for programming: see [docs/icc-ot-rcp-flash.md](docs/icc-ot-rcp-flash.md).

---

## Build & flash (ESP32 host)

```bash
cd ~/esp32-icc1-thread-gw
./scripts/bootstrap.sh
source scripts/export-sdk.sh
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

Pair sensors from the portal with a Matter setup code, or from the serial
Matter shell. See **[docs/MATTER.md](docs/MATTER.md)** for the command sequence.

Flashing only the app at `0x30000` keeps Wi-Fi, the Thread dataset, and paired
sensors. A full `idf.py flash` rewrites the bootloader and partition table as
well.

---

## ICC RCP firmware

1. Build **ot-rcp** for **EFR32MG1** with UART on **PB14/PB15 @ 460800** (Simplicity Studio or ot-efr32).  
2. Flash via your **ESP32 CMSIS-DAP over Wi‑Fi** + OpenOCD — full steps in **[docs/icc-ot-rcp-flash.md](docs/icc-ot-rcp-flash.md)**.  
3. Helper: `tools/flash-icc-openocd.sh path/to/ot-rcp.s37`

---

## Portal API (summary)

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/` | Portal |
| GET | `/api/status` | Board, Thread, Wi-Fi, HomeKit, and paired devices |
| POST | `/api/nodes/pair` | Pair a Matter setup code onto this fabric |
| POST | `/api/nodes/update` | Rename a device or change its type |
| POST | `/api/nodes/forget` | Remove a device from the gateway and Home |
| POST | `/api/nodes/read` | Read a device now |
| GET | `/api/matter/sniff` | Recent Matter traffic |
| POST | `/api/matter/sniff/clear` | Clear the sniffer log |
| POST | `/api/wifi/sta` | `{"ssid","password"}` |
| GET | `/api/wifi/scan` | AP list JSON |
| POST | `/api/homekit/reset` | Forget the Home pairing and restart the bridge |

Each device in `/api/status` includes `rssi`: the Thread child's last RSSI in
dBm, or `null` until that device has a secure session. The icon uses four
arcs at −50 dBm or better, then three, two, and one as the signal falls
through −65, −78, and −90 dBm.

---

## Roadmap

The border router, Matter controller, portal, and HomeKit bridge are in this
tree. Still open: grouping, and more device types as new sensors are paired.

---

## License / provenance

Application code in this tree is written for this project. Third-party SDKs (ESP-IDF, OpenThread, Silicon Labs, ESP-Matter) remain under their own licenses. Do not copy proprietary IKEA or closed HomeKit sources here.
