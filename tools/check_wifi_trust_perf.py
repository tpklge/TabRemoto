#!/usr/bin/env python3
"""Real Tab5 scan, encrypted saved Wi-Fi, persistent RDP trust and paged KDF.

Credentials arrive only on stdin. Personal vault remains locked throughout.
"""
import argparse, json, sys, time
from pathlib import Path
from device import Device, find_port
from provision_device import provision

parser = argparse.ArgumentParser()
parser.add_argument('--known-certificate', action='store_true', help='Use an already saved laboratory certificate without clearing trust')
args = parser.parse_args()
payload = json.loads(sys.stdin.readline())
report = {'status': 'running', 'checks': {}}
output = Path('artifacts/wifi-trust-perf-device.json')
private = Path('artifacts/device-private/wifi-trust-perf.serial.log')
private.touch(mode=0o600, exist_ok=True)
private.chmod(0o600)
log = private.open('w')
d = Device(log=log)

def save():
    output.write_text(json.dumps(report, indent=2) + '\n')

def wait(predicate, seconds=90):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        s = d.command(timeout=8)
        if predicate(s):
            return s
        time.sleep(.3)
    raise TimeoutError('Device condition not reached')

def click(label):
    provision({'op': 'ui-click', 'label': label}, device=d)
    for _ in range(50):
        result = d.command('ui-action-status')['result']
        if result == 2:
            return
        assert result != 3, 'Missing visible control: ' + label
        time.sleep(.1)
    raise TimeoutError('UI action pending')

def connect(expect_verify):
    d.command('rdp-open')
    request = dict(payload['rdp'], op='rdp-ui', fingerprint='')
    provision(request, diagnostic_log=log, device=d)
    verified = False
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        s = d.command(timeout=8)
        r = s['rdp']
        if r['phase'] == 2:
            assert expect_verify, 'Saved certificate asked for approval again'
            assert r['fingerprint'] == payload['rdp']['fingerprint']
            if not verified:
                d.command('rdp-trust ' + r['fingerprint'])
                verified = True
        if r['phase'] in (4, 5):
            raise AssertionError(r)
        if r['phase'] == 3 and r['frames']:
            assert verified == expect_verify
            return s
        time.sleep(.25)
    raise TimeoutError('RDP authentication/desktop')

try:
    s = wait(lambda s: s['ready'] and s['network']['profiles_ready'])
    assert not s['security']['unlocked']
    provision(payload['wifi'], device=d)
    wait(lambda s: s['network']['wifi'] and s['network']['saved_wifi_count'] >= 1)
    report['checks']['protected_wifi_connected_and_saved'] = True
    d.command('network-open')
    click('Buscar redes')
    time.sleep(7)
    aps = json.loads(d.command('wifi-scan-result')['networks'])
    assert any(a['ssid'] == payload['wifi']['ssid'] and a['protected'] for a in aps)
    assert d.command('ui-layout-status')['errors'] == 0
    report['checks']['real_scan_shows_protected_target_without_layout_errors'] = True
    click('Cancelar')
    click('Redes salvas')
    assert d.command('ui-layout-status')['errors'] == 0
    click(payload['wifi']['ssid'] + ' | senha salva')
    wait(lambda s: s['network']['wifi'])
    report['checks']['saved_network_selected_without_retyping_password'] = True
    provision({'op': 'clock', 'epoch': int(time.time())}, device=d)
    report['first_rdp'] = connect(expect_verify=not args.known_certificate)
    report['checks']['existing_certificate_used' if args.known_certificate else 'first_approved_certificate_committed_before_authentication'] = True
    d.command('rdp-disconnect-all')
    wait(lambda s: s['rdp']['phase'] in (4, 5))
    report['second_rdp'] = connect(expect_verify=False)
    report['checks']['reconnect_uses_saved_certificate_without_prompt'] = True
    report['before_kdf'] = d.command()
    d.command('security-selftest')
    started = time.monotonic()
    seen_busy = False
    phases = set()
    previous = None
    while time.monotonic() - started < 600:
        s = d.command(timeout=10)
        phases.add(s['rdp']['phase'])
        sec = s['security']
        seen_busy |= sec['busy']
        if sec['message'] != previous:
            previous = sec['message']
            print('Vault:', previous, flush=True)
        if not sec['busy'] and (seen_busy or time.monotonic() - started > 2):
            assert sec['selftest'] == 2, sec
            break
        time.sleep(.7)
    else:
        raise TimeoutError('Isolated vault selftest')
    assert not s['security']['unlocked']
    report['after_kdf'] = wait(lambda s: s['rdp']['phase'] == 3 and s['rdp']['frames'], 120)
    report['kdf_observed_rdp_phases'] = sorted(phases)
    report['checks']['real_sd_kdf_aead_recovery_and_password_rotation_passed'] = True
    report['checks']['personal_vault_remained_locked_and_rdp_available_after_kdf'] = True
    save()
    d.command('rdp-disconnect-all')
    wait(lambda s: s['rdp']['phase'] in (4, 5))
    d.command('reboot')
    d.close()
    d = None
    time.sleep(14)
    sys.path.insert(0, str(Path.cwd().parent / 'Tab5LauncherPaludo/tools'))
    from tab5_usb import Tab5
    from launcher import Launcher
    launcher = Launcher()
    files = []
    for name in ('known-a.enc', 'known-b.enc'):
        raw = launcher.get_file('/sdcard/TabRemoto/config/' + name)
        assert raw[:8] == b'TRCFG001'
        assert payload['wifi']['password'].encode() not in raw
        assert payload['wifi']['ssid'].encode() not in raw
        assert payload['rdp']['fingerprint'].encode() not in raw
        files.append({'name': name, 'bytes': len(raw)})
    report['encrypted_files'] = files
    report['checks']['sd_profiles_encrypted_and_not_plaintext'] = True
    launcher.boot()
    time.sleep(10)
    d = Device(log=log)
    s = wait(lambda s: s['network']['wifi'], 90)
    assert s['network']['wifi_automatic']
    report['checks']['reboot_autoconnected_saved_protected_wifi_without_provisioning'] = True
    report['after_reboot_rdp'] = connect(expect_verify=False)
    report['checks']['reboot_preserved_approved_certificate_without_prompt'] = True
    report['status'] = 'passed'
except BaseException as error:
    report['status'] = 'failed'
    report['error'] = repr(error)
    raise
finally:
    if d:
        d.close()
    log.close()
    save()
