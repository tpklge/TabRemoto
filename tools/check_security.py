#!/usr/bin/env python3
"""Real SD crypto/catalog tests in exclusive test directories; no personal vault changes."""
import json,time,hashlib,struct
from pathlib import Path
from device import Device
from hardware_check import wait_launcher
ROOT=Path(__file__).resolve().parents[1]
report={'status':'running','real_device':True,'personal_vault_modified':False,'reboot_persistence_tested':False}
def save(): (ROOT/'artifacts/security-check.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
with (ROOT/'artifacts/security-check-serial.log').open('w') as log:
 d=Device(log=log)
 try:
  report['initial']=d.command('status',timeout=60);save()
  def run(op):
   d.command(op);deadline=time.monotonic()+600;observed_busy=False;started=time.monotonic();last_message=None
   while time.monotonic()<deadline:
    s=d.command('status',timeout=8);sec=s['security']
    observed_busy|=sec['busy']
    if sec.get('message')!=last_message:
     last_message=sec.get('message');print(op,last_message,flush=True)
    if not sec['busy'] and (observed_busy or time.monotonic()-started>2):
     assert sec['selftest']==2,s
     return s
    time.sleep(1)
   raise TimeoutError('Security worker did not finish: '+op)
  report['selftest']=run('security-selftest');save();print('AEAD/catalog/trust/SD selftest passed',report['selftest']['security']['operation_ms'],flush=True)
  d.command('input-selftest');time.sleep(2);s=d.command('status');assert s['input_selftest']==2,s
  report['trema_and_forms_injected']=s;save();print('Trema and forms passed',flush=True)
  report['before_reboot']=run('security-boot-prepare');save()
  d.command('reboot');d.close();d=None
  l=wait_launcher(None)
  # Independent host verification of the P4's encrypted public test fixture.
  from cryptography.hazmat.primitives.ciphers.aead import AESGCM
  candidates=[l.get_file('/sdcard/TabRemoto/cache/security-boot-test/store.'+slot) for slot in ('a','b')]
  raw=max(candidates,key=lambda x:struct.unpack_from('<I',x,12)[0])
  version,generation,cost,length=struct.unpack_from('<IIII',raw,8)
  assert raw[:8]==b'TRVAULT2' and version==2 and cost==16384 and len(raw)==148+length
  from cryptography.hazmat.primitives.kdf.scrypt import Scrypt
  kek=Scrypt(salt=raw[24:40],length=32,n=cost,r=8,p=1).derive(b'Tab58221')
  key=AESGCM(kek).decrypt(raw[40:52],raw[52:100],raw[:8]+raw[16:20])
  fixture=json.loads(AESGCM(key).decrypt(raw[120:132],raw[148:]+raw[132:148],raw[:132]))
  assert fixture['connections'][0]['favorite'] is True and fixture['secrets'][0]['value']=='Public fake test key'
  assert fixture['connections'][0]['identity_id']==fixture['identities'][0]['id'] and fixture['identities'][0]['secret_id']==fixture['secrets'][0]['id']
  report['independent_host_aead_verification']={'passed':True,'generation':generation,'kdf':'scrypt','N':cost,'r':8,'p':1,'fixture_only':True};save()
  l.boot();d=Device(log=log);s=d.command('status',timeout=60)
  assert s['security']['ready'] and not s['security']['unlocked'],s
  report['after_reboot']=run('security-boot-check');report['reboot_persistence_tested']=True;save();print('Catalog persistence after actual reboot passed',flush=True)
  # Self-tests create only isolated public fixtures. Production vault stays locked.
  assert not report['after_reboot']['security']['unlocked']
  d.command('security-open');time.sleep(1);d.command('snapshot',timeout=60)
  report['final']=d.command('status');report['status']='passed'
 except BaseException as e:report['status']='failed';report['error']=repr(e);raise
 finally:
  if d:d.close()
  save()
