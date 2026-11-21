#!/usr/bin/env python3
"""Pinned PuTTY PPK reader; only algorithm selection and scheduling are patched."""
import hashlib,json,subprocess,tarfile,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1];pin=json.loads((root/'research/putty-dependencies.json').read_text());source=root/'research/putty';archive=root/'research/putty-0.83.tar.gz'
if not (source/'sshpubk.c').exists():
 if not archive.exists():
  with urllib.request.urlopen(pin['url'],timeout=45) as response:archive.write_bytes(response.read())
 if hashlib.sha256(archive.read_bytes()).hexdigest()!=pin['sha256']:raise RuntimeError('PuTTY archive checksum mismatch')
 source.mkdir(parents=True,exist_ok=True)
 with tarfile.open(archive) as tar:
  prefix='putty-'+pin['version']+'/'
  for member in tar.getmembers():
   if member.name.startswith(prefix) and member.isfile():
    relative=Path(member.name[len(prefix):])
    if relative.is_absolute() or '..' in relative.parts:raise ValueError('Unsafe upstream archive path')
    target=source/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(tar.extractfile(member).read())
patch=root/'patches/putty-0.83-esp-idf.patch'
command=['patch','-p1','--batch','-i',str(patch)]
probe=subprocess.run(command+['--dry-run','--forward'],cwd=source,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
if probe.returncode==0:subprocess.run(command,cwd=source,check=True,stdout=subprocess.DEVNULL)
elif subprocess.run(command+['--dry-run','--reverse'],cwd=source,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode:raise RuntimeError('PuTTY source differs from the pinned port patch')
