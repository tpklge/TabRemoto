#!/usr/bin/env python3
"""Disposable byte-for-byte fault fixture for the single approved RDP server."""
import socket,threading,time,json,sys
from pathlib import Path
host,port,folder=sys.argv[1],int(sys.argv[2]),Path(sys.argv[3]);folder.mkdir(mode=0o700,parents=True,exist_ok=True)
state={'accepted':0,'faults':0};active=[];lock=threading.Lock()
def save():
 (folder/'state.json').write_text(json.dumps(state))
def copy(a,b):
 try:
  while True:
   data=a.recv(32768)
   if not data:break
   b.sendall(data)
 except OSError:pass
 finally:
  for s in (a,b):
   try:s.shutdown(socket.SHUT_RDWR)
   except OSError:pass
   s.close()
def faults():
 while True:
  marker=folder/'drop'
  if marker.exists():
   marker.unlink()
   with lock:
    state['faults']+=1
    for s in active:
     try:s.shutdown(socket.SHUT_RDWR)
     except OSError:pass
    active.clear();save()
  time.sleep(.1)
threading.Thread(target=faults,daemon=True).start();server=socket.socket();server.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);server.bind((host,port));server.listen(4);save();print('Transparent RDP fault fixture ready',flush=True)
while True:
 c,_=server.accept()
 try:t=socket.create_connection(('192.168.88.102',3389),timeout=10);t.settimeout(None)
 except OSError:c.close();continue
 with lock:state['accepted']+=1;active.extend((c,t));save()
 threading.Thread(target=copy,args=(c,t),daemon=True).start();threading.Thread(target=copy,args=(t,c),daemon=True).start()
