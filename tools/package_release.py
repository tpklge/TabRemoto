#!/usr/bin/env python3
"""Package only the public source resources and the actual app-only P4 image."""
import hashlib
import json
import shutil
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
PUBLIC_FILES = ('assets/themes/dark.json', 'locales/pt-BR.json', 'config/app.json', 'assets/fonts/latin20.bin', 'assets/fonts/OFL.txt', 'assets/fonts/terminal20.bin', 'assets/fonts/TERMINAL-OFL.txt')

def sha(data):
    return hashlib.sha256(data).hexdigest()

def validate_manifest(root):
    manifest = json.loads((root / 'manifest.json').read_text())
    if manifest.get('schema') != 1 or manifest.get('app') != 'TabRemoto':
        raise ValueError('Invalid resource manifest')
    if manifest.get('resource_version')!='0.1.0' or manifest.get('firmware')!='0.1.0':
        raise ValueError('Incompatible resource package')
    entries = manifest.get('files')
    if not isinstance(entries, dict) or set(entries) != set(PUBLIC_FILES):
        raise ValueError('Unexpected public resource inventory')
    for name, digest in entries.items():
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name:
            raise ValueError('Unsafe resource path')
        target = root / name
        if target.resolve() != root.resolve().joinpath(*path.parts) or target.is_symlink():
            raise ValueError('Resource symlink rejected')
        if sha(target.read_bytes()) != digest:
            raise ValueError('Resource checksum mismatch: ' + name)
    return manifest

def package():
    resources = ROOT / 'sdcard/TabRemoto'
    # This directory is versioned public input. Never package a device SD dump.
    for name in PUBLIC_FILES:
        if (resources / name).is_symlink():
            raise ValueError('Symlink rejected')
    manifest = dict(schema=1, app='TabRemoto', resource_version='0.1.0',
                    firmware='0.1.0', files={name: sha((resources/name).read_bytes()) for name in PUBLIC_FILES})
    (resources/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n')
    validate_manifest(resources)
    app = ROOT/'build/TabRemoto.bin'
    data = app.read_bytes()
    if len(data)<288 or data[0]!=0xe9 or int.from_bytes(data[12:14], 'little')!=0x12:
        raise ValueError('Not an ESP32-P4 application image')
    if data[23]!=1 or hashlib.sha256(data[:-32]).digest()!=data[-32:]:
        raise ValueError('Invalid application SHA-256 trailer')
    out = ROOT/'dist/TabRemoto-0.1.0'
    (out/'firmware').mkdir(parents=True, exist_ok=True)
    shutil.copy2(app, out/'firmware/TabRemoto.bin')
    for name in ('manifest.json',)+PUBLIC_FILES:
        target=out/'sdcard/TabRemoto'/name;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(resources/name,target)
    report=dict(app='TabRemoto',version='0.1.0',bytes=len(data),sha256=sha(data),application_only=True,
                install='LauncherdoPaludo app-only; do not flash a bootloader or partition table')
    (out/'firmware/manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    shutil.copy2(ROOT/'dependencies.lock',out/'dependencies.lock')
    for component in (ROOT/'managed_components').iterdir():
        if not component.is_dir():continue
        for pattern in ('LICENSE*','LICENCE*','COPYING*'):
            for source in component.glob(pattern):
                if source.is_file() and not source.is_symlink():
                    target=out/'licenses'/component.name/source.name
                    target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    source=ROOT/'research/openssl/LICENSE.txt'
    target=out/'licenses/openssl/LICENSE.txt';target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,target)
    shutil.copy2(ROOT/'research/rdp-dependencies.json',out/'rdp-dependencies.json')
    shutil.copy2(ROOT/'research/cares-dependencies.json',out/'cares-dependencies.json')
    (out/'licenses/c-ares').mkdir(parents=True,exist_ok=True)
    shutil.copy2(ROOT/'research/c-ares/LICENSE.md',out/'licenses/c-ares/LICENSE.md')
    shutil.copy2(ROOT/'research/network-dependencies.json',out/'network-dependencies.json')
    shutil.copy2(ROOT/'research/putty-dependencies.json',out/'putty-dependencies.json')
    (out/'licenses/putty').mkdir(parents=True,exist_ok=True)
    shutil.copy2(ROOT/'research/putty/LICENCE',out/'licenses/putty/LICENCE')
    for name, license_name in (('libssh2','COPYING'),('esp_wireguard','LICENSE'),('libvterm','LICENSE')):
        source=ROOT/'research'/name/license_name
        target=out/'licenses'/name/license_name
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    # Preserve individual crypto/upstream copyright notices as distributed.
    for source in (ROOT/'research/esp_wireguard/src').rglob('*'):
        if source.is_file() and source.suffix in ('.c','.h'):
            target=out/'licenses/esp_wireguard/source-notices'/source.relative_to(ROOT/'research/esp_wireguard/src')
            target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    for source in (ROOT/'research/FreeRDP/LICENSE',ROOT/'research/FreeRDP/winpr/LICENSE',ROOT/'components/tabremoto_usb_net/license-ecm.txt',ROOT/'components/tabremoto_usb_net/license-framing.txt'):
        if source.is_file():
            target=out/'licenses'/source.parent.name/source.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    description=json.loads((ROOT/'build/project_description.json').read_text())
    sdk=Path(description['idf_path'])
    for name in ('LICENSE','components/mbedtls/mbedtls/LICENSE','components/lwip/lwip/COPYING','components/freertos/FreeRTOS-Kernel/LICENSE.md'):
        source=sdk/name
        if source.is_file():
            target=out/'licenses/esp-idf'/name
            target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    for name in ('README.md','LICENSE','THIRD_PARTY_NOTICES.md'):
        if (ROOT/name).exists():shutil.copy2(ROOT/name,out/name)
    for name in ('interface_launcher.md','rdp_cliques.md','wifi_confianca_desempenho.md','rdp_interface.md','etapas_finais.md','correcoes_usabilidade.md','rede_ssh.md','cadastros_seguranca.md','proximas_etapas.md','base_aparelho.md','status.md','testes.md','viabilidade.md','arquitetura.md','seguranca.md','armazenamento.md','teclado.md','usb_ethernet_compatibilidade.md','rdp_compatibilidade.md'):
        source=ROOT/'docs'/name
        if source.exists():
            (out/'docs').mkdir(exist_ok=True);shutil.copy2(source,out/'docs'/name)
    print(json.dumps(report,indent=2))
    return out

if __name__=='__main__':package()
