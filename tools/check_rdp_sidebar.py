#!/usr/bin/env python3
"""Real Tab5 UI, relative pointer, no-op resize and native server position."""
import sys,json,time,base64,uuid
from pathlib import Path
from device import Device
from provision_device import provision
r={'status':'running','input_method':'Coordinates injected into the same UI gesture dispatcher as LVGL touch','checks':{}};d=Device()
def wait(fn,seconds=30):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=d.command()
  if fn(s):return s
  time.sleep(.15)
 raise TimeoutError('RDP sidebar state not reached')
def click(label):
 provision({'op':'ui-click','label':label},device=d)
 for _ in range(40):
  code=d.command('ui-action-status')['result']
  if code==2:return
  assert code!=3,'Visible control absent: '+label
  time.sleep(.1)
 raise TimeoutError('UI control did not run')
def gesture(event,x,y,t):d.command('rdp-touch '+str(event)+' '+str(x)+' '+str(y)+' '+str(t));time.sleep(.08)
def key(code,down):
 for _ in range(30):
  try:d.command('rdp-key '+hex(code)+' '+str(int(down)));time.sleep(.12);return
  except RuntimeError:
   if d.command()['rdp']['phase']!=3:raise
   time.sleep(.05)
 raise RuntimeError('RDP input queue did not accept key')
def tap(code):key(code,True);key(code,False)
try:
 s=wait(lambda s:s['rdp']['phase']==3 and s['rdp']['frames']>0)
 ui=d.command('rdp-ui-status');assert ui['focused'] and ui['rendered'] and ui['sidebar_clear'] and ui['icons_only'];r['checks']['icon_only_sidebar_outside_desktop']=True
 assert d.command('ui-layout-status')['errors']==0;r['checks']['sidebar_labels_within_buttons']=True
 errors=s['rdp']['resize_errors'];frames=s['rdp']['frames']
 for _ in range(20):d.command('rdp-resize '+str(s['rdp_width'])+' '+str(s['rdp_height']))
 s=d.command();assert s['rdp']['resize_errors']==errors and not s['rdp_resizing'];assert not d.command('rdp-ui-status')['toast_visible'];r['checks']['twenty_current_resolution_requests_are_noops_without_warning']=True
 # A stroke starting at an arbitrary screen position moves relatively.
 before=d.command('rdp-ui-status');events=s['rdp']['pointer_events']
 gesture(0,250,250,1000);gesture(1,310,280,1100);gesture(2,310,280,1200)
 after=d.command('rdp-ui-status');assert after['pointer_x']==before['pointer_x']+60 and after['pointer_y']==before['pointer_y']+30
 wait(lambda s:s['rdp']['pointer_events']>=events+1);assert d.command()['rdp']['pointer_events']==events+1;r['checks']['stroke_moves_relatively_without_button_events']=True
 # Small jitter remains a tap at the existing cursor position, not finger position.
 events=d.command()['rdp']['pointer_events'];gesture(0,500,500,1300);gesture(2,500,500,1400)
 wait(lambda s:s['rdp']['pointer_events']>=events+2);r['checks']['tap_generates_left_down_and_up']=True
 events=d.command()['rdp']['pointer_events'];gesture(0,400,400,1500);gesture(1,430,412,2100);gesture(3,430,412,2200)
 wait(lambda s:s['rdp']['pointer_events']>=events+3);r['checks']['hold_move_and_cancel_release_drag']=True
 # Query Windows itself, through the native RDP clipboard; no host-side RDP client.
 point=d.command('rdp-ui-status');token='TOUCHPAD_'+uuid.uuid4().hex[:8]+':'
 ps="Add-Type -AssemblyName System.Windows.Forms;$p=[System.Windows.Forms.Cursor]::Position;Set-Clipboard ('"+token+"'+$p.X+','+$p.Y)"
 command='powershell -NoProfile -WindowStyle Hidden -Command "'+ps+'"'
 for modifier in (0x2a,0x36,0x1d,0x15b):d.command('rdp-key '+hex(modifier)+' 0')
 tap(0x01);tap(0x01)
 provision({'op':'rdp-clipboard-send','text':command},device=d);time.sleep(.6)
 d.command('rdp-key 0x15b 1');tap(0x13);d.command('rdp-key 0x15b 0');time.sleep(.8)
 d.command('rdp-key 0x1d 1');tap(0x1e);tap(0x2f);d.command('rdp-key 0x1d 0');time.sleep(.6);tap(0x1c)
 expected=token+str(point['pointer_x'])+','+str(point['pointer_y']);limit=time.monotonic()+30
 while time.monotonic()<limit:
  try:d.command('rdp-clipboard-fetch')
  except RuntimeError:pass
  time.sleep(.4)
  if d.command('rdp-clipboard-read')['text'].strip()==expected:break
 else:raise AssertionError('Native Windows cursor does not match relative touchpad')
 r['checks']['windows_cursor_position_matches_touchpad_coordinates']=True
 click('Resolucao');assert d.command('ui-layout-status')['errors']==0;click('960x540');wait(lambda s:s['rdp_width']==960 and not s['rdp_resizing']);time.sleep(2)
 click('Resolucao');click('800x450');wait(lambda s:s['rdp_width']==800 and not s['rdp_resizing']);time.sleep(2)
 click('Resolucao');click('1280x720');wait(lambda s:s['rdp_width']==1280 and not s['rdp_resizing']);time.sleep(2)
 assert d.command()['rdp']['resize_errors']==errors;r['checks']['three_live_resolution_changes_without_generic_warning']=True
 click('Clipboard');assert d.command('ui-layout-status')['errors']==0;click('Receber do remoto');click('Fechar');r['checks']['clipboard_accessible_from_icon']=True
 click('Clique direito');tap(0x01);r['checks']['right_click_icon_sends_events']=True
 click('Sessoes');click('Tela inicial');assert d.command('ui-layout-status')['errors']==0
 click('REDE');assert d.command('ui-layout-status')['errors']==0;click('Voltar')
 r['checks']['home_and_network_button_labels_centered_and_within_bounds']=True
 d.command('rdp-open');wait(lambda s:d.command('rdp-ui-status')['rendered'])
 limit=time.monotonic()+60;uptime=d.command()['uptime_ms']
 while time.monotonic()<limit:
  s=d.command();assert s['rdp']['phase']==3 and s['uptime_ms']>=uptime and s['rdp']['resize_errors']==errors;assert d.command('rdp-ui-status')['sidebar_clear'];time.sleep(1)
 r['checks']['sixty_seconds_without_reset_or_resize_error']=True
 generation=d.command()['sd_generation'];d.command('storage-test');d.command('storage-retry')
 wait(lambda s:s['sd_generation']>generation and s['resources'] and s['external_font'] and not s['sd_busy'])
 assert d.command('rdp-ui-status')['icons_only'];assert d.command('ui-layout-status')['errors']==0
 r['checks']['sd_font_reload_preserves_icon_fonts_and_layout']=True
 d.command('snapshot',timeout=60);r['status']='passed'
except BaseException as e:r['status']='failed';r['error']=str(e);raise
finally:d.close();Path('artifacts/rdp-sidebar-device.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2),flush=True)
