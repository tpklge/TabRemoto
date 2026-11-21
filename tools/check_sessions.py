#!/usr/bin/env python3
"""Four independent real SSH sessions plus one native RDP, using visible UI.
Private credentials arrive on stdin. No personal vault changes.
"""
import sys,json,time,uuid
from pathlib import Path
from device import Device
from provision_device import provision
p=json.loads(sys.stdin.readline());d=Device();r={'status':'running','input_method':'Raw A164 scancode injection through the real-device dispatcher','checks':{}}
def wait(fn,seconds=50):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=d.command()
  if fn(s):return s
  time.sleep(.25)
 raise TimeoutError('Session state did not arrive')
def click(label):
 provision({'op':'ui-click','label':label},device=d)
 for _ in range(50):
  result=d.command('ui-action-status')['result']
  if result==2:return
  assert result!=3,'Visible button missing: '+label
  time.sleep(.1)
 raise TimeoutError('UI action did not complete')
def next_tab():
 provision({'op':'ui-keys','keys':[0xc0,0xa0,0x20,0x40]},device=d);time.sleep(.4)
try:
 s=d.command();assert s['diagnostics'] and s['ready']
 if not s['network']['wifi']:provision(p['wifi'],device=d)
 wait(lambda s:s['network']['wifi'])
 markers=['TABREMOTO_SESSION_'+uuid.uuid4().hex[:8] for _ in range(4)]
 for index in range(4):
  if index:click('Sessoes');click('Nova sessao SSH')
  request=dict(p['ssh'],op='ssh-ui');approved=request.pop('fingerprint');request.pop('command',None)
  provision(request,device=d);s=wait(lambda s:s['ssh']['phase'] in (2,4,5))
  assert s['ssh']['phase']==2 and s['ssh']['fingerprint']==approved
  d.command('ssh-ui-trust '+approved);wait(lambda s:s['ssh']['phase']==4)
  assert d.command('ssh-ui-status')['fullscreen']
  marker=markers[index];provision({'op':'ssh-input','text':"printf '\\033[2J\\033[H"+marker[:18]+"%s\\n' "+marker[18:]+"\r"},device=d)
  end=time.monotonic()+10
  while time.monotonic()<end:
   if marker in d.command('ssh-output')['output']:break
   time.sleep(.2)
  else:raise AssertionError('SSH marker missing')
  s=d.command();assert sum(t['protocol']=='ssh' and t['phase']==4 for t in s['sessions'])==index+1
 r['checks']['four_independent_authenticated_ssh_sessions']=True
 provision({'op':'clock','epoch':int(time.time())},device=d)
 request=dict(p['rdp'],op='rdp-ui');provision(request,device=d)
 s=wait(lambda s:s['rdp']['phase'] in (2,3,4));assert s['rdp']['phase']==2 and s['rdp']['fingerprint']==p['rdp']['fingerprint']
 wait(lambda s:d.command('rdp-ui-status')['trust_visible']);d.command('rdp-trust '+p['rdp']['fingerprint'])
 s=wait(lambda s:s['rdp']['phase']==4 or s['rdp']['phase']==3 and s['rdp']['frames']>0);assert s['rdp']['phase']==3
 assert sum(t['protocol']=='ssh' and t['phase']==4 for t in s['sessions'])==4
 r['checks']['native_rdp_and_four_ssh_alive_together']=True
 next_tab();assert d.command('ssh-ui-status')['focused']
 for index in range(4):
  s=d.command();selected=next(t['id'] for t in s['sessions'] if t['protocol']=='ssh' and t['selected']);assert selected==index
  output=d.command('ssh-output')['output'];assert markers[index] in output and all(m not in output for i,m in enumerate(markers) if i!=index)
  assert s['rdp']['phase']==3
  next_tab()
 assert d.command('rdp-ui-status')['focused'];r['checks']['raw_a164_ctrl_tab_dispatch_cycles_sessions_without_mixing_terminals']=True
 next_tab();click('Desconectar');s=wait(lambda s:s['ssh']['phase'] in (5,6))
 assert sum(t['protocol']=='ssh' and t['phase']==4 for t in s['sessions'])==3 and s['rdp']['phase']==3
 r['checks']['disconnect_one_preserves_other_sessions']=True
 click('Sessoes');d.command('snapshot',timeout=60);r['checks']['session_selector_visible']=True
 active=next(t for t in s['sessions'] if t['protocol']=='ssh' and t['phase']==4)
 label='SSH #'+str(active['id']+1)+' | '+p['ssh']['user']+'@'+active['host']+' | SSH autenticado | '+active['transport']
 click(label);s=wait(lambda s:s['ssh']['phase']==4);r['checks']['resume_remaining_ssh_session']=True
 r['internal_free']=s['internal_free'];r['psram_free']=s['psram_free'];r['status']='passed'
except BaseException as e:r['status']='failed';r['error']=str(e);raise
finally:d.close();Path('artifacts/remaining-sessions-device.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2),flush=True)
