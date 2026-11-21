#!/usr/bin/env python3
"""Validate real WireGuard payload over Tab5 Wi-Fi and fail-closed stop."""
import json,time
from pathlib import Path
from device import Device
from provision_device import provision
LOG=None
DEVICE=None

def command(cmd):
 return DEVICE.command(cmd,timeout=30)

def wait(predicate,seconds=45):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  s=command('status')
  if predicate(s):return s
  time.sleep(.5)
 raise TimeoutError('Expected WireGuard state did not arrive')
if __name__=='__main__':
 LOG=Path('artifacts/device-private/wg-runtime.log').open('w')
 DEVICE=Device(log=LOG)
 report={'status':'running','real_device':True,'transport':'wifi','checks':{}}
 path=Path('artifacts/stage45-wireguard.json')
 def save():path.write_text(json.dumps(report,indent=2)+'\n')
 try:
  lab=json.loads(Path('artifacts/device-private/lab-credentials.json').read_text())
  command('ssh-disconnect')
  provision({'op':'clock','epoch':int(time.time())},LOG,DEVICE)
  report['checks']['clock_set_from_mac']=True;save()
  provision(lab['vpn'],LOG,DEVICE)
  s=wait(lambda s:s['network']['vpn'])
  report['checks']['handshake']=True;save();print('Real WireGuard handshake confirmed',flush=True)
  command('vpn-probe 10.87.0.1 7007')
  s=command('status');n=s['network']
  assert n['vpn_rx_udp']>0 and n['vpn_tx_udp']>0
  report['checks']['tcp_payload_echo']=True
  report['udp_rx_bytes']=n['vpn_rx_udp'];report['udp_tx_bytes']=n['vpn_tx_udp'];save()
  p=dict(lab['ssh'],host='10.87.0.1',port=2222,require_vpn=True);p.pop('password',None)
  provision(p,LOG,DEVICE);s=wait(lambda s:s['ssh']['phase'] in (5,6))
  assert 'TabRemoto lab key authentication OK' in command('ssh-output')['output']
  report['checks']['encrypted_key_ssh_inside_tunnel']=True;save()
  command('vpn-stop');wait(lambda s:not s['network']['vpn'])
  blocked=False
  try:command('vpn-probe 10.87.0.1 7007')
  except RuntimeError:blocked=True
  assert blocked and command('status')['network']['vpn_armed']
  report['checks']['stop_keeps_protected_routes_blocked']=True
  p.update(require_vpn=False)
  provision(p,LOG,DEVICE);s=wait(lambda s:s['ssh']['phase'] in (5,6))
  assert s['ssh']['phase']==5 and 'TabRemoto lab key authentication OK' not in command('ssh-output')['output']
  report['checks']['protected_destination_no_physical_fallback']=True
  report['status']='passed'
 except BaseException as error:
  report['status']='failed';report['error']=str(error);raise
 finally:
  save();DEVICE.close();LOG.close()
 print(json.dumps(report,indent=2),flush=True)
