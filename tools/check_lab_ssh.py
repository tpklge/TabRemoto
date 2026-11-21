#!/usr/bin/env python3
"""Authenticate Tab5 to owned lab, with encrypted Ed25519 private key.
Reads lab credentials from an ignored private file; reports only known marker.
"""
import json,time,sys
from pathlib import Path
from device import Device
from provision_device import provision

def command(cmd):
 d=Device()
 try:return d.command(cmd)
 finally:d.close()

def wait(predicate,seconds=40):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=command('status')
  if predicate(s):return s
  time.sleep(.5)
 raise TimeoutError('Lab test state did not arrive')
if __name__=='__main__':
 lab=json.loads(Path('artifacts/device-private/lab-credentials.json').read_text())
 p=lab['ssh'];p.pop('password',None)
 inside='--vpn' in sys.argv
 if inside:p.update(host='10.87.0.1',port=2222,require_vpn=True)
 command('ssh-disconnect');wait(lambda s:s['ssh']['phase'] in (0,5,6))
 provision(p)
 s=wait(lambda s:s['ssh']['phase'] in (5,6))
 out=command('ssh-output')['output']
 ok='TabRemoto lab key authentication OK' in out
 report={'status':'passed' if ok else 'failed','real_device':True,'encrypted_ed25519_key':ok,'wireguard':inside,'detail':s['ssh']['detail']}
 Path('artifacts/stage45-key'+('-vpn' if inside else '')+'.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2));assert ok
