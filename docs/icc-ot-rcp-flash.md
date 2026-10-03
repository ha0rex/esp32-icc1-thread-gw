# Flashing OpenThread RCP on IKEA ICC-1 / ICC-A-1 (via ESP32 CMSIS-DAP over Wi‑Fi)

This gateway’s radio is an **IKEA ICC-1 / ICC-A-1** module (**EFR32MG1P132F256** class: **256 KB flash / 32 KB RAM**). Stock IKEA firmware is Zigbee end-device/router firmware — **not** an OpenThread RCP.

You already have an **ESP32-based CMSIS-DAP over Wi‑Fi** probe that exposes SWD to OpenOCD over TCP. This guide covers building / obtaining **OT-RCP**, wiring SWD, flashing with OpenOCD, and verifying Spinel over UART.

> **Never apply 5 V to the ICC. Use 3.3 V only.**

---

## 1. Hardware compatibility (memory)

| Item | Value | Notes |
| --- | --- | --- |
| SoC | EFR32MG1P…F256 | Series 1 Mighty Gecko |
| Flash | 256 KB | OT-RCP on similar MG1B builds ≈ **100 KB** app — **fits** |
| RAM | 32 KB | OT-RCP ≈ **16 KB** — **fits** (tight; avoid bloated RCP configs) |
| Concurrent Zigbee+Thread on same ICC | Possible via Silabs multi-PAN/CPC on MG1 | **Not used by this project** (Thread-only RCP) |
| Zigbee NCP + OT RCP combo | Silabs recommends **>64 KB RAM** | Prefer pure **ot-rcp** on ICC-A-1 |

**Confirmed:** MG1-class can run OT-RCP.  
**Experiment required:** your exact ICC-A-1 marking + pinmux build for **PB14/PB15**.

---

## 2. Required firmware (source)

Prefer **official / open** sources:

| Source | What | License |
| --- | --- | --- |
| [Silicon Labs Simplicity Studio / SiSDK](https://www.silabs.com/developers/simplicity-studio) → **ot-rcp** example | Production-oriented RCP | Silabs SDK EULA + OpenThread |
| [openthread/ot-efr32](https://github.com/openthread/ot-efr32) | Upstream OT platform port | BSD-3 / Apache-2.0 (OpenThread) |
| Community MG1 OT-RCP (e.g. Lidl MG1B docs) | Proof RCP fits F256 | Use only as reference; **rebuild for ICC UART pins** |

### Must-match configuration for this gateway

| Setting | Required value |
| --- | --- |
| App | **OpenThread RCP** (`ot-rcp`), Spinel over UART |
| UART pins | **PB14 = TX (to ESP RX)**, **PB15 = RX (from ESP TX)** |
| Baud | **460800** 8N1 (match `CONFIG_ICC_UART_BAUD`) |
| Flow control | **None** for XIAO wiring (no RTS/CTS on pads 2/3). If your RCP defaults to HW flow, disable it or wire RTS/CTS. |
| HDLC | Standard Spinel HDLC framing |

Do **not** flash EmberZNet Zigbee NCP images from the Zigbee gateway project — wrong protocol (EZSP/ASH vs Spinel).

---

## 3. Build instructions (Simplicity Studio)

1. Install **Simplicity Studio 5** + **Simplicity SDK** with OpenThread.
2. File → New → Silicon Labs Project → filter **`ot-rcp`**.
3. Target board: custom / **EFR32MG1P132F256GM32** (or the exact part Commander reports).
4. In **Software Components**:
   - OpenThread → **Stack (RCP)**
   - IO Stream / USART on **PB14/PB15**, baud **460800**, **no** LF→CRLF conversion
   - Prefer **uartdrv** / low-level USART (binary Spinel safe)
5. Build → produce `.s37` / `.hex` / `.gbl`.
6. Copy artifact to this machine, e.g. `~/esp32-icc1-thread-gw/firmware/ot-rcp-icc-pb14-pb15-460800.s37` (gitignored).

### Alternate: ot-efr32

Follow [ot-efr32 README](https://github.com/openthread/ot-efr32). You must still force UART pinmux to ICC pads; stock WSTK boards differ.

---

## 4. ESP32 ↔ ICC SWD wiring (CMSIS-DAP)

Module pad map (same family as Zigbee twin):

| Pad | EFR32 | Role |
| --- | --- | --- |
| **6** | **PF0** | **SWCLK** |
| **7** | **PF1** | **SWDIO** |
| **8** | **PF2** | SWO (optional) |
| **10** | **RESETn** | Reset (recommended) |
| **11** | **VDD** | **3.3 V** |
| **12** | **GND** | GND |

Wire your Wi‑Fi CMSIS-DAP ESP32:

| CMSIS-DAP ESP32 | ICC |
| --- | --- |
| 3V3 (or shared rail) | pad **11** |
| GND | pad **12** |
| SWCLK | pad **6** |
| SWDIO | pad **7** |
| nRESET (if available) | pad **10** |

Power the ICC from a stable **3.3 V** supply (probe or bench). Module must **not** be powered from a bulb / mains driver while flashing.

**Runtime data UART** (gateway ESP32, different board from the probe) uses pads **2/3** — see [ARCHITECTURE.md](ARCHITECTURE.md).

---

## 5. OpenOCD — remote CMSIS-DAP over TCP

Your probe should already advertise a CMSIS-DAP (or raw SWD) endpoint. Typical patterns:

### A) Probe runs OpenOCD and listens on TCP (common)

On the machine that runs OpenOCD against the Wi‑Fi DAP:

```bash
# Example: CMSIS-DAP via WLAN — adjust interface script to your probe firmware docs
openocd \
  -f interface/cmsis-dap.cfg \
  -c "cmsis_dap_vid_pid 0x0000 0x0000" \
  -f target/efm32.cfg \
  -c "adapter speed 1000" \
  -c "transport select swd"
```

Many Wi‑Fi DAP firmwares instead expose **CMSIS-DAP v2 over TCP** or a **remote bitbang**. Use the OpenOCD config shipped with *your* probe project. Conceptually you need:

- `transport select swd`
- Target **EFR32MG1** / `efm32` family
- Adapter speed **≤ 2000 kHz** for long jumper wires (start at **1000**)

### B) Local OpenOCD attaches to remote GDB/SWD bridge

```tcl
# Example remote Tcl — replace HOST:PORT with your probe
adapter driver remote_bitbang
remote_bitbang host 192.168.x.x
remote_bitbang port 3335
transport select swd
source [find target/efm32.cfg]
adapter speed 1000
```

Save as `tools/openocd-icc-remote.cfg` and run:

```bash
openocd -f tools/openocd-icc-remote.cfg
```

---

## 6. Flash and verify

With OpenOCD running and the ICC halted:

```bash
# In another terminal — adjust .s37 path
arm-none-eabi-gdb -nx --batch \
  -ex "target extended-remote localhost:3333" \
  -ex "monitor halt" \
  -ex "monitor flash write_image erase firmware/ot-rcp-icc-pb14-pb15-460800.s37" \
  -ex "monitor reset run" \
  -ex "quit"
```

Or OpenOCD Tcl:

```tcl
halt
flash write_image erase firmware/ot-rcp-icc-pb14-pb15-460800.s37
reset run
```

### Simplicity Commander (USB J-Link alternative)

If you temporarily use a wired J-Link instead of Wi‑Fi DAP:

```bash
commander device info
commander flash ot-rcp-icc-pb14-pb15-460800.s37
commander device reset
```

Expect **EFR32MG1** / **F256** in `device info`.

### Verify RCP over UART

1. Wire gateway ESP32 UART to ICC pads 2/3 @ **460800**.
2. Flash/run this repo’s firmware on the ESP32.
3. Serial log should show `spinel: RCP OK version='…'` within a few seconds.
4. Portal **Overview → Thread radio** should read **CONNECTED**.

If you see `NO RCP` / `no_response`: wrong baud, TX/RX crossed, still Zigbee NCP image, or UART on wrong pins in the RCP build.

---

## 7. UART vs SPI between ESP32 and ICC

| Link | This project |
| --- | --- |
| **UART Spinel/HDLC** | **Yes** — default, pads PB14/PB15 |
| SPI Spinel | Supported by Silabs RCP examples; **not wired** on stock ICC padout used here |
| EZSP/ASH (Zigbee) | Used by the *other* gateway — **do not** use here |

---

## 8. Recovery if flashing fails

| Symptom | Action |
| --- | --- |
| OpenOCD can’t see DP | Check 3.3 V, GND, SWCLK/SWDIO; lower `adapter speed`; hold reset then release |
| `TARGET: timed out` | Power-cycle ICC; ensure only one probe attached |
| Flash verify error | Erase chip then re-flash: `commander device masserase` or OpenOCD `flash erase_address` |
| Device locked (IKEA) | Unlock with Commander for **MG1** part number — see community ICC unlock notes; do not use MG21 unlock scripts blindly |
| Bricked app, bootloader OK | Flash `.gbl` via Gecko Bootloader UART recovery if present; else SWD reflash `.s37` |
| RCP never answers Spinel | Confirm image is **ot-rcp**, baud **460800**, pins **PB14/PB15**, TX/RX not swapped |

After a bad flash you can always re-flash Zigbee NCP (other project) or OT-RCP again over SWD — the debug port does not depend on the app image.

---

## 9. Helper script

See [`tools/flash-icc-openocd.sh`](../tools/flash-icc-openocd.sh) for a template that passes a remote OpenOCD host/port and image path. Edit the OpenOCD interface block to match your CMSIS-DAP-over-Wi‑Fi firmware.
