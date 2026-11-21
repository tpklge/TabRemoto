#!/usr/bin/env python3
"""Accept private JSON from stdin, encrypt in RAM, provision once over USB.
Never prints credentials, session keys, or the supplied JSON. No persistent file.
"""
import json,os,sys,time
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from device import Device

def provision(payload, diagnostic_log=None, device=None):
 request_id=os.urandom(16).hex()
 data=json.dumps(dict(payload,request_id=request_id),separators=(',',':')).encode()
 if len(data)>1750:raise ValueError('Provisioning payload too large')
 owned=device is None
 device=device or Device()
 previous_log=device.log
 try:
  device.log=None
  # Deliberately no serial log: the one-use key must never enter test artifacts.
  # A busy crypto gate rejects only this read/challenge request; retrying it
  # cannot repeat the requested action. Never retry the encrypted action frame.
  for attempt in range(50):
   try:challenge=device.command('provision-open');break
   except RuntimeError as error:
    if not error.args or error.args[0]!={'ok':False} or attempt==49:raise
    time.sleep(.025)
  key=bytes.fromhex(challenge['key']);nonce=os.urandom(12)
  encrypted=nonce+AESGCM(key).encrypt(nonce,data,b'TabRemoto provision v1')
  device.log=diagnostic_log
  try:device.command('provision '+encrypted.hex(),timeout=30)
  except TimeoutError as lost_ack:
   if not challenge.get('receipt'):raise
   # Read-only receipt queries may repeat; never repeat the encrypted action.
   for attempt in range(3):
    try:receipt=device.command('provision-status',timeout=12)
    except (TimeoutError,RuntimeError):continue
    if receipt.get('id')!=request_id:continue
    if not receipt.get('accepted'):raise RuntimeError({'ok':False}) from lost_ack
    return
   raise lost_ack
 finally:
  device.log=previous_log
  if owned:device.close()
if __name__=='__main__':
 payload=json.loads(sys.stdin.readline());provision(payload);print('Private configuration accepted (RAM only)',flush=True)
