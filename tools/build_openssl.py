#!/usr/bin/env python3
"""Cross-build real OpenSSL for the ESP32-P4, using this IDF build's headers.
Experimental single-worker build; no host libraries or cryptographic replacements.
"""
import json,os,shlex,subprocess,hashlib,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
commands=json.loads((root/'build/compile_commands.json').read_text())
command=shlex.split(next(c['command'] for c in commands if c['file'].endswith('/main/main.cpp')))
compiler=Path(command[0]);bindir=compiler.parent
flags=[v for v in command[1:] if v.startswith(('-I','-D')) and not any(k in v for k in ('IDF_VER','LV_CONF','_GLIBCXX'))]
flags+=['-march=rv32imafc_zicsr_zifencei','-mabi=ilp32f','-Os','-ffunction-sections','-fdata-sections','-DOPENSSL_NO_UNIX_SOCK','-DNO_SYSLOG','-Dlstat=stat','-include',str(root/'components/tabremoto_rdp_port/include/openssl_idf_compat.h')]
env=os.environ.copy();env.update(CC=str(bindir/'riscv32-esp-elf-gcc'),AR=str(bindir/'riscv32-esp-elf-ar'),RANLIB=str(bindir/'riscv32-esp-elf-ranlib'),CFLAGS=shlex.join(flags))
env['PATH']=str(bindir)+os.pathsep+env['PATH']
build=root/'research/openssl-build';build.mkdir(exist_ok=True)
stamp=build/'tabremoto-patch.sha256'
patch_digest=hashlib.sha256((root/'patches/openssl-3.5.9-esp-idf.patch').read_bytes()).hexdigest()
if '--if-needed' in sys.argv and stamp.exists() and stamp.read_text().strip()==patch_digest and (build/'libcrypto.a').exists() and (build/'libssl.a').exists():sys.exit(0)
args=['perl',str(root/'research/openssl/Configure'),'linux-generic32','no-shared','no-pic','no-dso','no-module','no-tests','no-apps','no-asm','no-async','no-ui-console','no-threads','no-engine','no-comp','no-quic','no-dgram','--with-rand-seed=getrandom','--openssldir=/sdcard/TabRemoto/trust/openssl','--prefix='+str(root/'research/openssl-install')]
with (root/'artifacts/openssl-configure.log').open('w') as out:subprocess.run(args,cwd=build,env=env,stdout=out,stderr=subprocess.STDOUT,check=True)
with (root/'artifacts/openssl-build.log').open('w') as out:subprocess.run(['make','-j8','build_libs'],cwd=build,env=env,stdout=out,stderr=subprocess.STDOUT,check=True)

with (root/'artifacts/openssl-install.log').open('w') as out:subprocess.run(['make','install_dev'],cwd=build,env=env,stdout=out,stderr=subprocess.STDOUT,check=True)

stamp.write_text(patch_digest+"\n")
