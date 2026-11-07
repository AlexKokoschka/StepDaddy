#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,zipfile
root=Path(__file__).resolve().parents[1];build=root/'build';arm=Path(os.environ['ARM_TOOLCHAIN'])/'bin'
name='StepMother_firmware';elf=build/(name+'.elf');hexfile=build/(name+'.hex')
log=(root/'build-final.log').read_text();tests=(root/'tests-final.log').read_text()
assert 'PASS Teensy 4.1 target build' in log and 'error:' not in log
assert tests.count('PASS ')>=7 and 'error:' not in tests
assert not re.search(re.escape(str(root))+r'/(?:src|voice)/[^\n]*warning:',log)
nm=subprocess.check_output([str(arm/'arm-none-eabi-nm'),'-n','-S',str(elf)],text=True)
symbols={};sizes={}
for line in nm.splitlines():
 p=line.split()
 if len(p)==4:
  symbols[p[3]]=int(p[0],16);sizes[p[3]]=int(p[1],16)
 elif len(p)==3:
  try:symbols[p[2]]=int(p[0],16)
  except ValueError:pass
stackgap=symbols['_estack']-symbols['_ebss'];heapgap=symbols['_heap_end']-symbols['_heap_start']
assert stackgap>32768 and heapgap>65536
assert symbols['_flashimagelen']<0x6C0000   # below the 1 MiB save partition (ends 0x7C0000)
# MIDI out is flushed only by the USB start-of-frame interrupt: a second flush
# from the main loop raced it (duplicate packets, a stuck MIDI-out pipe).
code=re.sub(r'//[^\n]*','',(root/'src/main_teensy.cpp').read_text())
assert 'usb_midi_flush_output' not in code and 'usbMIDI.send_now' not in code
for s in ['audio_service','stepmother_audio_usb_tx','sm_usb_midi_try_write','persist_task']:
 assert s in symbols and symbols[s]<0x20000
# Verify Intel HEX checksums, no overlaps, and flash-only programming addresses.
image={};upper=0;eof=False
for line in hexfile.read_text().splitlines():
 raw=bytes.fromhex(line.removeprefix(':'));assert sum(raw)%256==0
 n=raw[0];assert len(raw)==n+5
 addr=int.from_bytes(raw[1:3],'big');typ=raw[3];data=raw[4:4+n]
 if typ==4:upper=int.from_bytes(data,'big')<<16
 elif typ==0:
  for i,v in enumerate(data):
   a=upper+addr+i;assert 0x60000000<=a<0x60780000 and a not in image;image[a]=v
 elif typ==1:eof=True
 elif typ==5:pass
 else:raise AssertionError('Unexpected HEX record '+str(typ))
assert eof
# ELF runtime addresses map to flash load addresses for the .data segment.
def data_at(addr,n):
 if 0x20000000<=addr<0x20200000:addr=symbols['_sdataload']+addr-symbols['_sdata']
 return bytes(image[addr+i] for i in range(n))
for conf in ['usb_config_descriptor_12','usb_config_descriptor_480']:
 d=data_at(symbols[conf],sizes[conf]);i=0;rates=[];packets=[];interfaces=set();audio_eps=[];iad=None;ac_header=None
 assert d[1]==2 and int.from_bytes(d[2:4],'little')==len(d)
 while i<len(d):
  n=d[i];assert n>=2 and i+n<=len(d);b=d[i:i+n]
  if b[1]==4:interfaces.add(b[2])
  if b[1]==11 and b[4]==1:iad=b
  if b[1]==0x24 and b[2]==1 and n>=8 and b[3:5]==bytes([0,1]):ac_header=b
  if n==11 and b[1:4]==bytes([0x24,2,1]):
   assert b[4:8]==bytes([2,2,16,1]);rates.append(int.from_bytes(b[8:11],'little'))
  if b[1]==5 and b[2]&0x0F in (5,6):audio_eps.append(b[2])
  if b[1]==5 and b[2] in [0x85,0x05]:packets.append(int.from_bytes(b[4:6],'little'))
  i+=n
 # One audio stream, device to computer: one "Step Mother" in Audio MIDI Setup.
 assert rates==[48000] and packets==[192] and audio_eps==[0x85],(conf,rates,packets,audio_eps)
 assert d[4]==len(interfaces)==5,(conf,d[4],interfaces)
 assert iad is not None and iad[3]==2 and ac_header is not None and ac_header[7]==1 and ac_header[0]==9
 # The AC header's total length covers itself and the two terminals that follow.
 assert int.from_bytes(ac_header[5:7],'little')==9+12+9
p=data_at(symbols['usb_string_product_name'],sizes['usb_string_product_name'])
assert p[0]==len(p) and p[1]==3 and p[2:].decode('utf-16-le')=='Step Mother'
m=data_at(symbols['usb_string_manufacturer_name'],sizes['usb_string_manufacturer_name'])
assert m[2:].decode('utf-16-le')=='Step Mother'
# The version tag the updater reads (src/firmware_version.h), in the image.
ver=re.search(r'FIRMWARE_VERSION_STRING "([^"]+)"',(root/'src/firmware_version.h').read_text()).group(1)
blob=bytes(image[a] for a in sorted(image))
assert ('SSFW|Step Mother|'+ver+'|').encode() in blob,'version tag missing from the image'
print('PASS HEX validation: checksums, flash boundaries, both USB descriptors 48 kHz/192-byte audio, USB name "Step Mother", version tag '+ver+' and memory guards')
print(f"Memory: DTCM gap {stackgap}; RAM2 heap {heapgap}")
