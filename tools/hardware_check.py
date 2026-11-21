#!/usr/bin/env python3
"""Real Tab5 checks: boot, bounded SD probes, memory samples and reboot cycles."""
import argparse
import json
import time
from pathlib import Path
from device import Device, find_port
from launcher import Launcher

ROOT=Path(__file__).resolve().parents[1]

def restore_wifi_auto(launcher,value):
    # Avoid unnecessary SD writes in the launcher, including a write of a
    # preference that was already enabled by the user before this test.
    if launcher.request('/api/config')['wifiAuto']!=value:
        launcher.request('/api/config',{'wifiAuto':value})
    assert launcher.request('/api/config')['wifiAuto']==value

def wait_launcher(port):
    deadline=time.monotonic()+90
    while time.monotonic()<deadline:
        try:
            launcher=Launcher(port)
            # The launcher's SD font/profile workers can still be initializing
            # after its USB console and HTTP API are already available.
            if launcher.status.get('uptimeUs',0)<20_000_000:
                time.sleep(2);continue
            return launcher
        except (OSError,RuntimeError,TimeoutError):time.sleep(2)
    raise TimeoutError('Launcher did not return')

def boot(launcher,log):
    device=Device(launcher.port,log)
    launcher.boot()
    deadline=time.monotonic()+60
    while time.monotonic()<deadline:
        try:
            state=device.command('status',timeout=3)
            if state['ready']:return device,state
        except (OSError,TimeoutError):pass
        time.sleep(1)
    device.close();raise TimeoutError('TabRemoto did not boot')

def check(port,seconds):
    report={'status':'running','real_hardware':True,'cycles':[],'samples':[],
            'physical_touch_test':False,'physical_typing_test':False,
            'network_protocols_tested':False}
    output=ROOT/'artifacts/hardware-check.json'
    device=None
    original_wifi_auto=None
    def save():output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    with (ROOT/'artifacts/hardware-serial.log').open('w') as log:
        try:
            launcher=wait_launcher(port)
            # The launcher defaults to manual Wi-Fi. Temporarily enable its
            # saved-network reconnect for repeated HTTP checks, then restore.
            original_wifi_auto=launcher.request('/api/config')['wifiAuto']
            report['launcher_wifi_auto_before']=original_wifi_auto
            if not original_wifi_auto:launcher.request('/api/config',{'wifiAuto':True})
            device,state=boot(launcher,log)
            print('TabRemoto booted',flush=True)
            for key in ('ready','display','touch_driver','sd_mounted','sd_writable','resources'):
                assert state[key],(key,state)
            assert state['app']=='TabRemoto' and state['flash_bytes']==16*1024*1024,state
            assert state['ui_flushes']>0,state
            report['initial']=state;save()
            for _ in range(20):device.command('storage-test')
            report['storage_probes_passed']=20
            deadline=time.monotonic()+seconds
            while time.monotonic()<deadline:
                time.sleep(min(5,max(0,deadline-time.monotonic())))
                sample=device.command('status');report['samples'].append(sample);save()
                if sample.get('touch_clicks',0)>0:report['physical_touch_test']=True
                assert sample['ready'] and sample['uptime_ms']>state['uptime_ms'],sample
            assert report['samples'][-1]['internal_free']>=state['internal_free']-8192,report['samples'][-1]
            device.command('snapshot',timeout=60);report['render_snapshot_saved']=True
            for cycle in range(2):
                device.command('reboot');device.close();device=None
                launcher=wait_launcher(port)
                if cycle==0:
                    image=launcher.get_file('/sdcard/TabRemoto/cache/screen.ppm')
                    assert image.startswith(b'P6\n1280 720\n255\n') and len(image)==1280*720*3+16
                    (ROOT/'artifacts/tabremoto-screen.ppm').write_bytes(image)
                device,after=boot(launcher,log)
                assert after['resources'] and after['sd_writable'] and after['ready'],after
                report['cycles'].append(after);save()
                print('Reboot cycle passed:',cycle+1,flush=True)
            # Tamper only with this app's public manifest; restore exact bytes
            # in a finally block and verify the internal diagnostic fallback.
            device.command('reboot');device.close();device=None
            launcher=wait_launcher(port)
            manifest_path='/sdcard/TabRemoto/manifest.json'
            original=launcher.get_file(manifest_path)
            endpoint='/api/upload?directory=%2Fsdcard%2FTabRemoto&relative=manifest.json'
            try:
                corrupt=json.loads(original);corrupt['files']['assets/themes/dark.json']='0'*64
                launcher.request(endpoint,json.dumps(corrupt).encode())
                device,bad=boot(launcher,log)
                assert bad['ready'] and not bad['resources'] and bad['sd_writable'],bad
                report['invalid_manifest_fallback']=bad
            finally:
                if device:
                    device.command('reboot');device.close();device=None
                launcher=wait_launcher(port)
                launcher.request(endpoint,original)
                assert launcher.get_file(manifest_path)==original
            device,restored=boot(launcher,log)
            assert restored['resources'] and restored['ready'],restored
            report['manifest_restored']=True
            device.command('reboot');device.close();device=None
            launcher=wait_launcher(port)
            restore_wifi_auto(launcher,original_wifi_auto)
            report['launcher_wifi_auto_restored']=True
            device,restored=boot(launcher,log)
            report['status']='passed';report['final']=device.command('status')
        except BaseException as error:
            report['status']='failed';report['error']=repr(error);raise
        finally:
            if original_wifi_auto is not None and not report.get('launcher_wifi_auto_restored'):
                try:
                    if device:
                        device.command('reboot');device.close();device=None
                    launcher=wait_launcher(port)
                    restore_wifi_auto(launcher,original_wifi_auto)
                    report['launcher_wifi_auto_restored']=True
                except Exception as cleanup:
                    report['cleanup_error']=repr(cleanup)
            save()
            if device:device.close()
    print(json.dumps(report,ensure_ascii=False,indent=2))

def resume_final(port):
    """Finish only the final boot after the known redundant-config-write error."""
    output=ROOT/'artifacts/hardware-check.json'
    report=json.loads(output.read_text())
    assert report['status']=='failed' and report.get('error')=="<HTTPError 400: 'Bad Request'>",'Unexpected failure; do not skip tests'
    assert report['storage_probes_passed']==20 and len(report['cycles'])==2
    assert report['render_snapshot_saved'] and report['manifest_restored']
    assert report['invalid_manifest_fallback']['resources'] is False
    assert report['launcher_wifi_auto_restored'] is True
    issue=report.pop('error');report['status']='running'
    report['test_harness_issues']=[{'error':issue,'resolved':'Read back the unchanged Wi-Fi preference; skip redundant configuration writes.'}]
    report['resumed_final_boot']=True
    launcher=wait_launcher(port)
    restore_wifi_auto(launcher,report['launcher_wifi_auto_before'])
    device=None
    try:
        with (ROOT/'artifacts/hardware-serial.log').open('a') as log:
            device,state=boot(launcher,log)
            for key in ('ready','display','touch_driver','sd_mounted','sd_writable','resources'):
                assert state[key],(key,state)
            assert state['ui_flushes']>0
            report['final']=state;report['status']='passed'
    except BaseException as error:
        report['status']='failed';report['error']=repr(error);raise
    finally:
        output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
        if device:device.close()
    print(json.dumps(report,ensure_ascii=False,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port');p.add_argument('--seconds',type=int,default=60)
    p.add_argument('--resume-final',action='store_true')
    a=p.parse_args()
    if a.resume_final:resume_final(a.port or find_port())
    else:check(a.port or find_port(),a.seconds)
