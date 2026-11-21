import sys,json,time,os
from pathlib import Path
sys.path.insert(0,str(Path.cwd()/'tools'))
from device import Device
from provision_device import provision
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--report',default='artifacts/five-stages-native-rdp-check.json');parser.add_argument('--ui',action='store_true');args=parser.parse_args()
payload=json.loads(sys.stdin.readline());private=Path('artifacts/device-private')/(Path(args.report).stem+'.serial.log');private.touch(mode=0o600,exist_ok=True);private.chmod(0o600)
report={'status':'running','from':'Tab5 native FreeRDP','checks':{}}
output=Path(args.report)
with private.open('w') as log:
 d=Device(log=log)
 try:
  s=d.command();assert s['ready'];report['checks']['device_ready']=True
  if not s['network']['wifi']:
   provision(payload['wifi'],device=d)
   end=time.monotonic()+40
   while time.monotonic()<end:
    s=d.command()
    if s['network']['wifi']:break
    time.sleep(.5)
  assert s['network']['wifi'];report['checks']['wifi']=True
  provision({'op':'clock','epoch':int(time.time())},device=d)
  d.command('rdp-open');payload['rdp']['op']='rdp-ui' if args.ui else 'rdp'
  try:provision(payload['rdp'],diagnostic_log=log,device=d)
  except TimeoutError:
   observed=d.command().get('rdp',{})
   if observed.get('phase',0)==0:raise
   report.setdefault('collector_issues',[]).append('USB provision acknowledgement timed out; native backend state confirmed acceptance; no retry')
  started=time.monotonic();end=started+90
  previous=None
  while time.monotonic()<end:
   s=d.command(timeout=8);r=s['rdp'];state=(r['phase'],r['error'],r['frames'])
   if state!=previous:print(json.dumps({'rdp':r,'internal_free':s['internal_free'],'psram_free':s['psram_free']}),flush=True);previous=state
   if args.ui and r['phase']==2:
    assert r['fingerprint']==payload['rdp']['fingerprint'],'Unexpected RDP certificate'
    ui=d.command('rdp-ui-status')
    if ui['trust_visible']:
     d.command('rdp-trust '+r['fingerprint']);report['checks']['visible_trust_control']=True
   report['last']=r;output.write_text(json.dumps(report,indent=2)+'\n')
   if r['phase'] in (4,5) and time.monotonic()-started>2:break
   if r['phase']==3 and r['frames']>0:
    report['checks']['authenticated']=True;report['checks']['desktop_received']=True;break
   time.sleep(.5)
  assert r['phase']==3 and r['frames']>0,r
  assert r['fingerprint']==payload['rdp']['fingerprint'];report['checks']['certificate_matches_approved_pin']=True
  assert r['selected_protocol'] in (2,8),'Server did not select NLA'
  report['checks']['nla_selected']=True
  if args.ui:
   ui=d.command('rdp-ui-status');assert ui['rendered'] and ui['focused'];report['checks']['desktop_rendered_and_focused']=True
  d.command('snapshot',timeout=60);report['status']='passed'
 except BaseException as e:report['status']='failed';report['error']=str(e);raise
 finally:output.write_text(json.dumps(report,indent=2)+'\n');d.close()
