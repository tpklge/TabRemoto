#!/usr/bin/env python3
"""Native pinned NLA RDP through own WG IPv4/IPv6 byte relays to approved .102."""
import json,sys,time
from pathlib import Path
from device import Device
from provision_device import provision
payload=json.loads(sys.stdin.readline());lab=json.loads(Path('artifacts/device-private/remaining-vpn0.json').read_text());d=Device();r={'status':'running','checks':{}}
def wait(fn,seconds=60):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=d.command(timeout=10)
  if fn(s):return s
  time.sleep(.3)
 raise TimeoutError('RDP VPN state did not arrive')
def stop():
 d.command('rdp-disconnect-all');wait(lambda s:s['rdp']['phase'] in (0,4,5));time.sleep(.6)
try:
 s=d.command()
 if not any(t['id']=='remaining-0' and t['up'] for t in s['network']['tunnels']):
  provision(dict(lab['vpn'],profile='remaining-0'),device=d);wait(lambda s:any(t['id']=='remaining-0' and t['up'] for t in s['network']['tunnels']))
 for label,host in (('ipv4','10.90.0.11'),('ipv6','fd79:90::11')):
  stop();p=dict(payload['rdp'],op='rdp',host=host,transport='wifi',require_vpn=True,vpn_profile='remaining-0',width=960,height=540)
  provision(p,device=d);s=wait(lambda s:s['rdp']['phase']==3 and s['rdp']['frames']>0)
  assert s['rdp']['fingerprint']==p['fingerprint'] and s['rdp']['selected_protocol'] in (2,8)
  session=next(t for t in s['sessions'] if t['protocol']=='rdp' and t['selected']);assert session['vpn_profile']=='remaining-0' and session['transport']=='wifi'
  r['checks'][label+'_native_nla_pin_and_desktop_inside_wireguard']=True
 stop();r['status']='passed'
except BaseException as e:r['status']='failed';r['error']=str(e);raise
finally:d.close();Path('artifacts/remaining-rdp-vpn-device.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2),flush=True)
