#!/usr/bin/env python3
"""Fetch pinned RDP dependencies and apply only the recorded platform patch."""
import json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def git(path,*args):return subprocess.check_output(['git','-C',str(path),*args],text=True).strip()
for name,dep in json.loads((root/'research/rdp-dependencies.json').read_text()).items():
 path=root/'research'/('FreeRDP' if name=='freerdp' else name)
 if not (path/'.git').exists():
  subprocess.run(['git','init',str(path)],check=True)
  subprocess.run(['git','-C',str(path),'remote','add','origin',dep['url']],check=True)
  subprocess.run(['git','-C',str(path),'fetch','--depth','1','origin',dep['commit']],check=True)
  subprocess.run(['git','-C',str(path),'checkout','--detach','FETCH_HEAD'],check=True)
 if git(path,'rev-parse','HEAD')!=dep['commit']:raise RuntimeError('Unexpected source revision: '+name)
 if 'patch' in dep:
  patch=root/dep['patch']
  if subprocess.run(['git','-C',str(path),'apply','--reverse','--check',str(patch)],capture_output=True).returncode==0:continue
  if git(path,'status','--porcelain'):raise RuntimeError('Unrecorded source changes: '+name)
  subprocess.run(['git','-C',str(path),'apply','--check',str(patch)],check=True)
  subprocess.run(['git','-C',str(path),'apply',str(patch)],check=True)
