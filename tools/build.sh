#!/bin/bash
set -euo pipefail
export IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/python_env/idf5.4_py3.9_env}"
source "$IDF_PYTHON_ENV_PATH/bin/activate"
source "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" >/dev/null
if [[ "$PWD" == "$(cd "$(dirname "$0")/.." && pwd)" ]]; then
  python tools/prepare_cares.py
  python tools/prepare_putty.py
  python tools/prepare_network_sources.py
fi
if [[ "$PWD" == "$(cd "$(dirname "$0")/.." && pwd)" ]]; then
  python tools/prepare_rdp_sources.py
  if [[ ! -f research/openssl-build/libssl.a || ! -f research/openssl-build/libcrypto.a || ! -f research/build-rdp-idf/libfreerdp/libfreerdp3.a || ! -f research/build-cares-idf/lib/libcares.a ]]; then
    idf.py -G 'Unix Makefiles' -DTABREMOTO_OPENSSL_BOOTSTRAP=ON reconfigure
  fi
  python tools/build_openssl.py --if-needed
  python tools/build_rdp.py
  python tools/build_cares.py
fi
exec idf.py -G 'Unix Makefiles' -DTABREMOTO_OPENSSL_BOOTSTRAP=OFF "$@"
