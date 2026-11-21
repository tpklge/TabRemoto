#!/usr/bin/env python3
"""Real LVGL pointer dispatch -> native RDP -> Windows MessageBox OK result.
Diagnostic candidate only. Credentials on stdin; --reuse-session keeps an
already authenticated desktop. Raw rotation adapts the initial test driver.
"""
import argparse,json,sys,time,uuid,math
from pathlib import Path
from device import Device
from provision_device import provision
parser=argparse.ArgumentParser();parser.add_argument('--raw-pointer-rotation',type=int,choices=(0,90),default=0);parser.add_argument('--reuse-session',action='store_true');args=parser.parse_args()
p=json.loads(sys.stdin.readline());d=Device();report={'status':'running','checks':{},'raw_pointer_rotation':args.raw_pointer_rotation};out=Path('artifacts/rdp-click-dialog-device.json')
def wait(fn,seconds=60):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=d.command(timeout=10)
  if fn(s):return s
  time.sleep(.2)
 raise TimeoutError('Device state')
def key(code,down):
 for _ in range(20):
  try:d.command('rdp-key '+hex(code)+' '+str(int(down)));time.sleep(.12);return
  except RuntimeError:time.sleep(.1)
 raise TimeoutError('Key queue')
def tap(code):key(code,1);key(code,0)
def gesture(points):
 samples=[]
 for x,y,pressed,delay in points:
  if args.raw_pointer_rotation==90:x,y=y,1279-x
  samples.append(dict(x=x,y=y,pressed=pressed,delay_ms=delay))
 provision({'op':'ui-pointer','samples':samples},device=d)
 wait(lambda s:not s['ui_pointer_busy'])
def cursor(x,y):
 ui=d.command('rdp-ui-status');dx=x-ui['pointer_x'];dy=y-ui['pointer_y']
 if not(dx or dy):return
 if dx*dx+dy*dy<=24*24:cursor(x+100,y);return cursor(x,y)
 count=max(1,math.ceil(abs(dx)/300),math.ceil(abs(dy)/200))
 for left in range(count,0,-1):
  dx=round((x-ui['pointer_x'])/left);dy=round((y-ui['pointer_y'])/left);sx=1100 if dx<0 else 750
  gesture([(sx,350,True,0),(sx+dx,350+dy,True,100),(sx+dx,350+dy,False,100)])
  ui=d.command('rdp-ui-status')
 assert (ui['pointer_x'],ui['pointer_y'])==(x,y),ui

def dialog():
 marker='T5'+uuid.uuid4().hex[:6]+':'
 cmd='powershell -nop -w hidden -c "Add-Type -AssemblyName System.Windows.Forms;$f=New-Object Windows.Forms.Form;$f.TopMost=1;$f.WindowState=2;$f.Show();$r=[Windows.Forms.MessageBox]::Show($f,\'Clique OK\',\'Tab5\');Set-Clipboard (\''+marker+'\'+$r)"'
 assert len(cmd)<260
 for mod in (0x1d,0x38,0x2a,0x36,0x15b):key(mod,0)
 tap(0x01)
 provision({'op':'rdp-clipboard-send','text':cmd},device=d);time.sleep(.6)
 key(0x15b,1);tap(0x13);key(0x15b,0);time.sleep(.8)
 key(0x1d,1);tap(0x1e);tap(0x2f);key(0x1d,0);time.sleep(.7);tap(0x1c);time.sleep(2)
 return marker

def result(marker,seconds=3):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:d.command('rdp-clipboard-fetch',timeout=4)
  except (RuntimeError,TimeoutError):pass
  time.sleep(.25)
  try:c=d.command('rdp-clipboard-read',timeout=4)
  except TimeoutError:continue
  t=c['text'].strip()
  if t.startswith(marker):
   assert t==marker+'OK','Temporary Windows dialog closed without OK'
   return True
 return False

def click_control(label):
 provision({'op':'ui-click','label':label},device=d)
 wait(lambda s:d.command('ui-action-status')['result']==2)

try:
 s=d.command();assert s['ready'] and s['diagnostics']
 if not args.reuse_session:
  d.command('rdp-disconnect-all');wait(lambda s:all(t['phase'] in (0,4,5) for t in s['sessions'] if t['protocol']=='rdp'));time.sleep(.8)
  provision(dict(p['rdp'],op='rdp-ui',fingerprint=''),device=d)
  wait(lambda s:s['rdp']['phase']==3 and s['rdp']['frames']);time.sleep(12)
 assert d.command()['rdp']['phase']==3
 marker=dialog();point=None
 for y in (400,390,380,370,410,420,430):
  for x in (680,640,700):
   cursor(x,y);gesture([(900,350,True,0),(900,350,False,120)])
   if result(marker,1.5):point=(x,y);break
  if point:break
 assert point,'Windows OK button was not activated'
 report['button_point']=point;report['checks']['lvgl_hit_testing_tap_activates_windows_ok']=True;print('Windows OK activated at',point,flush=True)
 marker=dialog();cursor(*point)
 gesture([(900,350,True,0),(913,357,True,100),(906,362,True,150),(907,355,False,230)])
 assert result(marker,15);ui=d.command('rdp-ui-status');assert (ui['pointer_x'],ui['pointer_y'])==point
 report['checks']['480ms_finger_jitter_clicks_without_cursor_drift']=True;print('Finger jitter tap activated Windows OK.',flush=True)
 marker=dialog();cursor(*point)
 gesture([(900,350,True,0),(900,350,True,200),(900,350,True,200),(900,350,False,300)])
 assert result(marker,15);report['checks']['700ms_stationary_tap_clicks']=True
 marker=dialog();cursor(*point)
 if args.raw_pointer_rotation:click_control('Clique esquerdo')
 else:gesture([(30,240,True,0),(30,240,False,120)])
 assert result(marker,15)
 report['checks']['left_click_icon_activates_windows_ok']=True
 marker=dialog();cursor(*point)
 px=72+point[0]*1208//1280+2;py=20+point[1]*679//720+4
 gesture([(px,py,True,0),(px,py,False,120)]);assert result(marker,15)
 report['checks']['tap_on_visible_cursor_reaches_desktop']=True
 assert d.command('ui-layout-status')['errors']==0
 s=d.command();report['last']={'rdp':s['rdp'],'touch':s['rdp_touch']};assert s['rdp_touch']['dropped']==0
 report['status']='passed';print('Full LVGL input and five real Windows OK activations passed.',flush=True)
except BaseException as e:report['status']='failed';report['error']=repr(e);raise
finally:d.close();out.write_text(json.dumps(report,indent=2)+'\n')
