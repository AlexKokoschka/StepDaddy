#!/usr/bin/env python3
"""Package a Pico SDK RP2040 .bin and verify its address range and round-trip."""
import argparse,struct
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('binary',type=Path);p.add_argument('uf2',type=Path);args=p.parse_args()
b=args.binary.read_bytes()
# The reserved save partition begins at flash offset 0x30000. This test
# must not reach it or either original firmware's high-address save area.
if not 256<len(b)<=0x30000:raise SystemExit('Image does not fit the test application flash region')
blocks=(len(b)+255)//256;out=bytearray()
for i in range(blocks):
 payload=b[i*256:(i+1)*256].ljust(256,b'\0')
 out+=struct.pack('<8I',0x0A324655,0x9E5D5157,0x2000,0x10000000+i*256,256,i,blocks,0xE48BFF56)
 out+=payload+bytes(220)+struct.pack('<I',0x0AB16F30)
assert len(out)==blocks*512
roundtrip=bytearray()
for i in range(blocks):
 block=out[512*i:512*(i+1)];h=struct.unpack_from('<8I',block)
 assert h==(0x0A324655,0x9E5D5157,0x2000,0x10000000+i*256,256,i,blocks,0xE48BFF56)
 assert struct.unpack_from('<I',block,508)[0]==0x0AB16F30
 roundtrip+=block[32:288]
assert roundtrip[:len(b)]==b
args.uf2.parent.mkdir(parents=True,exist_ok=True);args.uf2.write_bytes(out)
print(f'UF2 verified: {blocks} blocks, {len(b)} binary bytes, RP2040, flash ends 0x{0x10000000+blocks*256:08X}')
