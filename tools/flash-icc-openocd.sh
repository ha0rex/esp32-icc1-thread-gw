#!/usr/bin/env bash
# Template: flash OT-RCP .s37 to ICC-1-A through OpenOCD (local or remote CMSIS-DAP).
# Edit OPENOCD_INTERFACE / remote host to match your Wi-Fi CMSIS-DAP firmware.
set -euo pipefail

IMAGE="${1:-firmware/ot-rcp-icc-pb14-pb15-460800.s37}"
OPENOCD_HOST="${OPENOCD_HOST:-localhost}"
OPENOCD_GDB_PORT="${OPENOCD_GDB_PORT:-3333}"

if [[ ! -f "$IMAGE" ]]; then
  echo "Image not found: $IMAGE" >&2
  echo "Build ot-rcp for EFR32MG1 PB14/PB15 @ 460800 and place it here." >&2
  exit 1
fi

echo "Flashing $IMAGE via ${OPENOCD_HOST}:${OPENOCD_GDB_PORT}"
echo "Ensure OpenOCD is already running against your CMSIS-DAP (see docs/icc-ot-rcp-flash.md)."

arm-none-eabi-gdb -nx --batch \
  -ex "target extended-remote ${OPENOCD_HOST}:${OPENOCD_GDB_PORT}" \
  -ex "monitor halt" \
  -ex "monitor flash write_image erase ${IMAGE}" \
  -ex "monitor reset run" \
  -ex "quit"

echo "Done. Wire UART PB14/PB15 @ 460800 to the gateway ESP32 and check 'spinel: RCP OK'."
