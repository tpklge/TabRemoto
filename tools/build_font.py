#!/usr/bin/env python3
"""Generate the public Latin-1 font from the pinned LVGL Montserrat source.
Requires Node and lv_font_conv 1.5.3; pass their paths explicitly if necessary.
"""
import argparse,subprocess,shutil,hashlib,json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--node',default='node');p.add_argument('--converter',required=True);a=p.parse_args()
source=root/'managed_components/lvgl__lvgl/scripts/built_in_font'
out=root/'sdcard/TabRemoto/assets/fonts';out.mkdir(parents=True,exist_ok=True)
subprocess.run([a.node,a.converter,'--size','20','--bpp','4','--format','bin','--font',str(source/'Montserrat-Medium.ttf'),'-r','0x20-0xff','--no-compress','--no-kerning','-o',str(out/'latin20.bin')],check=True)
shutil.copyfile(source/'font_license/Montserrat/OFL.txt',out/'OFL.txt')
print(json.dumps({'source_sha256':hashlib.sha256((source/'Montserrat-Medium.ttf').read_bytes()).hexdigest(),'font_sha256':hashlib.sha256((out/'latin20.bin').read_bytes()).hexdigest(),'ranges':'U+0020..U+00FF','pixels':20},indent=2))

terminal_source=root/'research/RobotoMono/fonts/ttf/RobotoMono-Regular.ttf'
subprocess.run([a.node,a.converter,'--size','20','--bpp','4','--format','bin','--font',str(terminal_source),'-r','0x20-0xff','--font',str(source/'DejaVuSans.ttf'),'-r','0x2500-0x257f','--no-compress','--no-kerning','-o',str(out/'terminal20.bin')],check=True)
(out/'TERMINAL-OFL.txt').write_text((root/'research/RobotoMono/OFL.txt').read_text()+'\n\nDejaVu Sans (box drawing fallback):\n'+(source/'font_license/DejaVuSans/LICENSE').read_text())
