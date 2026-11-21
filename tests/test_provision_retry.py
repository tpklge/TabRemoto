import sys,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from provision_device import provision
class Device:
 def __init__(self,busy=0,fail_action=False):self.busy=busy;self.fail_action=fail_action;self.log=None;self.actions=0;self.challenges=0
 def command(self,cmd,**kwargs):
  if cmd=='provision-open':
   self.challenges+=1
   if self.challenges<=self.busy:raise RuntimeError({'ok':False})
   return {'key':'00'*32}
  self.actions+=1
  if self.fail_action:raise TimeoutError('ack lost')
  return {'ok':True}
class ProvisionTests(unittest.TestCase):
 @patch('provision_device.time.sleep')
 def test_busy_challenge_retries_only_read(self,sleep):
  d=Device(busy=3);provision({'op':'noop'},device=d)
  self.assertEqual((d.challenges,d.actions),(4,1))
 @patch('provision_device.time.sleep')
 def test_action_ack_timeout_never_repeats_action(self,sleep):
  d=Device(fail_action=True)
  with self.assertRaises(TimeoutError):provision({'op':'noop'},device=d)
  self.assertEqual((d.challenges,d.actions),(1,1))
 @patch('provision_device.time.sleep')
 def test_busy_retry_bounded(self,sleep):
  d=Device(busy=100)
  with self.assertRaises(RuntimeError):provision({'op':'noop'},device=d)
  self.assertEqual((d.challenges,d.actions),(50,0))

class ReceiptDevice:
 def __init__(self,accepted=True,matching=True,read_failures=0):
  self.log=None;self.actions=0;self.reads=0;self.accepted=accepted;self.matching=matching;self.read_failures=read_failures;self.last_id=''
 def command(self,cmd,**kwargs):
  if cmd=='provision-open':return {'key':'00'*32,'receipt':True}
  if cmd=='provision-status':
   self.reads+=1
   if self.reads<=self.read_failures:raise TimeoutError('read delayed')
   return {'ok':True,'id':self.last_id if self.matching else 'ff'*16,'accepted':self.accepted}
  assert cmd.startswith('provision ')
  import json
  from cryptography.hazmat.primitives.ciphers.aead import AESGCM
  wire=bytes.fromhex(cmd.split(' ',1)[1]);body=AESGCM(bytes(32)).decrypt(wire[:12],wire[12:],b'TabRemoto provision v1');self.last_id=json.loads(body)['request_id'];self.actions+=1
  raise TimeoutError('action applied but ACK lost')
class ReceiptTests(unittest.TestCase):
 def test_matching_receipt_recovers_ack_without_resubmitting(self):
  d=ReceiptDevice();provision({'op':'noop'},device=d);self.assertEqual((d.actions,d.reads),(1,1))
 def test_rejected_receipt_propagates_rejection(self):
  d=ReceiptDevice(accepted=False)
  with self.assertRaises(RuntimeError):provision({'op':'noop'},device=d)
  self.assertEqual((d.actions,d.reads),(1,1))
 def test_foreign_receipt_is_not_evidence_of_acceptance(self):
  d=ReceiptDevice(matching=False)
  with self.assertRaises(TimeoutError):provision({'op':'noop'},device=d)
  self.assertEqual((d.actions,d.reads),(1,3))
 def test_only_read_receipt_retries_when_read_delayed(self):
  d=ReceiptDevice(read_failures=2);provision({'op':'noop'},device=d);self.assertEqual((d.actions,d.reads),(1,3))

if __name__=='__main__':unittest.main()
