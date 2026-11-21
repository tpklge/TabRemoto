#!/usr/bin/env python3
"""Validate production app-only image, rejected test hooks and approved servers."""
import sys,json,time,argparse
from pathlib import Path
from device import Device
from provision_device import provision
parser=argparse.ArgumentParser();parser.add_argument('--report',default='artifacts/remaining-production-device.json');parser.add_argument('--stability-seconds',type=int,default=120);parser.add_argument('--saved-profiles',action='store_true');args=parser.parse_args()
p=json.loads(sys.stdin.readline());d=Device();r={'status':'running','checks':{}}
def wait(fn,seconds=60):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=d.command(timeout=10)
  if fn(s):return s
  time.sleep(.3)
 raise TimeoutError('Expected production state did not arrive')
try:
 s=d.command();assert s['ready'] and not s['diagnostics'];r['checks']['release_without_diagnostics']=True
 if args.saved_profiles:
  s=wait(lambda s:s['network']['profiles_ready'] and s['network']['wifi'] and s['network']['wifi_automatic'],90)
  assert s['network']['saved_wifi_count']>=1;r['checks']['production_autoconnected_saved_wifi_without_provisioning']=True
 for cmd in ('rdp-os-test','ssh-ui-probe','file-fixture','rdp-ui-status','ui-action-status','input-selftest','security-selftest','network-selftest','rdp-clipboard-read','rdp-touch 0 100 100 1','ui-layout-status'):
  try:d.command(cmd)
  except RuntimeError:continue
  raise AssertionError('Development hook available: '+cmd)
 for action in ({'op':'security-layout','page':0},{'op':'ui-capture','name':'forbidden'},{'op':'ui-click','label':'Sessoes'},{'op':'ui-pointer','samples':[{'x':900,'y':350,'pressed':True,'delay_ms':0},{'x':900,'y':350,'pressed':False,'delay_ms':100}]}):
  try:provision(action,device=d)
  except RuntimeError:pass
  else:raise AssertionError('Encrypted development UI injection available')
 r['checks']['cli_and_encrypted_test_hooks_rejected']=True
 if not s['network']['wifi']:provision(p['wifi'],device=d)
 wait(lambda s:s['network']['wifi']);provision({'op':'clock','epoch':int(time.time())},device=d)
 provision({'op':'ssh','host':'192.168.88.6','port':22,'user':'tabremoto-test','transport':'invalid'},device=d)
 wait(lambda s:s['ssh']['phase']==5 and s['ssh']['detail']=='Transporte fisico invalido');r['checks']['invalid_transport_rejected_before_dns_or_tcp']=True
 if 'ssh' in p:
  q=dict(p['ssh'],op='ssh',command='printf "TabRemoto release SSH OK\\n"');provision(q,device=d)
  wait(lambda s:s['ssh']['phase']==6);assert 'TabRemoto release SSH OK' in d.command('ssh-output')['output'];r['checks']['approved_ssh_pin_auth_and_command']=True
 d.command('rdp-open');bad=dict(p['rdp'],op='rdp',fingerprint='00'*32);provision(bad,device=d)
 s=wait(lambda s:s['rdp']['phase']==4 and s['rdp']['detail'].startswith('Certificado RDP alterado'))
 assert s['rdp']['frames']==0 and s['rdp']['fingerprint']==p['rdp']['fingerprint'];r['checks']['changed_certificate_rejected_before_authentication']=True
 time.sleep(.7);approved=dict(p['rdp'],op='rdp')
 if args.saved_profiles:approved['fingerprint']=''
 provision(approved,device=d)
 s=wait(lambda s:s['rdp']['phase']==3 and s['rdp']['frames']>0)
 assert s['rdp']['selected_protocol'] in (2,8) and s['rdp']['fingerprint']==p['rdp']['fingerprint'];r['checks']['approved_nla_pin_and_real_desktop']=True
 if args.saved_profiles:r['checks']['saved_pin_used_without_resending_or_approval']=True
 d.command('snapshot',timeout=60);minimum=s['psram_free'];internal=s['internal_free'];end=time.monotonic()+args.stability_seconds
 print('Production RDP authenticated; monitoring stability.',flush=True)
 while time.monotonic()<end:
  s=d.command();assert s['rdp']['phase']==3 and s['ready'] and not s['diagnostics'];assert not s['security']['unlocked'];minimum=min(minimum,s['psram_free']);internal=min(internal,s['internal_free']);time.sleep(1)
 r['checks']['production_stability_seconds']=args.stability_seconds;r['minimum_psram_free']=minimum;r['minimum_internal_free']=internal
 r['last']={k:s['rdp'][k] for k in ('frames','key_events','pointer_events')};r['status']='passed';d.command('snapshot',timeout=60)
except BaseException as e:r['status']='failed';r['error']=str(e);raise
finally:d.close();Path(args.report).write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2),flush=True)
