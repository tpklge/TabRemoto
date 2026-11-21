#!/usr/bin/env python3
"""Verify the physical key dispatch path against a real remote raw-mode PTY."""
import sys,json,time,base64,uuid
from pathlib import Path
from device import Device
from provision_device import provision
payload=json.loads(sys.stdin.readline());log_path=Path('artifacts/device-private/usability-terminal-keys.serial.log');log_path.touch(mode=0o600,exist_ok=True);log_path.chmod(0o600);log=log_path.open('w');d=Device(log=log);report={'status':'running','checks':{}}
def wait_output(marker,seconds=30):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  output=d.command('ssh-output')['output']
  if marker in output:return output
  time.sleep(.3)
 raise TimeoutError('Remote keyboard marker did not arrive')
try:
 assert d.command()['ssh']['phase']==4 and d.command('ssh-ui-status')['fullscreen']
 expected=b'\x1bOA\x1bOB\x1bOP\x1b[21~\x1b[23~\x1b[24~\x1b[Z'
 program="""import os,sys,tty,termios,select
fd=sys.stdin.fileno();saved=termios.tcgetattr(fd);data=b''
try:
 tty.setraw(fd);sys.stdout.write('\\x1b[?1hREADY_KEYS\\n');sys.stdout.flush()
 while len(data)<27 and select.select([fd],[],[],8)[0]:data+=os.read(fd,27-len(data))
finally:
 termios.tcsetattr(fd,termios.TCSADRAIN,saved)
 sys.stdout.write('\\x1b[?1l\\nTABREMOTO_KEYS_'+data.hex()+'\\n');sys.stdout.flush()
"""
 token=uuid.uuid4().hex[:10];ready='READY_'+token;finished='TABKEYS_'+token+'_'
 program=program.replace('READY_KEYS',ready).replace('TABREMOTO_KEYS_',finished)
 command="python3 -c \"import base64;exec(base64.b64decode('"+base64.b64encode(program.encode()).decode()+"'))\"\r"
 provision({'op':'ssh-input','text':command},device=d);wait_output(ready)
 keys=[0xbb,0x3b,0xcb,0x4b,0xb1,0x81,0x01,0x8a,0x0a,0x8b,0x0b,0x8c,0x0c,0x31,0xb1,0xa0,0x20,0x31]
 provision({'op':'ui-keys','keys':keys},device=d)
 output=wait_output(finished);assert finished+expected.hex() in output
 report['checks']['application_arrows_f1_f10_f11_f12_shift_tab_actual_bytes']=True
 # The command below only starts the interactive process; F10 must quit it.
 provision({'op':'ssh-input','text':'htop\r'},device=d);time.sleep(3)
 for _ in range(12):
  state=d.command();assert state['ready'] and state['ssh']['phase']==4;time.sleep(1)
 output=d.command('ssh-output')['output'];assert 'htop: command not found' not in output
 assert 'PID' in output and ('CPU' in output or 'Mem' in output),'No interactive htop screen'
 provision({'op':'ui-keys','keys':[0xcb,0x4b,0xbb,0x3b,0xb1,0x8a,0x0a,0x31]},device=d);time.sleep(.8)
 provision({'op':'ssh-input','text':"printf '\\nTabRemoto-%s\\n' HTOP-KEYS-OK\r"},device=d)
 wait_output('TabRemoto-HTOP-KEYS-OK');report['checks']['htop_arrows_and_shift_zero_f10_quit']=True;assert 'task_wdt' not in log_path.read_text(),'Watchdog starvation during interactive rendering';report['checks']['interactive_rendering_without_watchdog']=True;report['status']='passed'
except BaseException as e:report['status']='failed';report['error']=str(e);raise
finally:d.close();log.close();Path('artifacts/usability-terminal-keys-device.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
