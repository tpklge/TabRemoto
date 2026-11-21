#!/usr/bin/env python3
"""Negative real SSH checks against the owned lab, never the user's server."""
import json
from pathlib import Path
from check_lab_ssh import command,wait
from provision_device import provision
lab=json.loads(Path('artifacts/device-private/lab-credentials.json').read_text())
p=dict(lab['ssh']);p.pop('password',None)
report={'real_device':True,'status':'running','checks':{}}
path=Path('artifacts/stage45-ssh-negative.json')
try:
 command('ssh-disconnect');wait(lambda s:s['ssh']['phase'] in (0,5,6))
 changed=dict(p,fingerprint='0'*64)
 provision(changed)
 s=wait(lambda s:s['ssh']['phase'] in (2,4,5,6))
 assert s['ssh']['phase']==2 and s['ssh']['fingerprint']==p['fingerprint']
 report['checks']['changed_host_pin_requires_decision']=True
 command('ssh-disconnect');wait(lambda s:s['ssh']['phase'] in (5,6))
 wrong=dict(p,passphrase='Wrong-lab-passphrase')
 provision(wrong);s=wait(lambda s:s['ssh']['phase'] in (5,6))
 assert s['ssh']['phase']==5 and 'Autenticacao SSH recusada' in s['ssh']['detail']
 report['checks']['wrong_key_passphrase_rejected']=True
 report['status']='passed'
except BaseException as e:
 report.update(status='failed',error=str(e));raise
finally:path.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
