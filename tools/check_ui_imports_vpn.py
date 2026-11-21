#!/usr/bin/env python3
"""Exercise real visible file selectors and two disposable WireGuard peers."""
import json,time,sys
from pathlib import Path
from device import Device
from provision_device import provision
payload=json.loads(sys.stdin.readline());labs=[json.loads(Path('artifacts/device-private/usability-lab'+str(i)+'.json').read_text()) for i in (0,1)]
d=Device();report={'status':'running','checks':{}}
def wait(fn,seconds=45):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=d.command(timeout=8)
  if fn(s):return s
  time.sleep(.25)
 raise TimeoutError('Expected UI/VPN state did not arrive')
def click(label):
 provision({'op':'ui-click','label':label},device=d)
 end=time.monotonic()+8
 while time.monotonic()<end:
  result=d.command('ui-action-status')['result']
  if result==2:return
  assert result!=3,'Visible control not found: '+label
  time.sleep(.1)
 raise TimeoutError('UI action did not complete')
def choose_file(label,filename):
 click(label);time.sleep(.5);click(filename)
def probe(ip,expected=True):
 try:d.command('vpn-probe '+ip+' 7007');ok=True
 except RuntimeError:ok=False
 assert ok==expected
try:
 s=d.command();assert s['diagnostics'] and not s['network']['vpn_armed']
 if not s['network']['wifi']:provision(payload['wifi'],device=d)
 wait(lambda s:s['network']['wifi']);d.command('ssh-disconnect');time.sleep(.5)
 d.command('network-open');time.sleep(.3);click('WireGuard')
 ids=[]
 for index in (0,1):
  name='TabRemoto-test-usability-wg'+str(index)+'.conf';ids.append('arquivo:'+name)
  choose_file('Abrir .conf: Wi-Fi',name)
  wait(lambda s:any(v['id']==ids[index] and v['up'] for v in s['network']['tunnels']))
 wait(lambda s:sum(v['up'] for v in s['network']['tunnels'])==2)
 report['checks']['two_files_selected_through_visible_picker_and_two_real_handshakes']=True
 probe('10.87.0.1');probe('10.88.0.1');report['checks']['tcp_payload_both_tunnels']=True
 for index in (1,0):
  lab=labs[index];p={k:lab['ssh'][k] for k in ('user','fingerprint')};p.update(op='ssh',host='10.'+str(87+index)+'.0.1',port=2222,key_file='/sdcard/TabRemoto/imports/TabRemoto-test-usability-fixture-rsa.pem',require_vpn=True,vpn_profile=ids[index],transport='wifi',command='probe')
  before=d.command()['ssh']['revision'];provision(p,device=d);s=wait(lambda s:s['ssh']['revision']>before and s['ssh']['phase'] in (5,6));assert s['ssh']['phase']==6,s['ssh']['detail'];assert 'TabRemoto lab key authentication OK' in d.command('ssh-output')['output'];time.sleep(.3)
 report['checks']['ssh_routed_by_named_profile_inside_both_tunnels']=True
 click('Parar');wait(lambda s:not next(v for v in s['network']['tunnels'] if v['id']==ids[0])['up'])
 probe('10.87.0.1',False);probe('10.88.0.1');report['checks']['stop_one_blocks_only_its_routes_other_tunnel_stays_usable']=True
 conflict=dict(labs[0]['vpn'],profile='usability-conflict');provision(conflict,device=d)
 s=wait(lambda s:'sobrepostos' in s['network']['vpn_detail']);assert len(s['network']['tunnels'])==2 and any(v['id']==ids[1] and v['up'] for v in s['network']['tunnels']);report['checks']['conflicting_routes_rejected_without_tearing_down_active_peer']=True
 choose_file('Abrir .conf: Wi-Fi','TabRemoto-test-usability-wg0.conf');wait(lambda s:sum(v['up'] for v in s['network']['tunnels'])==2);probe('10.87.0.1');probe('10.88.0.1');report['checks']['independent_reconnect']=True
 d.command('vpn-stop');wait(lambda s:not s['network']['vpn']);probe('10.87.0.1',False);probe('10.88.0.1',False);report['checks']['stop_all_fail_closed']=True
 s=d.command();report['ui_max_tick_us']=s['ui_max_tick_us'];report['ui_slow_ticks']=s['ui_slow_ticks'];report['status']='passed'
except BaseException as e:report['status']='failed';report['error']=str(e);raise
finally:d.close();Path('artifacts/usability-ui-multivpn-device.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2),flush=True)
