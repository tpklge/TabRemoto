#!/usr/bin/env python3
"""Real UI path: fill widgets, click Connect, confirm pin, type and inspect label.
Private quick-connect JSON arrives only on stdin. No plaintext credentials logged.
"""
import sys,json,time,base64
from pathlib import Path
from device import Device
from provision_device import provision

def capture(device,name):
 device.command('snapshot',timeout=60)
 print('Capture '+name+' saved on SD; collect /sdcard/TabRemoto/cache/screen.ppm through Launcher HTTP after stopping the app.',flush=True)
 return '/sdcard/TabRemoto/cache/screen.ppm'

if __name__=='__main__':
 p=json.loads(sys.stdin.readline());approved=p.pop('fingerprint');p['op']='ssh-ui'
 report={'status':'running','real_device':True,'checks':{}}
 path=Path('artifacts/ssh-ui-open.json' if '--connect-only' in sys.argv else 'artifacts/ssh-ui-check.json')
 def save():path.write_text(json.dumps(report,indent=2)+'\n')
 d=Device()
 def poll(fn,seconds=40):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   s=d.command()
   if fn(s):return s
   time.sleep(.25)
  raise TimeoutError('UI test state did not arrive')
 try:
  initial=poll(lambda s:s['ready'] and s['network']['wifi'])
  if '--connect-only' in sys.argv:
   d.command('ssh-disconnect');poll(lambda s:s['ssh']['phase'] in (0,5,6))
  else:
   d.command('input-selftest');focus_ok=poll(lambda s:s['input_selftest'] in (2,3))['input_selftest']==2
   report['checks']['home_and_panel_focus']=focus_ok;save()
  provision(p,device=d)
  s=poll(lambda s:s['ssh']['phase'] in (2,4,5,6))
  assert s['ssh']['phase']==2,'Expected explicit unknown-host confirmation'
  assert s['ssh']['fingerprint']==approved
  report['checks']['unknown_host_requires_confirmation']=True;save()
  end=time.monotonic()+10
  while time.monotonic()<end:
   if d.command('ssh-ui-status')['trust_dialog']:break
   time.sleep(.1)
  assert d.command('ssh-ui-status')['trust_dialog'],'Host verification dialog is not visible'
  d.command('ssh-ui-trust '+approved)
  s=poll(lambda s:s['ssh']['phase'] in (4,5,6));assert s['ssh']['phase']==4,s['ssh']['detail']
  assert d.command('ssh-ui-status')['focused'],'Terminal did not receive focus'
  assert d.command('ssh-ui-status')['fullscreen'],'Authenticated terminal did not expand'
  report['checks']['visible_connect_and_trust_controls']=True
  report['checks']['enter_handler_submits_connection']=True;save()
  if '--connect-only' not in sys.argv:
   d.command('ssh-ui-probe')
   end=time.monotonic()+15
   while time.monotonic()<end:
    rendered=d.command('ssh-ui-status')
    if rendered['rendered'] and rendered['focused']:break
    time.sleep(.25)
   assert rendered['rendered'] and rendered['focused'],'Remote marker not rendered in visible terminal'
   report['checks']['typed_command_returned_and_rendered']=True;save()
   if '--capture' in sys.argv:capture(d,'ssh-ui-connected')
  report['status']='passed' if all(report['checks'].values()) else 'failed'
 except BaseException as e:
  report.update(status='failed',error=str(e));raise
 finally:save();d.close()
 print(json.dumps(report,indent=2),flush=True)
