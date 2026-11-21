#!/usr/bin/env python3
"""Install/run TabRemoto via LauncherdoPaludo, preserving all existing apps.

USB authenticates the launcher; its authenticated HTTP API transfers files.
Only the app-only image is installed. No esptool writes, NVS or C6 updates.
"""
import argparse
import hashlib
import http.cookiejar
import json
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path
from device import Device, find_port
from package_release import validate_manifest

ROOT=Path(__file__).resolve().parents[1]

class Launcher:
    def __init__(self, port=None, tools_path=None):
        self.port=port or find_port()
        sys.path.insert(0,str(tools_path or ROOT.parent/'Tab5LauncherPaludo/tools'))
        from tab5_usb import Tab5
        device=Tab5(self.port,timeout=10)
        try:
            self.status=device.command('status')
            self.auth=device.command('web-start')
        finally:device.close()
        if not self.auth.get('ip') or not self.auth.get('code'):
            raise RuntimeError('Launcher Wi-Fi/API not available')
        # Exchange the short pairing code for the Launcher's RAM-only cookie.
        # A long backup/install must not depend on the pairing-code lifetime.
        self.http=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
        self.paired=False
        try:self.request('/api/session',{})
        except urllib.error.HTTPError as error:
            if error.code!=401:raise
            # web-start returns the existing code even after it expires. Restart
            # only the HTTP service over USB; flash operations run independently.
            device=Tab5(self.port,timeout=10)
            try:device.command('web-stop');self.auth=device.command('web-start')
            finally:device.close()
            self.request('/api/session',{})
        self.paired=True
        print('Launcher authenticated over USB',file=sys.stderr,flush=True)

    def request(self, path, body=None, raw=False):
        headers={'X-Launcher-Client':'web'}
        if not self.paired:headers['X-Launcher-Token']=self.auth['code']
        if isinstance(body,dict):body=json.dumps(body).encode();headers['Content-Type']='application/json'
        elif body is not None:headers['Content-Type']='application/octet-stream'
        req=urllib.request.Request('http://'+self.auth['ip']+path,data=body,headers=headers)
        with self.http.open(req,timeout=120 if body is not None else 20) as response:
            data=response.read()
        return data if raw else json.loads(data)

    def _recover_network_after_install(self, slot):
        """Recover a completed app-only install whose Launcher C6 failed.
        Only called while monitoring an authorized install/restart. Boot itself
        validates the descriptor and refuses while the installer is busy.
        """
        from tab5_usb import Tab5
        usb=Tab5(self.port,timeout=8)
        try:
            status=usb.command('status')
            if status.get('wifiState')!=10 or status.get('uptimeUs',0)<20_000_000:return False
            usb.command('boot',slot)
        except (OSError,RuntimeError,TimeoutError):return False
        finally:usb.close()
        time.sleep(10)
        app=Device(self.port)
        try:
            state=app.command(timeout=8)
            if state.get('app')!='TabRemoto' or not state.get('ready'):return False
            app.command('reboot')
        finally:app.close()
        time.sleep(14)
        print('Launcher network recovered with a clean app/Launcher cycle; no C6 firmware writes',file=sys.stderr,flush=True)
        return True

    def get_file(self,path):
        return self.request('/api/file?'+urllib.parse.urlencode({'path':path}),raw=True)

    def upload(self,local,remote,only_missing=False,data=None):
        if data is None:data=local.read_bytes()
        try:
            current=self.get_file(remote)
            if current==data:return {'unchanged':True}
            if only_missing:raise RuntimeError('Existing app configuration differs; preserved: '+remote)
        except urllib.error.HTTPError as e:
            if e.code!=404:raise
        target=Path(remote)
        for attempt in range(3):
            try:
                self.request('/api/upload?'+urllib.parse.urlencode({'directory':str(target.parent),'relative':target.name}),data)
                break
            except (OSError,urllib.error.URLError) as error:
                if isinstance(error,urllib.error.HTTPError) and error.code not in (409,503):raise
                if attempt==2:raise
                time.sleep(2)
                try:
                    if self.get_file(remote)==data:break
                except urllib.error.HTTPError as missing:
                    if missing.code!=404:raise

        if self.get_file(remote)!=data:raise RuntimeError('Upload readback mismatch: '+remote)
        print('Upload verified: '+remote,file=sys.stderr,flush=True)
        return {'path':remote,'sha256':hashlib.sha256(data).hexdigest(),'verified':True}

    def operation(self,request,allow_restart=False):
        plan=self.request('/api/operation',request)
        if not plan.get('ok') or not plan.get('ticket'):raise RuntimeError(plan)
        print(plan.get('message','Operation planned'),file=sys.stderr,flush=True)
        if plan.get('willRestart') and not allow_restart:raise RuntimeError('Unexpected restart plan: '+str(plan))
        result=self.request('/api/operation',{'action':'execute','ticket':plan['ticket']})
        if not result.get('ok'):raise RuntimeError(result)
        deadline=time.monotonic()+600
        resumed=False
        restart_seen=False
        network_recovered=False
        target=re.search(r"no slot (\d+) ",plan.get("message",""))
        previous_progress=None
        invalid_since=None
        while time.monotonic()<deadline:
            try:state=self.request('/api/operation')
            except (OSError,urllib.error.URLError) as error:
                if isinstance(error,urllib.error.HTTPError) and error.code not in (409,503) and not (allow_restart and error.code==401):raise
                # Flash erase/backup can stall HTTP while its worker continues.
                # Retry only this GET, never submit another execute request.
                if not allow_restart:
                    time.sleep(2);continue
                time.sleep(2)
                try:self.__init__(self.port);restart_seen=True
                except (OSError,RuntimeError,TimeoutError):
                    if not network_recovered and target and getattr(self,'status',{}).get('wifiState')==10:
                        network_recovered=self._recover_network_after_install(int(target.group(1)))
                    continue
                continue
            progress=(state.get('percent'),state.get('message'))
            if progress!=previous_progress:
                print(str(progress),file=sys.stderr,flush=True);previous_progress=progress
            if not state.get('running'):
                if state.get('error'):raise RuntimeError(state)
                try: apps=self.request('/api/apps') if allow_restart else []
                except urllib.error.HTTPError as e:
                    if allow_restart and e.code==409:time.sleep(2);continue
                    raise
                # Automatic pending install runs after reboot, independently of
                # the web worker. An allocated but invalid image is transitional:
                # do not start another plan while it is being populated.
                if allow_restart and any(not a.get('descriptorValid') for a in apps):
                    if invalid_since is None:invalid_since=time.monotonic()
                    # A reserved empty slot can outlive the automatic installer.
                    # Keep the settling window, then inspect a new read-only plan;
                    # the branch below requires an empty slot and no layout change.
                    if time.monotonic()-invalid_since<60:
                        time.sleep(2);continue
                else:invalid_since=None
                if allow_restart and not any(a.get('name')=='TabRemoto' for a in apps):
                    if not restart_seen:
                        try:self.__init__(self.port)
                        except (OSError,RuntimeError,TimeoutError):time.sleep(2);continue
                        if self.status.get('uptimeUs',0)<20_000_000:
                            time.sleep(2);continue
                        restart_seen=True
                    # Some launcher revisions allocate/reboot but leave the
                    # staged app waiting for a second install confirmation.
                    # Reconcile the actual empty target before that confirmation.
                    if restart_seen and not resumed and request.get('action')=='install':
                        next_plan=self.request('/api/operation',request)
                        # The automatic installer owns a separate busy flag even
                        # when the web operation looks idle. A busy plan is not
                        # permission to submit another execute request.
                        if not next_plan.get('ok') and next_plan.get('message') in ('Operacao em andamento','Instalacao em andamento'):
                            time.sleep(2);continue
                        match=re.search(r'no slot (\d+) ',next_plan.get('message',''))
                        apps=self.request('/api/apps')
                        if any(a.get('name')=='TabRemoto' and a.get('descriptorValid') for a in apps):
                            time.sleep(1);continue
                        occupied={a['slot'] for a in apps if a.get('descriptorValid')}
                        if not next_plan.get('ok') or next_plan.get('willRestart') or not match or int(match.group(1)) in occupied:
                            raise RuntimeError('Pending installation requires inspection: '+str(next_plan))
                        result=self.request('/api/operation',{'action':'execute','ticket':next_plan['ticket']})
                        if not result.get('ok'):raise RuntimeError(result)
                        resumed=True
                    time.sleep(1);continue
                return {'plan':plan['message'],'result':state}
            time.sleep(1)
        raise TimeoutError('Launcher operation not finished; do not repeat before inspecting status')

    def install(self):
        data=(ROOT/'build/TabRemoto.bin').read_bytes()
        if len(data)<288 or data[0]!=0xe9 or int.from_bytes(data[12:14],'little')!=0x12 or data[23]!=1 or hashlib.sha256(data[:-32]).digest()!=data[-32:]:
            raise ValueError('Invalid app-only P4 image')
        before=self.request('/api/flash-layout');apps=self.request('/api/apps')
        occupied={a['slot'] for a in apps if a.get('descriptorValid')}
        if any(a.get('name')=='TabRemoto' for a in apps):
            raise RuntimeError('TabRemoto already installed; inspect its slot before updating')
        resources=ROOT/'sdcard/TabRemoto';manifest=validate_manifest(resources)
        uploads=[]
        for name in list(manifest['files'])+['manifest.json']:
            uploads.append(self.upload(resources/name,'/sdcard/TabRemoto/'+name,only_missing=name.startswith('config/')))
        remote='/sdcard/TabRemoto/updates/TabRemoto-'+hashlib.sha256(data).hexdigest()[:12]+'.bin'
        uploads.append(self.upload(ROOT/'build/TabRemoto.bin',remote,data=data))
        # Review the real plan before any flash change. Never replace another app.
        preview=self.request('/api/operation',{'action':'install','path':remote})
        match=re.search(r'no slot (\d+) ',preview.get('message',''))
        if not preview.get('ok') or not match or int(match.group(1)) in occupied:
            raise RuntimeError('Install does not target a new slot: '+str(preview))
        backup=None
        if preview.get('willRestart'):
            backup=self.operation({'action':'backup'})
            path=backup['result']['message'].split(': ',1)[-1]
            if not path.startswith('/sdcard/') or not path.endswith('.full'):raise RuntimeError('Unexpected backup path')
            raw=self.get_file(path);digest=self.get_file(path+'.sha256').decode().strip()
            if len(raw)!=16*1024*1024 or hashlib.sha256(raw).hexdigest()!=digest:raise RuntimeError('Backup readback failed')
            dest=ROOT/'artifacts/device-backups';dest.mkdir(parents=True,exist_ok=True)
            dest.chmod(0o700)
            private_backup=dest/(digest+'.full')
            private_backup.touch(mode=0o600,exist_ok=True);private_backup.chmod(0o600)
            private_backup.write_bytes(raw)
            backup['sha256']=digest;backup['host_copy_verified']=True
        operation=self.operation({'action':'install','path':remote},allow_restart=bool(preview.get('willRestart')))
        settled=time.monotonic()+120
        while True:
            try:
                after=self.request('/api/apps');layout=self.request('/api/flash-layout')
                if all(a.get('descriptorValid') for a in after):break
            except urllib.error.HTTPError as e:
                if e.code!=409:raise
            if time.monotonic()>settled:raise TimeoutError('Launcher still finalizing; inspect before retry')
            time.sleep(2)
        new=[a for a in after if a.get('name')=='TabRemoto' and a['slot'] not in occupied]
        if len(new)!=1:raise RuntimeError('Unexpected post-install state')
        if not preview.get('willRestart') and before['sizes']!=layout['sizes']:raise RuntimeError('Unexpected layout change')
        for old in apps:
            if old.get('descriptorValid') and not any(a['slot']==old['slot'] and a.get('name')==old.get('name') and a.get('version')==old.get('version') for a in after):
                raise RuntimeError('Existing application changed')
        report={'before_apps':apps,'after_apps':after,'before_layout':before,'after_layout':layout,'uploads':uploads,'backup':backup,'operation':operation,'slot':new[0]['slot'],'partition_table_changed':before['sizes']!=layout['sizes']}
        (ROOT/'artifacts/launcher-install.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
        return report

    def update(self):
        apps=self.request('/api/apps')
        own=[a for a in apps if a.get('name')=='TabRemoto' and a.get('descriptorValid')]
        if len(own)!=1:raise RuntimeError('Expected exactly one TabRemoto to update')
        # The launcher's installer requires an empty slot. Remove only this
        # project's previous diagnostic image, with its binary backup enabled.
        uninstall=self.operation({'action':'uninstall','slot':own[0]['slot'],'backupBinary':True})
        result=self.install();result['previous_tabremoto']=own[0];result['previous_binary_backup']=uninstall
        (ROOT/'artifacts/launcher-update.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
        return result

    def boot(self):
        apps=self.request('/api/apps');app=[a for a in apps if a.get('name')=='TabRemoto' and a.get('descriptorValid')]
        if len(app)!=1:raise RuntimeError('Expected one installed TabRemoto')
        return self.request('/api/app-action',{'action':'boot','slot':app[0]['slot']})

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port');p.add_argument('--launcher-tools',type=Path)
    p.add_argument('command',choices=['status','install','update','boot','capture']);a=p.parse_args()
    launcher=Launcher(a.port,a.launcher_tools)
    if a.command=='status':result={'status':launcher.status,'apps':launcher.request('/api/apps'),'layout':launcher.request('/api/flash-layout')}
    elif a.command=='install':result=launcher.install()
    elif a.command=='update':result=launcher.update()
    elif a.command=='boot':result=launcher.boot()
    else:
        path=ROOT/'artifacts/tabremoto-screen.ppm';path.write_bytes(launcher.get_file('/sdcard/TabRemoto/cache/screen.ppm'));result={'capture':str(path)}
    print(json.dumps(result,ensure_ascii=False,indent=2))
