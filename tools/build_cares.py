#!/usr/bin/env python3
"""Cross-build the exact c-ares resolver against the actual P4/IDF headers."""
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(root/'tools/prepare_cares.py')],check=True)
inputs=[root/n for n in ('research/cares-dependencies.json','patches/cares-1.34.9-esp-idf.patch','research/freerdp-p4.cmake','tools/build_cares.py','research/rdp-idf-flags.rsp')]
fingerprint=hashlib.sha256(b''.join(p.read_bytes() for p in inputs)).hexdigest();build=root/'research/build-cares-idf';stamp=build/'tabremoto-inputs.sha256'
if (build/'lib/libcares.a').exists() and stamp.exists() and stamp.read_text().strip()==fingerprint:raise SystemExit(0)
with (root/'artifacts/native-cares-configure.log').open('w') as log:
 subprocess.run(['cmake','--fresh','-S',str(root/'research/c-ares'),'-B',str(build),'-DCMAKE_TOOLCHAIN_FILE='+str(root/'research/freerdp-p4.cmake'),'-DCARES_STATIC=ON','-DCARES_SHARED=OFF','-DCARES_BUILD_TOOLS=OFF','-DCARES_BUILD_TESTS=OFF','-DCARES_THREADS=OFF','-DHAVE___SYSTEM_PROPERTY_GET=OFF','-DCARES_INSTALL=OFF','-DCMAKE_C_FLAGS=-Os -ffunction-sections -fdata-sections -DESP_PLATFORM=1 -D__STDC_NO_THREADS__=1 -march=rv32imafc_zicsr_zifencei -mabi=ilp32f @'+str(root/'research/rdp-idf-flags.rsp')],stdout=log,stderr=subprocess.STDOUT,check=True)
with (root/'artifacts/native-cares-libraries.log').open('w') as log:subprocess.run(['cmake','--build',str(build),'-j','8'],stdout=log,stderr=subprocess.STDOUT,check=True)
stamp.write_text(fingerprint+'\n')
