#!/usr/bin/env python3
"""Restore this test's public manifest using Launcher USB disk mode on macOS.
Requires pyusb 1.3.1 and libusb, plus the adjacent Launcher tools. Never formats
or flashes. Accepts only an unchanged manifest or the exact test corruption.
"""
import argparse,hashlib,json,os,plistlib,subprocess,sys,tempfile,time
from pathlib import Path
from device import find_port
from package_release import validate_manifest
ROOT=Path(__file__).resolve().parents[1]
def disks():return plistlib.loads(subprocess.check_output(['diskutil','list','-plist']))['AllDisksAndPartitions']
def recover(pyusb_path=None):
 if sys.platform!='darwin':raise RuntimeError('This USB disk helper requires macOS')
 if pyusb_path:sys.path.insert(0,str(pyusb_path))
 import usb.core,usb.util
 validate_manifest(ROOT/'sdcard/TabRemoto')
 sys.path.insert(0,str(ROOT.parent/'Tab5LauncherPaludo/tools'))
 from tab5_usb import Tab5
 previous={d['DeviceIdentifier'] for d in disks()}
 device=Tab5(find_port(),timeout=10)
 try:
  device.command('status')
  device.command('usb-disk','confirm')
 finally:device.close()
 target_disk=None;mount=None
 deadline=time.monotonic()+45
 while time.monotonic()<deadline:
  found=[d for d in disks() if d['DeviceIdentifier'] not in previous]
  if len(found)>1:raise RuntimeError('Multiple new disks; no disk selected or modified')
  if found:
   disk=found[0]
   parts=disk.get('Partitions',[disk])
   mounted=[p.get('MountPoint') for p in parts if p.get('MountPoint')]
   if len(mounted)==1:target_disk=disk['DeviceIdentifier'];mount=Path(mounted[0]);break
  time.sleep(1)
 if not target_disk:raise TimeoutError('Tab5 SD volume not mounted; no file modified')
 units=list(usb.core.find(find_all=True,idVendor=0x303a,idProduct=0x4002))
 if len(units)!=1 or usb.util.get_string(units[0],units[0].iProduct)!='Tab5 microSD':raise RuntimeError('Tab5 USB identity mismatch; no file modified')
 info=plistlib.loads(subprocess.check_output(['diskutil','info','-plist',target_disk]))
 if info['BusProtocol']!='USB' or info['Internal']:raise RuntimeError('Unexpected disk; no file modified')
 report={'disk':target_disk,'usb_product':'Tab5 microSD'}
 try:
  target=mount/'TabRemoto/manifest.json';original=(ROOT/'sdcard/TabRemoto/manifest.json').read_bytes()
  expected=json.loads(original);existing=json.loads(target.read_bytes())
  if existing!=expected:
   if existing.get('files',{}).get('assets/themes/dark.json')!='0'*64:raise RuntimeError('Manifest differs from this test fixture; preserved')
   existing['files']['assets/themes/dark.json']=expected['files']['assets/themes/dark.json']
   if existing!=expected:raise RuntimeError('Unexpected manifest differences; preserved')
   f=tempfile.NamedTemporaryFile(dir=target.parent,prefix='manifest-recovery-',delete=False)
   try:
    f.write(original);f.flush();os.fsync(f.fileno());f.close();os.replace(f.name,target)
   finally:
    if not f.closed:f.close()
  if target.read_bytes()!=original:raise RuntimeError('Manifest readback mismatch')
  report.update(manifest_restored=True,bytes=len(original),sha256=hashlib.sha256(original).hexdigest())
 finally:
  subprocess.run(['diskutil','eject','/dev/'+target_disk],check=True)
  units[0].ctrl_transfer(0x40,0x5a,0x5442,0,None,timeout=3000)
 (ROOT/'artifacts/usb-manifest-recovery.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report,indent=2))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pyusb-path',type=Path);a=p.parse_args();recover(a.pyusb_path)
