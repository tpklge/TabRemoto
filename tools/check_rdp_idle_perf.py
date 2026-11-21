#!/usr/bin/env python3
"""Measure idle display flushes and RDP copies on the real device."""
import json, sys, time, argparse
from pathlib import Path
from device import Device
from provision_device import provision
parser = argparse.ArgumentParser()
parser.add_argument('--report', required=True)
parser.add_argument('--connect', action='store_true')
args = parser.parse_args()
p = json.loads(sys.stdin.readline())
d = Device()
def wait(predicate, seconds=90):
    deadline = time.monotonic()+seconds
    while time.monotonic() < deadline:
        s=d.command(timeout=8)
        if predicate(s): return s
        time.sleep(.3)
    raise TimeoutError('Real device state')
try:
    wait(lambda s:s['ready'])
    if args.connect:
        if not d.command()['network']['wifi']:
            provision(p['wifi'],device=d)
        wait(lambda s:s['network']['wifi'])
        provision({'op':'clock','epoch':int(time.time())},device=d)
        d.command('rdp-open')
        provision(dict(p['rdp'],op='rdp'),device=d)
    wait(lambda s:s['rdp']['phase']==3 and s['rdp']['frames'])
    time.sleep(5)
    before=d.command();time.sleep(12);after=d.command()
    r={'status':'passed','seconds':(after['uptime_ms']-before['uptime_ms'])/1000,
       'before':before,'after':after}
    for field in ('flushes','ui_flushes','ui_slow_ticks','rdp_copied_pixels','rdp_paint_calls'):
        if field in after:r[field+'_delta']=after[field]-before[field]
    Path(args.report).write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({k:v for k,v in r.items() if k not in ('before','after')}),flush=True)
finally:d.close()
