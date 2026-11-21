import sys,time,json
from pathlib import Path
sys.path.insert(0,str(Path.cwd()/'tools'))
from device import Device
from provision_device import provision
d=Device();r={'status':'running','checks':{}}
def send(text):provision({'op':'ssh-input','text':text},device=d)
try:
 assert d.command()['ssh']['phase']==4
 send('TERM=xterm top -d 1\r');time.sleep(4)
 send('q');time.sleep(.5)
 output=d.command('ssh-output')['output']
 assert 'PID' in output and ('CPU' in output or 'Cpu' in output), 'No top interactive screen'
 r['checks']['interactive_top_received']=True
 send("printf '\\nTabRemoto-%s\\n' INTERACTIVE-OK\r");time.sleep(1)
 output=d.command('ssh-output')['output'];assert 'TabRemoto-INTERACTIVE-OK' in output
 r['checks']['quit_and_shell_input_returned']=True
 assert d.command('ssh-ui-status')['fullscreen']
 r['status']='passed'
finally:
 d.close();Path('artifacts/five-stages-ssh-interactive.json').write_text(json.dumps(r,indent=2)+'\n')
print(json.dumps(r,indent=2))
