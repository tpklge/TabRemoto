#!/usr/bin/env python3
"""Drop a temporary transparent TCP fixture, verify native FreeRDP reconnect."""
import sys,json,time
from pathlib import Path
from device import Device
from provision_device import provision
payload=json.loads(sys.stdin.readline());d=Device();r={'status':'running','checks':{}}
def wait(fn,seconds=60):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=d.command(timeout=10)
  if fn(s):return s
  time.sleep(.4)
 raise TimeoutError('Native RDP reconnect state did not arrive')
try:
 baseline=json.loads(Path('artifacts/device-private/remaining-rdp-fault/state.json').read_text())
 d.command('rdp-disconnect-all');wait(lambda s:s['rdp']['phase'] in (0,4,5));time.sleep(.5)
 p=dict(payload['rdp'],op='rdp',host='192.168.88.71',port=43389,width=960,height=540,reconnect=True)
 provision(p,device=d);s=wait(lambda s:s['rdp']['phase']==3 and s['rdp']['frames']>0)
 assert s['rdp']['fingerprint']==p['fingerprint'];r['checks']['native_rdp_through_byte_transparent_fixture']=True
 initial_reconnections=s['rdp_reconnections'];initial_attempts=s['rdp_reconnect_attempts']
 for outage in range(4):
  before=s['rdp']['frames'];Path('artifacts/device-private/remaining-rdp-fault/drop').touch()
  s=wait(lambda s:s['rdp']['phase']==3 and s['rdp_reconnections']>=initial_reconnections+outage+1,90)
  time.sleep(1);s=d.command();painted=s['rdp']['frames']
  if s['diagnostics']:
   for code,down in ((0x01,True),(0x01,False),(0x15b,True),(0x13,True),(0x13,False),(0x15b,False)):
    d.command('rdp-key '+hex(code)+' '+str(int(down)));time.sleep(.1)
   s=wait(lambda s:s['rdp']['frames']>painted,20)
   d.command('rdp-key 0x01 1');time.sleep(.1);d.command('rdp-key 0x01 0')
  else:s=wait(lambda s:s['rdp']['frames']>before,20)
  assert s['rdp']['fingerprint']==p['fingerprint']
  time.sleep(2)
 state=json.loads(Path('artifacts/device-private/remaining-rdp-fault/state.json').read_text());assert state['accepted']>=baseline['accepted']+5 and state['faults']==baseline['faults']+4
 r['checks']['four_separate_connection_losses_recovered_with_same_pin_and_new_frames']=True
 r['attempts']=s['rdp_reconnect_attempts'];r['reconnections']=s['rdp_reconnections'];r['fixture']=state;r['status']='passed'
except BaseException as e:r['status']='failed';r['error']=str(e);raise
finally:d.close();Path('artifacts/remaining-rdp-reconnect-device.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2),flush=True)
