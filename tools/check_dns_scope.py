#!/usr/bin/env python3
"""Real P4 full-tunnel DNS fail-closed and interface-specific DHCP DNS tests."""
import sys,json,time,uuid
from pathlib import Path
from device import Device
from provision_device import provision
payload=json.loads(sys.stdin.readline());lab=json.loads(Path('artifacts/device-private/remaining-vpn0.json').read_text());d=Device();r={'status':'running','checks':{}}
def wait(fn,seconds=45):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=d.command(timeout=10)
  if fn(s):return s
  time.sleep(.3)
 raise TimeoutError('DNS policy state did not arrive')
def click(label):
 provision({'op':'ui-click','label':label},device=d)
 for _ in range(50):
  state=d.command('ui-action-status')['result']
  if state==2:return
  assert state!=3,label;time.sleep(.1)
 raise TimeoutError(label)
def rejected(host):
 before=d.command()['ssh']['revision'];provision({'op':'ssh','host':host,'user':'tabremoto-test','transport':'wifi','port':22},device=d)
 return wait(lambda s:s['ssh']['revision']>before and s['ssh']['phase']==5)['ssh']['detail']
try:
 s=d.command();assert not s['network']['vpn_armed'] and s['diagnostics']
 if not s['network']['wifi']:provision(payload['wifi'],device=d)
 wait(lambda s:s['network']['wifi']);provision({'op':'clock','epoch':int(time.time())},device=d)
 lines=[]
 for line in lab['vpn']['config'].splitlines():
  if line.strip().startswith('DNS'):continue
  if line.strip().startswith('AllowedIPs'):line='AllowedIPs = 0.0.0.0/0, ::/0'
  lines.append(line)
 provision({'op':'vpn','profile':'remaining-full','transport':'wifi','config':'\n'.join(lines)+'\n'},device=d)
 wait(lambda s:any(t['id']=='remaining-full' and t['up'] for t in s['network']['tunnels']))
 assert rejected('shared.test').startswith('DNS fisico protegido por VPN')
 r['checks']['full_tunnel_without_dns_blocks_physical_lookup_before_query']=True
 d.command('network-open');time.sleep(.4);click('WireGuard');click('Fechar e liberar rotas');click('Confirmar');wait(lambda s:not s['network']['vpn_armed'])
 r['checks']['explicit_visible_release_of_only_test_route_policy']=True
 detail=rejected('tabremoto-'+uuid.uuid4().hex[:8]+'.invalid')
 assert 'not found' in detail.lower(),detail
 r['checks']['interface_specific_dhcp_dns_returns_real_nxdomain']=True;r['status']='passed'
except BaseException as e:r['status']='failed';r['error']=str(e);raise
finally:d.close();Path('artifacts/remaining-dns-scope-device.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2),flush=True)
