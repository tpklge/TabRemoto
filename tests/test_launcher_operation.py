"""A reboot's automatic install must finish without another mutation."""
import sys,unittest
from pathlib import Path
from unittest.mock import patch
import urllib.error
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from launcher import Launcher

class LauncherOperationTests(unittest.TestCase):
    def test_transient_invalid_image_and_busy_apps_do_not_replan(self):
        device=object.__new__(Launcher)
        writes=[]
        replies=iter([
            [{'slot':2,'name':'app2','descriptorValid':False}],
            urllib.error.HTTPError('http://device/api/apps',409,'Busy',{},None),
            [{'slot':2,'name':'TabRemoto','descriptorValid':True}],
        ])
        def request(path,body=None,raw=False):
            if body:
                writes.append(body)
                return {'ok':True,'ticket':'test','willRestart':True,'message':'Install test image'} if body['action']=='install' else {'ok':True}
            if path=='/api/operation':return {'running':False,'percent':0,'error':0,'message':''}
            if path=='/api/apps':
                response=next(replies)
                if isinstance(response,Exception):raise response
                return response
            raise AssertionError(path)
        device.request=request
        with patch('launcher.time.sleep'):
            result=device.operation({'action':'install','path':'/sdcard/TabRemoto/updates/test.bin'},allow_restart=True)
        self.assertEqual([w['action'] for w in writes],['install','execute'])
        self.assertFalse(result['result']['running'])

if __name__=='__main__':unittest.main()

class LauncherTransferTests(unittest.TestCase):
    def test_upload_uses_frozen_image_bytes(self):
        import tempfile,hashlib
        device=object.__new__(Launcher);stored={};writes=[]
        def request(path,body=None,raw=False):
            if path.startswith('/api/file?'):
                if not stored:raise urllib.error.HTTPError('http://device',404,'missing',{},None)
                return stored['data']
            writes.append(body);stored['data']=body;return {'ok':True}
        device.request=request
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'app.bin';source.write_bytes(b'rebuilt-after-plan')
            result=device.upload(source,'/sdcard/TabRemoto/updates/app.bin',data=b'validated-before-plan')
        self.assertEqual(writes,[b'validated-before-plan'])
        self.assertEqual(result['sha256'],hashlib.sha256(writes[0]).hexdigest())

    def test_cookie_session_does_not_keep_sending_pairing_code(self):
        import io,json
        class Response(io.BytesIO):
            def __enter__(self):return self
            def __exit__(self,*args):self.close()
        class Http:
            def open(self,request,timeout):
                self.headers={k.lower():v for k,v in request.header_items()}
                return Response(b'{"ok":true}')
        device=object.__new__(Launcher);device.auth={'ip':'device','code':'12345678'};device.paired=True;device.http=Http()
        self.assertTrue(device.request('/api/operation',{'action':'backup'})['ok'])
        self.assertEqual(device.http.headers['x-launcher-client'],'web')
        self.assertNotIn('x-launcher-token',device.http.headers)

class LauncherPollTests(unittest.TestCase):
    def test_read_timeout_during_flash_does_not_repeat_execute(self):
        device=object.__new__(Launcher);writes=[];reads=0
        def request(path,body=None,raw=False):
            nonlocal reads
            if body:
                writes.append(body['action'])
                return {'ok':True,'ticket':'one','willRestart':False,'message':'App-only write'}
            reads+=1
            if reads==1:raise TimeoutError('HTTP stalled during erase')
            return {'running':False,'percent':100,'error':0,'message':'done'}
        device.request=request
        with patch('launcher.time.sleep'):
            result=device.operation({'action':'install','path':'/sdcard/TabRemoto/updates/test.bin'})
        self.assertEqual(writes,['install','execute'])
        self.assertEqual(result['result']['percent'],100)

class LauncherUploadRecoveryTests(unittest.TestCase):
    def test_lost_upload_reply_is_reconciled_by_bytes_before_retry(self):
        import tempfile
        device=object.__new__(Launcher);stored={};posts=[]
        def request(path,body=None,raw=False):
            if path.startswith('/api/file?'):
                if 'data' not in stored:raise urllib.error.HTTPError('http://device',404,'missing',{},None)
                return stored['data']
            posts.append(body);stored['data']=body;raise BrokenPipeError('response lost')
        device.request=request
        with tempfile.TemporaryDirectory() as folder,patch('launcher.time.sleep'):
            p=Path(folder)/'app.bin';p.write_bytes(b'validated-image')
            result=device.upload(p,'/sdcard/TabRemoto/updates/test.bin')
        self.assertEqual(posts,[b'validated-image']);self.assertTrue(result['verified'])

class AutomaticInstallerBusyTests(unittest.TestCase):
    def test_busy_auto_installer_does_not_execute_another_install(self):
        device=object.__new__(Launcher);device.port='test';writes=[];plans=0;app_reads=0
        def request(path,body=None,raw=False):
            nonlocal plans,app_reads
            if body:
                writes.append(body['action'])
                if body['action']=='install':
                    plans+=1
                    if plans>1:return {'ok':False,'message':'Operacao em andamento'}
                    return {'ok':True,'ticket':'first','willRestart':True,'message':'Install test'}
                return {'ok':True}
            if path=='/api/operation':return {'running':False,'error':0,'percent':0,'message':''}
            if path=='/api/apps':
                app_reads+=1
                if plans<2:return [{'slot':2,'descriptorValid':False}]
                return [{'slot':2,'name':'TabRemoto','descriptorValid':True}]
            raise AssertionError(path)
        device.request=request
        # Simulate a reconnect without contacting USB and advance past settling.
        def init(self,*args):self.status={'uptimeUs':30_000_000}
        tick=[0]
        def monotonic():tick[0]+=10;return tick[0]
        with patch.object(Launcher,'__init__',init),patch('launcher.time.sleep'),patch('launcher.time.monotonic',monotonic):
            device.operation({'action':'install','path':'/sdcard/test.bin'},allow_restart=True)
        self.assertEqual(writes,['install','execute','install'])

class LauncherNetworkRecoveryTests(unittest.TestCase):
    def test_failed_c6_after_authorized_restart_recovers_without_repeating_execute(self):
        device=object.__new__(Launcher);device.port='test';writes=[];reads=[0];recoveries=[]
        def request(path,body=None,raw=False):
            if body:
                writes.append(body['action']);return {'ok':True,'ticket':'one','willRestart':True,'message':'Instalar TabRemoto no slot 2 (app2).'}
            if path=='/api/operation':
                reads[0]+=1
                if reads[0]==1:raise TimeoutError('reboot')
                return {'running':False,'error':0,'percent':100,'message':'done'}
            if path=='/api/apps':return [{'slot':2,'name':'TabRemoto','descriptorValid':True}]
            raise AssertionError(path)
        device.request=request
        def init(self,*args):self.status={'wifiState':10};raise RuntimeError('C6 unavailable')
        device._recover_network_after_install=lambda slot:recoveries.append(slot) or True
        with patch.object(Launcher,'__init__',init),patch('launcher.time.sleep'):
            result=device.operation({'action':'install','path':'/sdcard/test.bin'},allow_restart=True)
        self.assertEqual(writes,['install','execute']);self.assertEqual(recoveries,[2]);self.assertEqual(result['result']['percent'],100)
