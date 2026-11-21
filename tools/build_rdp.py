#!/usr/bin/env python3
"""Build the pinned native libraries when port/configuration inputs change.
Run through tools/build.sh so the ESP-IDF compiler environment is active.
"""
import subprocess,hashlib,json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
inputs=[root/n for n in ('research/rdp-dependencies.json','patches/freerdp-3.25.0-esp-idf.patch','research/freerdp-p4.cmake','tools/probe_rdp.sh')]
inputs+=sorted((root/'components/tabremoto_rdp_port/compat').rglob('*.h'))
fingerprint=hashlib.sha256(b''.join(str(p.relative_to(root)).encode()+p.read_bytes() for p in inputs)).hexdigest()
stamp=root/'research/build-rdp-idf/tabremoto-inputs.sha256'
archive=root/'research/build-rdp-idf/libfreerdp/libfreerdp3.a'
if archive.exists() and stamp.exists() and stamp.read_text().strip()==fingerprint:raise SystemExit(0)
with (root/'artifacts/native-rdp-configure.log').open('w') as log:subprocess.run(['bash','tools/probe_rdp.sh'],cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True)
with (root/'artifacts/native-rdp-libraries.log').open('w') as log:subprocess.run(['cmake','--build','research/build-rdp-idf','-j','10'],cwd=root,stdout=log,stderr=subprocess.STDOUT,check=True)
stamp.write_text(fingerprint+'\n')
