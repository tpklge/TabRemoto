#!/usr/bin/env python3
"""Fetch the exact upstream DNS parser/resolver; no credentials or device I/O."""
import hashlib,json,tarfile,urllib.request,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1];pin=json.loads((root/'research/cares-dependencies.json').read_text());archive=root/'research'/('c-ares-'+pin['version']+'.tar.gz');source=root/'research/c-ares'
if not archive.exists():
 with urllib.request.urlopen(pin['url'],timeout=45) as response:archive.write_bytes(response.read())
if hashlib.sha256(archive.read_bytes()).hexdigest()!=pin['sha256']:raise RuntimeError('c-ares archive checksum mismatch')
if not (source/'CMakeLists.txt').exists():
 source.mkdir(parents=True,exist_ok=True)
 with tarfile.open(archive) as tar:
  prefix='c-ares-'+pin['version']+'/'
  for member in tar.getmembers():
   if not member.name.startswith(prefix) or not member.isfile():continue
   relative=Path(member.name[len(prefix):])
   if relative.is_absolute() or '..' in relative.parts:raise ValueError('Unsafe c-ares archive path')
   target=source/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(tar.extractfile(member).read())

patch=root/pin['patch'];command=['patch','-p1','--batch','-i',str(patch)]
probe=subprocess.run(command+['--dry-run','--forward'],cwd=source,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
if probe.returncode==0:subprocess.run(command,cwd=source,check=True,stdout=subprocess.DEVNULL)
elif subprocess.run(command+['--dry-run','--reverse'],cwd=source,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode:raise RuntimeError('c-ares source differs from pinned patch')
