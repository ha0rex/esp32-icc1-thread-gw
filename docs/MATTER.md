# Standalone Matter gateway

The ESP32-S3 runs two real protocol roles:

- an OpenThread Border Router using the ICC-1-A as an external Spinel UART RCP;
- an ESP-Matter controller/commissioner with its own persistent local fabric.

This is a standalone development fabric. Devices paired here do not
automatically appear in Apple Home, Google Home, Alexa, or Home Assistant.

## Build the basic application

The project pins ESP-Matter `release/v1.4` with ESP-IDF `v5.4.1`.
Allow roughly 10 GB of free disk space for connectedhomeip and its build
environment.

```bash
./scripts/bootstrap.sh
source scripts/export-sdk.sh
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

`bootstrap.sh` also downloads production PAA roots from the CSA Distributed
Compliance Ledger into `paa_cert/`. These roots are required to attest retail
IKEA Matter devices.

The full OTBR build expects a responsive RCP during startup. Build and flash
the ESP32 application now, but do not attempt Thread commands until the ICC has
the OpenThread RCP firmware described below.

## ICC RCP requirements

The stock ICC firmware is not an OpenThread RCP. Flash a pure OpenThread
Spinel/HDLC RCP image with these settings:

| Setting | Value |
| --- | --- |
| Part | EFR32MG1P132F256 |
| RCP TX | PB14 / ICC pad 3 / ESP GPIO4 |
| RCP RX | PB15 / ICC pad 2 / ESP GPIO2 |
| UART | 460800, 8N1 |
| Flow control | None |
| Protocol | OpenThread Spinel over HDLC |

Use SWD pads PF0/PF1 and RESETn. Detailed wiring and recovery instructions are
in [icc-ot-rcp-flash.md](icc-ot-rcp-flash.md). Do not flash an EZSP Zigbee NCP
image.

## First commissioning session

All commands are entered in the USB serial monitor.

1. Connect the gateway to Wi-Fi:

   ```text
   matter esp wifi connect YOUR_SSID YOUR_PASSWORD
   ```

2. Create and start a Thread network:

   ```text
   matter esp ot_cli dataset init new
   matter esp ot_cli dataset commit active
   matter esp ot_cli dataset active -x
   matter esp ot_cli ifconfig up
   matter esp ot_cli thread start
   matter esp ot_cli state
   ```

3. Factory-reset the IKEA sensor so it advertises for Matter commissioning,
   then pair it using the `MT:...` payload printed below its Matter QR code:

   ```text
   matter esp controller pairing code-thread 1234 DATASET_HEX 'MT:YOUR_SETUP_PAYLOAD'
   ```

   `1234` is the local node ID. Use a different non-zero ID for every sensor.
   If only separate setup PIN and discriminator values are available, use the
   lower-level form:

   ```text
   matter esp controller pairing ble-thread 1234 DATASET_HEX SETUP_PIN DISCRIMINATOR
   ```

4. Discover endpoints and clusters:

   ```text
   matter esp gateway discover 1234
   ```

5. Save and subscribe to a window/contact sensor:

   ```text
   matter esp gateway remember 1234 ENDPOINT contact window
   ```

   Or save a motion sensor:

   ```text
   matter esp gateway remember 1235 ENDPOINT motion hallway
   ```

The registry survives reboot. After restarting the Thread network, restore all
subscriptions with:

```text
matter esp gateway watch-all
```

Contact reports are logged as `OPEN` or `CLOSED`. Occupancy reports are logged
as `DETECTED` or `CLEAR`.

Useful commands:

```text
matter esp gateway nodes
matter esp gateway read 1234
matter esp gateway watch 1234
matter esp gateway forget 1234
```

`gateway forget` removes only the local sensor record and subscription. It
does not remove the Matter fabric credentials from the sensor; factory-reset
the sensor before commissioning it to another fabric.

## Security scope

The MVP uses a local test operational-credentials issuer, which is suitable for
a private development fabric but not a commercial product. Device attestation
still uses production PAA roots. A production gateway needs a protected root CA
and NOC issuance design, secure key storage, access control, recovery, and
firmware update policy.
