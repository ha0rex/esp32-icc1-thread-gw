# ESP32 + IKEA ICC-1-A Thread → Matter Gateway

ESP-IDF firmware for an **ESP32** host that drives an **IKEA ICC-1 / ICC-A-1** OpenThread **RCP** (Silicon Labs EFR32MG1) and exposes a **Matter-ready** Thread border / bridge product experience.

This repository is **independent** of [`esp32-icc1-zigbee-gw`](https://github.com/ha0rex/esp32-icc1-zigbee-gw) (Zigbee + HomeKit). Same UX goals; **Thread + Matter** instead of Zigbee + HomeKit. Use a **separate ESP32** and a **separate ICC** module.

```
Phone / Matter controller / browser
        │
   Wi‑Fi (portal :80)
        │
   ESP32 application (Wi‑Fi + portal + OT host + Matter scaffold)
        │
      Spinel → HDLC → UART 460800 8N1
        │
   ICC-1-A / EFR32MG1 OT-RCP
        │
     Thread mesh
```

**Current firmware version:** `0.2.0` (see `PROJECT_VER` in `CMakeLists.txt`)

---

## Features (v0.2.0)

| Area | Status |
| --- | --- |
| Wi-Fi station setup from serial console | **Implemented** |
| ICC UART + OpenThread Border Router | **Implemented** (needs OT-RCP on ICC) |
| Matter controller / BLE-Thread commissioner | **Implemented** |
| Contact and occupancy subscriptions | **Implemented** |
| Persistent sensor registry | **Implemented** |
| Web portal | **Legacy UI; not started by the controller MVP** |

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

Configure Wi-Fi and commission sensors from the serial Matter shell. See
**[docs/MATTER.md](docs/MATTER.md)** for the complete command sequence.

---

## ICC RCP firmware

1. Build **ot-rcp** for **EFR32MG1** with UART on **PB14/PB15 @ 460800** (Simplicity Studio or ot-efr32).  
2. Flash via your **ESP32 CMSIS-DAP over Wi‑Fi** + OpenOCD — full steps in **[docs/icc-ot-rcp-flash.md](docs/icc-ot-rcp-flash.md)**.  
3. Helper: `tools/flash-icc-openocd.sh path/to/ot-rcp.s37`

---

## Portal API (summary)

| Method | Path | Purpose |
| --- | --- | --- |
| GET | `/api/status` | Board, RCP, Thread, Wi‑Fi, Matter, heap |
| POST | `/api/thread/form` | `{"channel":15}` form intent |
| POST | `/api/thread/leave` | Leave intent |
| POST | `/api/wifi/sta` | `{"ssid","password","slot"}` |
| GET | `/api/wifi/scan` | AP list JSON |

---

## Roadmap

1. Wire ESP-IDF OpenThread to `icc_uart` Spinel.  
2. OTBR / biphasic IPv6 as needed for your LAN.  
3. ESP-Matter bridge + dynamic endpoints.  
4. Device UX parity (expose, rename, groups).

---

## License / provenance

Application code in this tree is written for this project. Third-party SDKs (ESP-IDF, OpenThread, Silicon Labs, ESP-Matter) remain under their own licenses. Do not copy proprietary IKEA or closed HomeKit sources here.
