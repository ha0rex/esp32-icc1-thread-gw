#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Keep the versions together: ESP-Matter release/v1.4 recommends ESP-IDF
# v5.4.1. Reuse the conventional ~/esp location to avoid duplicating the large
# IDF toolchain. Set SDK_ROOT to use an isolated location when disk permits.
SDK_ROOT="${SDK_ROOT:-${HOME}/esp}"
IDF_DIR="${SDK_ROOT}/esp-idf"
MATTER_DIR="${SDK_ROOT}/esp-matter"
IDF_TAG="v5.4.1"
MATTER_BRANCH="release/v1.4"

# Apple's system Python 3.9 has old importlib metadata behavior that can reject
# modern dotted package names (for example ruamel.yaml). Prefer a current
# framework Python when it is installed.
MAC_PYTHON_BIN="/Library/Frameworks/Python.framework/Versions/3.13/bin"
if [[ "$(uname -s)" == "Darwin" && -x "${MAC_PYTHON_BIN}/python3" ]]; then
  export PATH="${MAC_PYTHON_BIN}:${PATH}"
fi

mkdir -p "$SDK_ROOT"

if [[ ! -d "${IDF_DIR}/.git" ]]; then
  git clone --branch "$IDF_TAG" --depth 1 \
    https://github.com/espressif/esp-idf.git "$IDF_DIR"
else
  EXPECTED_IDF="$(git -C "$IDF_DIR" rev-list -n 1 "$IDF_TAG" 2>/dev/null || true)"
  ACTUAL_IDF="$(git -C "$IDF_DIR" rev-parse HEAD)"
  if [[ -z "$EXPECTED_IDF" || "$ACTUAL_IDF" != "$EXPECTED_IDF" ]]; then
    echo "Existing ${IDF_DIR} is not ${IDF_TAG}." >&2
    echo "Set SDK_ROOT to an empty directory; bootstrap will not switch it." >&2
    exit 2
  fi
fi
git -C "$IDF_DIR" submodule update --init --recursive --depth 1
"${IDF_DIR}/install.sh" esp32s3

if [[ ! -d "${MATTER_DIR}/.git" ]]; then
  git clone --branch "$MATTER_BRANCH" --depth 1 \
    https://github.com/espressif/esp-matter.git "$MATTER_DIR"
fi
git -C "$MATTER_DIR" fetch --depth 1 origin "$MATTER_BRANCH"
git -C "$MATTER_DIR" checkout --detach FETCH_HEAD
git -C "$MATTER_DIR" submodule update --init --depth 1

HOST_PLATFORM="linux"
if [[ "$(uname -s)" == "Darwin" ]]; then
  HOST_PLATFORM="darwin"
fi
python3 \
  "${MATTER_DIR}/connectedhomeip/connectedhomeip/scripts/checkout_submodules.py" \
  --platform esp32 "$HOST_PLATFORM" --shallow

# Install only the embedded SDK tools; this firmware does not need host
# chip-tool.
# shellcheck disable=SC1091
source "${IDF_DIR}/export.sh"
pushd "$MATTER_DIR" >/dev/null
./install.sh --no-host-tool
popd >/dev/null

# Retail devices use production Device Attestation Certificates. Populate the
# SPIFFS image with the current CSA DCL PAA roots.
PAA_DIR="${ROOT}/paa_cert"
mkdir -p "$PAA_DIR"
python3 \
  "${MATTER_DIR}/connectedhomeip/connectedhomeip/credentials/fetch_paa_certs_from_dcl.py" \
  --use-main-net-http --paa-trust-store-path "$PAA_DIR"

# SPIFFS object names are limited to 32 bytes. Certificate filenames from the
# DCL contain full subjects, so replace them with stable short content hashes.
for cert in "$PAA_DIR"/*.der; do
  [[ -e "$cert" ]] || continue
  digest="$(shasum -a 256 "$cert" | awk '{print substr($1, 1, 20)}')"
  short_name="${PAA_DIR}/${digest}.der"
  if [[ "$cert" != "$short_name" ]]; then
    mv -f "$cert" "$short_name"
  fi
done
rm -f "$PAA_DIR"/*.pem

cat >"${ROOT}/scripts/export-sdk.sh" <<EOF
#!/usr/bin/env bash
export IDF_PATH="${IDF_DIR}"
export ESP_MATTER_PATH="${MATTER_DIR}"
if [[ -x "${MAC_PYTHON_BIN}/python3" ]]; then
  export PATH="${MAC_PYTHON_BIN}:\${PATH}"
fi
source "\${IDF_PATH}/export.sh"
source "\${ESP_MATTER_PATH}/export.sh"
EOF
chmod +x "${ROOT}/scripts/export-sdk.sh"

cd "$ROOT"
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3" \
  set-target esp32s3

echo
echo "SDK ready. For a new shell run:"
echo "  source ${ROOT}/scripts/export-sdk.sh"
echo "  idf.py build"
