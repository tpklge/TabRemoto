#!/usr/bin/env python3
"""Fetch exact source revisions, reject unexpected edits, apply recorded OS glue."""
import json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[1]
for name,dep in json.loads((root/'research/network-dependencies.json').read_text()).items():
 path=root/'research'/name
 def git(*args):return subprocess.check_output(['git','-C',str(path),*args],text=True).strip()
 if not (path/'.git').exists():
  subprocess.run(['git','init',str(path)],check=True,stdout=subprocess.DEVNULL)
  subprocess.run(['git','-C',str(path),'remote','add','origin',dep['url']],check=True)
  subprocess.run(['git','-C',str(path),'fetch','--depth','1','origin',dep['commit']],check=True)
  subprocess.run(['git','-C',str(path),'checkout','--detach','FETCH_HEAD'],check=True)
 if git('rev-parse','HEAD')!=dep['commit']:raise RuntimeError('Unexpected revision: '+name)
 patch=root/dep['patch']
 if git('diff')==patch.read_text().strip():continue
 if git('status','--porcelain'):raise RuntimeError('Unrecorded source edits: '+name)
 if patch.stat().st_size:subprocess.run(['git','-C',str(path),'apply',str(patch)],check=True)
