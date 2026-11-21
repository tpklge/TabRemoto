#!/usr/bin/env python3
"""Inspect the real LVGL home/forms without unlocking or writing the personal vault."""
import json,time,uuid
from pathlib import Path
from device import Device
from provision_device import provision
capture_run=uuid.uuid4().hex[:6]
r={'status':'running','checks':{},'captures':[]};d=Device()
def click(label):
 provision({'op':'ui-click','label':label},device=d)
 for _ in range(50):
  result=d.command('ui-action-status')['result']
  if result==2:return
  if result==3:raise AssertionError('Visible control absent: '+label)
  time.sleep(.05)
 raise TimeoutError('UI callback did not run')
def layout(name):
 time.sleep(.15)
 errors=d.command('ui-layout-status')['errors']
 assert not d.command()['security']['unlocked'],'Personal vault must stay locked'
 print('Checking real layout:',name,flush=True)
 capture=name+'-'+capture_run
 provision({'op':'ui-capture','name':capture},device=d);r['captures'].append(capture)
 assert errors==0,(name,errors)
 r['checks'][name]=True
try:
 s=d.command();assert s['ready'] and s['diagnostics'] and not s['security']['unlocked']
 started=s['uptime_ms']
 layout('home')
 d.command('input-selftest')
 deadline=time.monotonic()+35
 while time.monotonic()<deadline:
  s=d.command()
  if s['input_selftest'] in (2,3):break
  time.sleep(.2)
 assert s['input_selftest']==2,s['input_selftest_detail'];r['checks']['keyboard_event_dispatch_focus_masks_and_modal_isolation']=True
 for i,name in enumerate(('cofre','conexoes','identidades','credenciais','confianca','importar')):
  provision({'op':'security-layout','page':i},device=d);layout(name)
  if i==1:
   click('SSH');layout('seletor-protocolo');click('RDP');layout('protocolo-rdp');click('RDP');click('Cancelar');layout('cancelar-seletor')
 click('Voltar');layout('home-retorno')
 d.command('security-open');time.sleep(.3);click('Conexoes');layout('cofre-bloqueado');click('Desbloquear cofre');layout('desbloquear');click('Voltar')
 d.command('snapshot',timeout=60)
 r['checks']['normal_and_encrypted_capture_paths']=True
 assert d.command()['uptime_ms']>=started
 r['checks']['repeated_render_captures_without_reset']=True
 r['status']='passed'
except BaseException as e:r['status']='failed';r['error']=str(e);raise
finally:d.close();Path('artifacts/launcher-ui-device.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2),flush=True)
