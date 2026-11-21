#!/usr/bin/env python3
"""Verify the real base UI, resource font and SD remounts via USB.
Input self-test injects known test text into LVGL and restores existing text;
this is not evidence of physical keyboard input or card removal.
"""
import sys,time,json
from pathlib import Path
from device import Device
root=Path(__file__).resolve().parents[1]
report={'status':'running','real_device':True,'physical_card_removal_tested':False,'cycles':[]}
def save(): (root/'artifacts/base-check.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
with (root/'artifacts/base-serial.log').open('w') as log:
 d=Device(log=log)
 try:
  def poll(predicate,timeout=40):
   end=time.monotonic()+timeout
   while time.monotonic()<end:
    try:
     s=d.command('status',timeout=4)
     if predicate(s):return s
    except TimeoutError:pass
    time.sleep(.5)
   raise TimeoutError('Expected device state did not arrive')
  initial=poll(lambda s:s['ready'])
  assert initial['resources'] and initial['external_font'],initial
  report['initial']=initial;save()
  d.command('input-selftest');check=poll(lambda s:s['input_selftest'] in (2,3))
  assert check['input_selftest']==2,check
  report['utf8_focus_editing_injected_test']=check;save()
  for i in range(3):
   before=d.command('status');d.command('storage-retry')
   after=poll(lambda s:s['sd_generation']>=before['sd_generation']+2 and not s['sd_busy'])
   assert after['sd_mounted'] and after['sd_writable'] and after['resources'] and after['external_font'],after
   d.command('storage-test')
   d.command('input-selftest');assert poll(lambda s:s['input_selftest'] in (2,3))['input_selftest']==2
   report['cycles'].append(after);save();print('Remount passed',i+1,flush=True)
  time.sleep(5);final=d.command('status')
  assert final['internal_free']>=initial['internal_free']-16384,(initial,final)
  d.command('snapshot',timeout=60)
  report['final']=final;report['status']='passed'
 except BaseException as e:report['status']='failed';report['error']=repr(e);raise
 finally:save();d.close()
print(json.dumps(report,ensure_ascii=False,indent=2))
