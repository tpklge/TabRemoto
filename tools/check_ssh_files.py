#!/usr/bin/env python3
"""Real Tab5 SSH/SFTP/SCP check. Private SSH provision JSON arrives on stdin.
Only creates/deletes uniquely named test files; reports no terminal/listing data.
"""
import json,sys,time,uuid
from pathlib import Path
from device import Device
from provision_device import provision
report={'status':'running','real_device':True,'checks':{}}
report_path=Path('artifacts/stage45-ssh-files.json')
def save(): report_path.write_text(json.dumps(report,indent=2)+'\n')
def command(cmd):
 d=Device()
 try:return d.command(cmd,timeout=30)
 finally:d.close()
def poll(predicate,timeout=35):
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  s=command('status')
  if predicate(s):return s
  time.sleep(.4)
 raise TimeoutError('Expected device state did not arrive')
def file_job(op,remote,local='unused'):
 before=command('status')['ssh']['revision']
 cmd=('ssh-list '+remote) if op=='list' else ('ssh-file '+op+' '+remote+' '+local)
 command(cmd)
 s=poll(lambda s:s['ssh']['revision']>before and not s['ssh']['transfer'] and s['ssh']['detail'].startswith('Transferencia'))
 if not s['ssh']['transfer_ok']:raise RuntimeError('File operation failed: '+op)
 return s
payload=json.loads(sys.stdin.readline());payload=dict(payload,command='')
remote_files=[];local_files=[]
try:
 command('ssh-disconnect');poll(lambda s:s['ssh']['phase'] in (0,5,6))
 assert command('status')['network']['wifi'], 'Wi-Fi is not connected'
 provision(payload)
 ready=poll(lambda s:s['ssh']['phase'] in (4,5,6))
 assert ready['ssh']['phase']==4,ready['ssh']['detail']
 assert ready['ssh']['fingerprint']==payload['fingerprint']
 report['checks']['password_authentication_and_host_pin']=True;save()
 file_job('list','.');report['checks']['sftp_listing']=True;save()
 source=command('file-fixture')['name'];local_files.append(source)
 original=command('file-hash '+source)
 for upload,download,label in (('put','get','sftp'),('scp-put','scp-get','scp')):
  remote='TabRemoto-test-'+uuid.uuid4().hex+'.txt';remote_files.append(remote)
  local='TabRemoto-test-'+uuid.uuid4().hex+'.txt';local_files.append(local)
  file_job(upload,remote,source)
  file_job(download,remote,local)
  copied=command('file-hash '+local)
  assert copied['sha256']==original['sha256'] and copied['bytes']==original['bytes']
  report['checks'][label+'_roundtrip_sha256']=True;save()
  file_job('rm',remote);remote_files.remove(remote)
 report['status']='passed'
except BaseException as error:
 report['status']='failed';report['error']=str(error);raise
finally:
 for remote in remote_files:
  try:file_job('rm',remote)
  except Exception:report.setdefault('pending_remote_test_files',[]).append(remote)
 for local in local_files:
  try:command('file-remove '+local)
  except Exception:report.setdefault('pending_local_test_files',[]).append(local)
 save()
print(json.dumps(report,indent=2),flush=True)
