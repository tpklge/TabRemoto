#!/usr/bin/env python3
"""Disposable lab keys only; credentials arrive privately on stdin or 0600 lab JSON."""
import json,sys,time
from pathlib import Path
from device import Device
from provision_device import provision
payload=json.loads(sys.stdin.readline());lab=json.loads(Path('artifacts/device-private/usability-lab0.json').read_text());fixtures=json.loads(Path('artifacts/device-private/usability-import-fixtures.json').read_text())
report={'status':'running','checks':{}};log_path=Path('artifacts/device-private/usability-key-import.serial.log');log_path.touch(mode=0o600);log_path.chmod(0o600)
with log_path.open('w') as log:
 d=Device(log=log)
 def wait(fn,seconds=40):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   s=d.command(timeout=8)
   if fn(s):return s
   time.sleep(.2)
  raise TimeoutError('Expected key import state did not arrive')
 try:
  if not d.command()['network']['wifi']:provision(payload['wifi'],device=d)
  wait(lambda s:s['network']['wifi'])
  passed=0
  for fixture in fixtures:
   if Path(fixture['remote']).suffix not in ('.pem','.ppk'):continue
   d.command('ssh-disconnect');wait(lambda s:s['ssh']['phase'] in (0,5,6));time.sleep(.2)
   p={k:lab['ssh'][k] for k in ('host','port','user','fingerprint','command')};p.update(op='ssh',key_file=fixture['remote'],passphrase='UnitKey42')
   before=d.command()['ssh']['revision'];provision(p,device=d);s=wait(lambda s:s['ssh']['revision']>before and s['ssh']['phase'] in (5,6))
   assert s['ssh']['phase']==6,s['ssh']['detail']
   assert 'TabRemoto lab key authentication OK' in d.command('ssh-output')['output']
   passed+=1;print('Key import/authentication case',passed,'passed',flush=True)
  report['checks']['file_key_authentication_cases']=passed;assert passed==15
  sample=next(item for item in fixtures if item['source'].endswith('fixture-ed25519-v3-encrypted.ppk'))
  p={k:lab['ssh'][k] for k in ('host','port','user','fingerprint','command')};p.update(op='ssh',key_file=sample['remote'],passphrase='wrong')
  before=d.command()['ssh']['revision'];provision(p,device=d);s=wait(lambda s:s['ssh']['revision']>before and s['ssh']['phase']==5);assert s['ssh']['rx']==0 and s['ssh']['tx']==0 and not s['ssh']['fingerprint'];report['checks']['wrong_passphrase_rejected_before_tcp']=True
  damaged=Path(sample['source']).read_text();index=damaged.index('Private-MAC: ')+13;damaged=damaged[:index]+('b' if damaged[index]=='a' else 'a')+damaged[index+1:]
  p.pop('key_file');p.update(key=damaged,passphrase='UnitKey42');before=d.command()['ssh']['revision'];provision(p,device=d);s=wait(lambda s:s['ssh']['revision']>before and s['ssh']['phase']==5);assert s['ssh']['rx']==0 and not s['ssh']['fingerprint'];report['checks']['ppk_mac_rejected_before_tcp']=True
  report['status']='passed'
 except BaseException as e:report['status']='failed';report['error']=str(e);raise
 finally:d.close();Path('artifacts/usability-key-import-device.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2),flush=True)
