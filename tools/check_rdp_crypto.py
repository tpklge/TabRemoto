#!/usr/bin/env python3
"""Real local TLS test on Tab5; does not test RDP, NLA, sockets or a server."""
import json,time
from pathlib import Path
from device import Device
root=Path(__file__).resolve().parents[1]
report={'status':'running','real_device':True,'network_tested':False,'rdp_nla_tested':False,'cycles':[]}
def save(): (root/'artifacts/rdp-crypto-check.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
with (root/'artifacts/rdp-crypto-check-serial.log').open('w') as log:
 d=Device(log=log)
 try:
  report['initial']=d.command('status');save()
  for i in range(3):
   d.command('rdp-probe');deadline=time.monotonic()+60
   while time.monotonic()<deadline:
    s=d.command('status');r=s['rdp_crypto_probe']
    if not r['running']:break
    time.sleep(.5)
   else:raise TimeoutError('TLS probe did not finish')
   report['cycles'].append(s);save()
   assert r['error']==0 and all(r[k] for k in ('sha256','random','tls','hostname_rejected','untrusted_rejected')),r
   print('TLS cycle passed',i+1,r['elapsed_ms'],r['internal_before'],r['internal_after'],flush=True)
   time.sleep(2)
  assert report['cycles'][-1]['rdp_crypto_probe']['internal_after']>=report['cycles'][0]['rdp_crypto_probe']['internal_after']-16384,'Memory retained across repeated probes'
  d.command('input-selftest');deadline=time.monotonic()+15
  while time.monotonic()<deadline:
   s=d.command('status')
   if s['input_selftest'] in (2,3):break
   time.sleep(.5)
  assert s['input_selftest']==2 and s['resources'] and s['external_font'],s
  report['final']=s;report['status']='passed'
 except BaseException as e:report['status']='failed';report['error']=repr(e);raise
 finally:save();d.close()
