#!/usr/bin/env python3
"""Exercise real Windows clipboard text and display control through Tab5 RDP."""
import sys,time,json,uuid,base64
from pathlib import Path
from device import Device
from provision_device import provision
r={'status':'running','checks':{}};d=Device()
def wait(fn,seconds=30):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=d.command()
  if fn(s):return s
  time.sleep(.2)
 raise TimeoutError('Expected RDP feature state did not arrive')
def click(label):
 provision({'op':'ui-click','label':label},device=d)
 for _ in range(50):
  code=d.command('ui-action-status')['result']
  if code==2:return
  assert code!=3,'Visible button missing: '+label;time.sleep(.1)
 raise TimeoutError('UI action did not complete')
def key(code,down):
 for _ in range(30):
  try:d.command('rdp-key '+hex(code)+' '+str(int(down)),timeout=45);time.sleep(.12);return
  except RuntimeError:time.sleep(.05)
 raise RuntimeError('RDP input queue remained unavailable')
def tap(code):key(code,True);key(code,False)
def run_clipboard_command(command):
 provision({'op':'rdp-clipboard-send','text':command},device=d)
 time.sleep(.3)
 key(0x15b,True);tap(0x13);key(0x15b,False);time.sleep(.7)
 key(0x1d,True);tap(0x1e);key(0x1d,False)
 key(0x1d,True);tap(0x2f);key(0x1d,False)
 time.sleep(.5);tap(0x1c)
try:
 s=wait(lambda s:s['rdp']['phase']==3 and s['rdp_clipboard'] and s['rdp_dynamic_resize']);r['checks']['server_negotiated_cliprdr_and_disp']=True
 for modifier in (0x2a,0x36,0x1d,0x15b):key(modifier,False)
 marker='TabRemoto-'+uuid.uuid4().hex[:8]+' áçü😀';suffix='-ROUNDTRIP'
 run_clipboard_command("powershell -NoProfile -Command \"Set-Clipboard -Value '"+marker+suffix+"'\"")
 end=time.monotonic()+30;last_received=0
 while time.monotonic()<end:
  try:d.command('rdp-clipboard-fetch')
  except RuntimeError:pass
  time.sleep(.3);clip=d.command('rdp-clipboard-read')
  if clip['text'].rstrip('\r\n')==marker+suffix and clip['sent']>0:break
 else:raise AssertionError('Windows clipboard roundtrip marker not received')
 r['checks']['unicode_text_tab5_to_windows_and_back']=True;r['clipboard_sent']=clip['sent'];r['clipboard_received']=clip['received']
 click('Resolucao');click('960x540');s=wait(lambda s:s['rdp_width']==960 and s['rdp_height']==540 and s['rdp']['phase']==3);r['checks']['visible_resolution_selector_changes_remote_desktop']=True;time.sleep(2)
 click('Resolucao');click('800x450');wait(lambda s:s['rdp_width']==800 and s['rdp_height']==450 and s['rdp']['phase']==3);r['checks']['second_live_resize']=True;time.sleep(2)
 click('Resolucao');click('1280x720');wait(lambda s:s['rdp_width']==1280 and s['rdp_height']==720 and s['rdp']['phase']==3);r['checks']['return_to_original_resolution']=True
 click('Clipboard');click('Receber do remoto');time.sleep(.5);click('Fechar');r['checks']['visible_clipboard_controls_open_fetch_close']=True
 d.command('snapshot',timeout=60);r['status']='passed'
except BaseException as e:r['status']='failed';r['error']=str(e);raise
finally:d.close();Path('artifacts/remaining-rdp-clipboard-device.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2),flush=True)
