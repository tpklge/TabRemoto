#!/bin/bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
export IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/python_env/idf5.4_py3.9_env}"
source "$IDF_PYTHON_ENV_PATH/bin/activate"
source "${IDF_PATH:-$HOME/esp/esp-idf}/export.sh" >/dev/null
python "$root/tools/prepare_rdp_sources.py"
python - "$root" <<'FLAGS'
import json,shlex,sys
from pathlib import Path
root=Path(sys.argv[1])
commands=json.loads((root/'build/compile_commands.json').read_text())
command=shlex.split(next(c['command'] for c in commands if c['file'].endswith('/main/main.cpp')))
flags=[v for v in command[1:] if v.startswith(('-I','-D')) and not any(k in v for k in ('IDF_VER','LV_CONF','_GLIBCXX'))]
(root/'research/rdp-idf-flags.rsp').write_text(shlex.join(flags))
FLAGS
export PKG_CONFIG_LIBDIR=/nonexistent
exec cmake -S "$root/research/FreeRDP" -B "$root/research/build-rdp-idf" \
  -DCMAKE_TOOLCHAIN_FILE="$root/research/freerdp-p4.cmake" \
  -DOPENSSL_INCLUDE_DIR="$root/research/openssl-install/include" \
  -DOPENSSL_CRYPTO_LIBRARY="$root/research/openssl-install/lib/libcrypto.a" \
  -DOPENSSL_SSL_LIBRARY="$root/research/openssl-install/lib/libssl.a" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=OFF -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF -DWINPR_HAVE_SYS_EVENTFD_H=ON -DESP_PLATFORM=ON -DCMAKE_C_FLAGS="-I$root/components/tabremoto_rdp_port/compat -DESP_PLATFORM=1 @${root}/research/rdp-idf-flags.rsp -march=rv32imafc_zicsr_zifencei -mabi=ilp32f -D__STDC_NO_THREADS__=1 -ffunction-sections -fdata-sections -Os" \
  -DUSE_UNWIND=OFF -DUSE_EXECINFO=OFF \
  -DWITH_OPENSSL=ON -DWITH_MBEDTLS=OFF -DWITH_INTERNAL_MD4=ON -DWITH_INTERNAL_RC4=ON \
  -DWITH_UNICODE_BUILTIN=ON -DWITH_JSON_DISABLED=ON -DWITH_AAD=OFF -DWITH_KRB5=OFF \
  -DWITH_WINPR_TOOLS=OFF -DWITH_FUSE=OFF -DWITH_CUPS=OFF -DWITH_PCSC=OFF \
  -DWITH_CLIENT=OFF -DWITH_CLIENT_COMMON=OFF -DWITH_SERVER=OFF \
  -DWITH_X11=OFF -DWITH_WAYLAND=OFF -DWITH_CLIENT_SDL=OFF -DWITH_SAMPLE=OFF \
  -DWITH_CHANNELS=OFF -DWITH_SMARTCARD_EMULATE=OFF -DWITH_FFMPEG=OFF \
  -DWITH_SWSCALE=OFF -DWITH_OPUS=OFF -DWITH_OPENH264=OFF -DWITH_CAIRO=OFF \
  -DWITH_PULSE=OFF -DWITH_ALSA=OFF -DWITH_MANPAGES=OFF -DBUILD_TESTING=OFF \
  -DBUILD_SHARED_LIBS=OFF -DTARGET_ARCH=riscv32 "$@"
