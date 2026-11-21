#!/usr/bin/env python3
"""Real Tab5 negative test: strict Ethernet cannot fall back to active Wi-Fi.
Private SSH configuration arrives via stdin and is not logged.
"""
import sys,json,time
from pathlib import Path
from device import Device
from provision_device import provision
payload=json.loads(sys.stdin.readline());d=Device();report={'status':'running','checks':{}}
try:
 s=d.command();assert s['network']['wifi'] and not s['network']['ethernet'];d.command('ssh-disconnect');time.sleep(.5)
 for host,label in ((payload['host'],'literal_transport_rejected'),('invalid.test','hostname_rejected_without_physical_transport')):
  p=dict(payload,host=host,transport='ethernet',require_vpn=False);p.pop('command',None);provision(p,device=d)
  end=time.monotonic()+30
  while time.monotonic()<end:
   s=d.command()
   if s['ssh']['phase'] in (4,5,6):break
   time.sleep(.2)
  assert s['ssh']['phase']==5 and s['ssh']['rx']==0 and s['ssh']['tx']==0 and not s['ssh']['fingerprint']
  report['checks'][label]=True
 report['status']='passed'
finally:
 Path('artifacts/five-stages-transport-policy.json').write_text(json.dumps(report,indent=2)+'\n');d.close()
print(json.dumps(report,indent=2))
