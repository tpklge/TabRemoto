#!/usr/bin/env python3
"""USB diagnostic client for TabRemoto. Requires pyserial from the IDF venv."""
import argparse
import json
import time

def find_port():
    from serial.tools.list_ports import comports
    devices=[p.device for p in comports() if p.vid==0x303a and p.pid==0x1001]
    if len(devices)!=1:
        raise RuntimeError('Expected one Espressif USB Serial/JTAG device; found '+str(devices))
    return devices[0]

class Device:
    def __init__(self, port=None, log=None):
        import serial
        self.log=log
        self.serial=serial.Serial(port=None,baudrate=115200,timeout=.25,write_timeout=5,exclusive=True)
        self.serial.rts=True;self.serial.dtr=True;self.serial.port=port or find_port()
        self.serial.open();self.serial.rts=False;self.serial.dtr=False
        self.serial.write(b'\r\n');time.sleep(.3)

    def close(self):self.serial.close()

    def _wait_prompt(self):
        # An ACK is printed inside the handler. The SDK REPL still has to
        # finish that handler and restore linenoise before accepting input.
        # Sending immediately after ACK can lose the next command's prefix.
        deadline=time.monotonic()+.6
        tail=b''
        while time.monotonic()<deadline:
            raw=self.serial.read(self.serial.in_waiting or 1)
            if self.log and raw:
                self.log.write(raw.decode(errors='replace'));self.log.flush()
            tail=(tail+raw)[-64:]
            if b'TabRemoto>' in tail:return

    def command(self, command='status', timeout=20):
        if not command.startswith(('provision ','screen-read ','ssh-ui-trust ','rdp-trust ','rdp-key ','rdp-pointer ','rdp-touch ','ssh-list ','ssh-file ','vpn-probe ','vpn-stop ','rdp-resize ','file-hash ','file-remove ')) and command not in ('ssh-disconnect-all','rdp-disconnect-all','rdp-clipboard-fetch','rdp-clipboard-read','ui-action-status','ui-layout-status','status','rdp-ui-status','rdp-os-test','rdp-open','rdp-disconnect','storage-test','storage-retry','input-selftest','rdp-probe','security-selftest','security-boot-prepare','security-boot-check','security-open','provision-open','provision-status','network-open','ssh-open','ssh-ui-status','ssh-ui-probe','network-selftest','wifi-scan-result','wifi-reconnect-test','ethernet-start','vpn-stop','ssh-disconnect','ssh-output','ssh-list-result','file-fixture','snapshot','reboot'):
            raise ValueError('Unsupported diagnostic command')
        # A reply must follow this request; discard old replies first.
        pending=self.serial.read(self.serial.in_waiting) if self.serial.in_waiting else b''
        if self.log and pending:
            self.log.write(pending.decode(errors='replace'));self.log.flush()
        wire=('tabremoto '+command+'\r\n').encode()
        # JTAG console has a small RX ring and echoes each input character.
        # Pace long encrypted provisioning frames to avoid dropping the tail.
        for offset in range(0,len(wire),16):
            self.serial.write(wire[offset:offset+16]);self.serial.flush()
            if len(wire)>96:time.sleep(.025)
        expected_field={'status':'uptime_ms','rdp-ui-status':'focused','ui-layout-status':'errors','rdp-clipboard-read':'text','ssh-ui-status':'fullscreen','screen-read':'data','provision-open':'key','provision-status':'id','ui-action-status':'result'}.get(command.split(' ',1)[0])
        deadline=time.monotonic()+timeout
        next_retry=time.monotonic()+2
        while time.monotonic()<deadline:
            if command=='status' and time.monotonic()>=next_retry:
                self.serial.write(b'tabremoto status\r\n');next_retry=time.monotonic()+2
            line=self.serial.readline().decode(errors='replace')
            if self.log:self.log.write(line);self.log.flush()
            start=line.find('TABREMOTO {')
            if start<0:continue
            try:result=json.loads(line[start+10:])
            except json.JSONDecodeError:continue
            # A retried read can finish late during SD recovery. Never treat
            # its ACK as the answer to a different structured read.
            if result.get('ok') and expected_field and expected_field not in result:continue
            if (command=='snapshot' or command.startswith('provision ')) and set(result)!={'ok'}:continue
            if command!='reboot':self._wait_prompt()
            if not result.get('ok'):raise RuntimeError(result)
            return result
        raise TimeoutError('No TabRemoto reply for '+command.split(' ',1)[0])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--port');p.add_argument('command',nargs='?',default='status')
    a=p.parse_args();d=Device(a.port)
    try:print(json.dumps(d.command(a.command),ensure_ascii=False,indent=2))
    finally:d.close()
