#!/usr/bin/env python3
"""Lossless format conversion for fixed RGB device diagnostic screenshots."""
import struct,sys,zlib
from pathlib import Path

def convert(source):
 header,size,maximum,rgb=source.read_bytes().split(b'\n',3)
 width,height=map(int,size.split())
 if header!=b'P6' or maximum!=b'255' or not 0<width<=4096 or not 0<height<=4096 or len(rgb)!=width*height*3:
  raise ValueError('Invalid RGB PPM capture')
 def chunk(name,data):return struct.pack('>I',len(data))+name+data+struct.pack('>I',zlib.crc32(name+data)&0xffffffff)
 scan=b''.join(b'\0'+rgb[y*width*3:(y+1)*width*3] for y in range(height))
 png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b'')
 target=source.with_suffix('.png');target.write_bytes(png);target.chmod(0o600);return target
if __name__=='__main__':print(convert(Path(sys.argv[1])))
