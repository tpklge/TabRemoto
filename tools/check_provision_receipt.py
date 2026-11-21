#!/usr/bin/env python3
"""Drop only the host ACK after the real P4 dispatch; read its receipt once."""
import sys,time,json
from pathlib import Path
from device import Device
from provision_device import provision
inner=Device()
class LostAck:
 def __init__(self):self.actions=0;self.receipts=0
 @property
 def log(self):return inner.log
 @log.setter
 def log(self,value):inner.log=value
 def command(self,command,**kwargs):
  if command.startswith('provision '):
   self.actions+=1;inner.command(command,**kwargs)
   raise TimeoutError('Host deliberately discarded ACK after real dispatch')
  if command=='provision-status':self.receipts+=1
  return inner.command(command,**kwargs)
d=LostAck();r={'status':'running'}
try:
 provision({'op':'clock','epoch':int(time.time())},device=d)
 assert d.actions==1 and d.receipts==1
 receipt=inner.command('provision-status');assert len(receipt['id'])==32 and receipt['accepted']
 r={'status':'passed','checks':{'real_p4_receipt_recovers_discarded_ack_without_action_replay':True},'action_submissions':d.actions,'receipt_reads':d.receipts}
 print(json.dumps(r),flush=True)
finally:inner.close();Path('artifacts/remaining-provision-receipt-device.json').write_text(json.dumps(r,indent=2)+'\n')
