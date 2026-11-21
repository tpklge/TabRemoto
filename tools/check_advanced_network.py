#!/usr/bin/env python3
"""Real P4 two-peer WireGuard/DNS/IPv6 isolation; ephemeral userspace lab only."""
import sys,json,time
from pathlib import Path
from device import Device
from provision_device import provision
payload=json.loads(sys.stdin.readline());labs=[json.loads(Path('artifacts/device-private/remaining-vpn'+str(i)+'.json').read_text()) for i in (0,1)]
r={'status':'running','checks':{}};d=Device()
def wait(fn,seconds=45):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=d.command(timeout=10)
  if fn(s):return s
  time.sleep(.3)
 raise TimeoutError('Expected network/session state did not arrive')
def ssh(index,host,required=True,profile=None):
 lab=labs[index];initial=d.command();old_revision=initial['ssh']['revision'];old=next(t['id'] for t in initial['sessions'] if t['protocol']=='ssh' and t['selected']);was_ready=initial['ssh']['phase']==4
 p={k:lab['ssh'][k] for k in ('user','password','fingerprint')};p.update(op='ssh',host=host,port=2222,transport='wifi',require_vpn=required,vpn_profile=('remaining-'+str(index)) if profile is None else profile)
 provision(p,device=d)
 s=wait(lambda s:((not was_ready and s['ssh']['revision']>old_revision) or any(t['protocol']=='ssh' and t['selected'] and t['id']!=old for t in s['sessions'])) and s['ssh']['phase'] in (4,5,6))
 assert s['ssh']['phase']==4,s['ssh']['detail']
 return s
try:
 s=d.command();assert s['diagnostics'] and not s['network']['vpn_armed'];assert not s['security']['unlocked']
 if not s['network']['wifi']:provision(payload['wifi'],device=d)
 wait(lambda s:s['network']['wifi']);provision({'op':'clock','epoch':int(time.time())},device=d)
 for i in (0,1):
  p=dict(labs[i]['vpn'],profile='remaining-'+str(i));provision(p,device=d)
  wait(lambda s:any(t['id']==p['profile'] and t['up'] for t in s['network']['tunnels']))
 r['checks']['two_real_handshakes_same_ipv4_ipv6_routes_and_dns']=True
 ssh(0,'shared.test');r['checks']['profile0_private_dns_and_ssh']=True
 ssh(1,'shared.test');r['checks']['profile1_distinct_answer_same_hostname']=True
 ssh(0,'v6only.test');r['checks']['ipv6_dns_answer_and_real_ssh_over_wireguard']=True
 # Stop only profile1: established profile0 leases must remain valid.
 d.command('vpn-stop remaining-1')
 s=wait(lambda s:not next(t for t in s['network']['tunnels'] if t['id']=='remaining-1')['up'])
 time.sleep(2);s=d.command()
 assert sum(t['phase']==4 for t in s['sessions'] if t['protocol']=='ssh')==2,s['sessions']
 r['checks']['other_tunnel_change_preserves_two_existing_sessions']=True
 # No implicit DNS selection among two armed private profiles, even if one stopped.
 before_revision=s['ssh']['revision'];before_selected=next(t['id'] for t in s['sessions'] if t['protocol']=='ssh' and t['selected']);provision(dict(labs[0]['ssh'],op='ssh',host='shared.test',port=2222,transport='wifi',command='',require_vpn=False,vpn_profile=''),device=d)
 s=wait(lambda s:s['ssh']['phase']==5 and (s['ssh']['revision']>before_revision or any(t['protocol']=='ssh' and t['selected'] and t['id']!=before_selected for t in s['sessions'])))
 r['checks']['ambiguous_private_dns_fails_closed']=True
 d.command('ssh-disconnect-all');wait(lambda s:all(t['phase']!=4 for t in s['sessions'] if t['protocol']=='ssh'))
 d.command('vpn-stop');wait(lambda s:not s['network']['vpn'])
 r['last_memory']={k:s[k] for k in ('internal_free','psram_free','psram_largest') if k in s};r['status']='passed'
except BaseException as e:r['status']='failed';r['error']=str(e);raise
finally:d.close();Path('artifacts/remaining-advanced-network-device.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2),flush=True)
