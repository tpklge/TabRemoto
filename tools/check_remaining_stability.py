#!/usr/bin/env python3
"""Observe real sessions and bounded memory; run logical SD recovery in own test."""
import json,time,sys
from pathlib import Path
from device import Device
from provision_device import provision
r={'status':'running','checks':{},'samples':[]};d=Device();duration=int(sys.argv[1]) if len(sys.argv)>1 else 600
try:
 initial=d.command();assert initial['network']['wifi'];assert not initial['security']['unlocked']
 assert any(t['phase']==4 and t['protocol']=='ssh' for t in initial['sessions'])
 provision({'op':'ssh-input','text':'for i in $(seq 1 3000); do printf \"STABILITY-BURST-%04d abcdefghijklmnopqrstuvwxyz\\n\" \"$i\"; done\n'},device=d)
 start=time.monotonic();end=start+duration;next_input=start+15
 while time.monotonic()<end:
  s=d.command(timeout=10)
  assert s['ready'] and s['network']['wifi'] and s['sd_mounted'] and s['resources']
  assert s['ssh']['terminal_history']<=128
  assert s['key_errors']==initial['key_errors'];assert not s['security']['unlocked']
  assert s['internal_free']>45000 and s['psram_free']>1000000
  assert any(t['phase']==4 and t['protocol']=='ssh' for t in s['sessions'])
  if initial['rdp']['phase']==3:assert s['rdp']['phase']==3
  if time.monotonic()>=next_input:
   provision({'op':'ssh-input','text':'printf "STABILITY-%d\\n" '+str(len(r['samples']))+'\n'},device=d);next_input+=15
  r['samples'].append({k:s[k] for k in ('uptime_ms','internal_free','psram_free','ui_max_tick_us','ui_slow_ticks')})
  Path('artifacts/remaining-stability-device.json').write_text(json.dumps(r,indent=2)+'\n');time.sleep(2)
 r['checks']['sessions_and_memory_stable_seconds']=round(time.monotonic()-start)
 for i in range(3):
  d.command('storage-test');generation=d.command()['sd_generation'];d.command('storage-retry');limit=time.monotonic()+30
  while time.monotonic()<limit:
   s=d.command()
   if s['sd_generation']>generation and s['resources'] and s['external_font'] and not s['sd_busy']:break
   time.sleep(.2)
  else:raise AssertionError('SD/font recovery not completed')
 r['checks']['three_real_logical_sd_remounts_and_font_reload']=True
 r['checks']['personal_vault_remained_locked']=True;r['status']='passed'
except BaseException as e:r['status']='failed';r['error']=str(e);raise
finally:d.close();Path('artifacts/remaining-stability-device.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps({'status':r['status'],'checks':r['checks'],'samples':len(r['samples'])}),flush=True)
