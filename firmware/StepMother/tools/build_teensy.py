#!/usr/bin/env python3
"""Pinned PJRC Teensyduino 1.62.0 build, Arm GNU 13.2.Rel1, Teensy 4.1 only.
Env: TEENSY_PACKAGE (directory containing cores/ and libraries/), ARM_TOOLCHAIN.
Downloads a verified package if TEENSY_PACKAGE is absent; needs zstandard then.
No Arduino installation or USB device access is used to build.
"""
from pathlib import Path
import concurrent.futures,hashlib,json,os,shutil,subprocess,sys,urllib.request,tarfile
root=Path(__file__).resolve().parents[1];build=root/'build';build.mkdir(exist_ok=True)
version='1.62.0';digest='472d0a66708e366bf447c7870032edc549a496d671d651d51e2d3c3f28baffb9'
pkg=os.environ.get('TEENSY_PACKAGE')
if not pkg and (root/'vendor/avr').is_dir():pkg=root/'vendor/avr'
if not pkg:
 import zstandard
 archive=build/'teensy-package.tar.zst'
 if not archive.exists():archive.write_bytes(urllib.request.urlopen('https://www.pjrc.com/teensy/td_162/teensy-package-1.62.0.tar.zst',timeout=60).read())
 assert hashlib.sha256(archive.read_bytes()).hexdigest()==digest
 with archive.open('rb') as f,zstandard.ZstdDecompressor().stream_reader(f) as stream:
  with tarfile.open(fileobj=stream,mode='r|') as tf:tf.extractall(build/'vendor',filter='data')
 pkg=build/'vendor/avr'
pkg=Path(pkg).resolve();core=build/'teensy4'
if core.exists():shutil.rmtree(core)
shutil.copytree(pkg/'cores/teensy4',core)
libs=pkg/'libraries'
# Copy and patch LittleFS locally; installed libraries are never modified.
lf=build/'LittleFS';shutil.rmtree(lf,ignore_errors=True);shutil.copytree(libs/'LittleFS/src',lf)
p=lf/'LittleFS.cpp';s=p.read_text();needle='if (lfs_mount(&lfs, &config) < 0) {'
a=s.index(needle,s.index('bool LittleFS_Program::begin'));a+=len(needle)
s=s[:a]+'''
        // Keep an unmountable partition that holds a littlefs superblock (our own
        // saves, never formatted away); blank flash or leftovers from other
        // firmware are formatted. Decision and status: src/storage_teensy.cpp.
        if(sm_lfs_keep_unmountable((const volatile uint8_t *)baseaddr,size,SECTOR_SIZE))return false;
'''+s[a:]
s=s.replace('bool LittleFS_Program::begin','extern "C" bool sm_lfs_keep_unmountable(const volatile uint8_t *raw,uint32_t size,uint32_t block);\nbool LittleFS_Program::begin',1)
p.write_text(s)
p=core/'usb_desc.h';s=p.read_text().replace('AUDIO_TX_SIZE         180','AUDIO_TX_SIZE         192').replace('AUDIO_RX_SIZE         180','AUDIO_RX_SIZE         192');p.write_text(s)
p=core/'usb_desc.c';s=p.read_text();assert s.count('LSB(44100), MSB(44100), 0')==4;s=s.replace('LSB(44100), MSB(44100), 0','LSB(48000), MSB(48000), 0');p.write_text(s)
def replace_body(text,signature,body):
 a=text.index(signature);start=text.index('{',a);depth=1;i=start+1
 while depth:
  if text[i]=='{':depth+=1
  if text[i]=='}':depth-=1
  i+=1
 return text[:start]+'{\n'+body+'\n}'+text[i:]
p=core/'usb_audio.cpp';s=p.read_text().replace('739875226; // 44.1 * 2^24','805306368; // 48 * 2^24')
s=s.replace('#include <Arduino.h>','#include <Arduino.h>\nextern "C" void stepmother_audio_usb_tx(int16_t *,unsigned);')
s=replace_body(s,'unsigned int usb_audio_transmit_callback(void)','stepmother_audio_usb_tx((int16_t *)usb_audio_transmit_buffer,48);\nreturn 48*4;')
# Host playback is intentionally discarded; this device supplies synth audio only.
s=replace_body(s,'void usb_audio_receive_callback(unsigned int len)','(void)len;')
p.write_text(s)
p=core/'usb_midi.c';s=p.read_text();a=s.index('void usb_midi_write_packed(uint32_t n)');b=s.index('\nvoid usb_midi_flush_output',a);fn=s[a:b]
fn=fn.replace('void usb_midi_write_packed','int sm_usb_midi_try_write').replace('if (!usb_configuration) return;','if (!usb_configuration) return 0;')
a1=fn.index('\tuint32_t wait_begin_at');b1=fn.index('\tuint32_t *txdata',a1)
fn=fn[:a1]+'''\tif (!tx_available) {
        uint32_t status=usb_transfer_status(xfer);
        if(status&0x80){tx_noautoflush=0;return 0;}
        tx_available=tx_packet_size;
    }
'''+fn[b1:]
fn=fn.replace('\ttx_noautoflush = 0;','\ttx_noautoflush = 0;\n return 1;')
s=s[:a]+fn+'\n'+s[a:];p.write_text(s)
# One audio device on the computer: the Step Mother only sends audio. The
# host-to-device stream (and its feedback endpoint) is removed from the
# descriptors; with it, macOS listed a second "Step Mother" with outputs only,
# because the two streams were clocked differently (adaptive in, async out).
p=core/'usb_desc.h';s=p.read_text();a=s.index('#elif defined(USB_MIDI_AUDIO_SERIAL)');b=s.index('#elif',a+5)
blk=s[a:b];assert blk.count('#define NUM_INTERFACE\t\t6')==1
s=s[:a]+blk.replace('#define NUM_INTERFACE\t\t6','#define NUM_INTERFACE\t\t5')+s[b:];p.write_text(s)
p=core/'usb_desc.c';s=p.read_text()
old_size='#define AUDIO_INTERFACE_DESC_SIZE\t8 + 9+10+12+9+12+10+9 + 9+9+7+11+9+7 + 9+9+7+11+9+7+9'
assert s.count(old_size)==1
s=s.replace(old_size,'#define AUDIO_INTERFACE_DESC_SIZE\t8 + 9+9+12+9 + 9+9+7+11+9+7')
def one_way_audio(blk):
 for old,new in [
  ('        3,                                      // bInterfaceCount','        2,                                      // bInterfaceCount'),
  ('\t10,\t\t\t\t\t// bLength\n\t0x24,\t\t\t\t\t// bDescriptorType, 0x24 = CS_INTERFACE\n\t0x01,\t\t\t\t\t// bDescriptorSubtype, 1 = HEADER',
   '\t9,\t\t\t\t\t// bLength\n\t0x24,\t\t\t\t\t// bDescriptorType, 0x24 = CS_INTERFACE\n\t0x01,\t\t\t\t\t// bDescriptorSubtype, 1 = HEADER'),
  ('\tLSB(62), MSB(62),\t\t\t// wTotalLength\n\t2,\t\t\t\t\t// bInCollection\n',
   '\tLSB(30), MSB(30),\t\t\t// wTotalLength\n\t1,\t\t\t\t\t// bInCollection\n'),
  ('\tAUDIO_INTERFACE+2,\t\t\t// baInterfaceNr(2) - Receive from PC\n','')]:
  assert blk.count(old)==1,old
  blk=blk.replace(old,new)
 # Terminals 3 (USB stream in), 0x31 (volume) and 4 (out) feed the removed stream.
 a=blk.index('\t// Input Terminal Descriptor',blk.index('\t\t\t\t\t// bTerminalID\n')+1)
 b=blk.index('\t// Standard AS Interface Descriptor')
 assert '3,\t\t\t\t\t// bTerminalID' in blk[a:b] and '0x31' in blk[a:b]
 blk=blk[:a]+blk[b:]
 # The second streaming interface (AUDIO_INTERFACE+2) runs to the block's end.
 a=blk.index('\t// Standard AS Interface Descriptor',blk.index('AUDIO_INTERFACE+1,\t\t\t// bInterfaceNumber'))
 assert 'AUDIO_INTERFACE+2' in blk[a:] and 'AUDIO_INTERFACE+2' not in blk[:a]
 return blk[:a]
out,pos,n='',0,0
while True:
 a=s.find('#ifdef AUDIO_INTERFACE\n',pos)
 if a<0:break
 b=s.index('\n#endif',a)
 out+=s[pos:a]+one_way_audio(s[a:b]);pos=b;n+=1
assert n==2;s=out+s[pos:];p.write_text(s)
# The sampling-rate reply matches the 48 kHz stream.
p=core/'usb.c';s=p.read_text();assert s.count('44100 & 255')==1 and s.count('44100 >> 8')==1
s=s.replace('44100 & 255','48000 & 255').replace('44100 >> 8','48000 >> 8');p.write_text(s)
# Hash every dependency actually compiled/used, including local patches.
deps={str(p.relative_to(build)):hashlib.sha256(p.read_bytes()).hexdigest() for base in [core,lf] for p in base.rglob('*') if p.is_file()}
(build/'patched_dependencies_sha256.json').write_text(json.dumps(deps,indent=2)+'\n')
arm=Path(os.environ['ARM_TOOLCHAIN'])/'bin'
inc=[root/'src',root/'voice/src',root/'compat',core,libs/'Wire',libs/'SPI',lf]
flags=['-O2','-g','-mcpu=cortex-m7','-mthumb','-mfloat-abi=hard','-mfpu=fpv5-d16',
 '-ffunction-sections','-fdata-sections','-fshort-enums','-Wall','-Wextra','-Wno-unused-parameter',
 '-DF_CPU=600000000','-DUSB_MIDI_AUDIO_SERIAL','-DLAYOUT_US_ENGLISH','-DARDUINO=10819',
 '-DTEENSYDUINO=162','-DARDUINO_TEENSY41','-D__IMXRT1062__','-DSTEPMOTHER_TEENSY','-DAUDIO_SAMPLE_RATE_EXACT=48000.0f']
flags += ['-I'+str(p) for p in inc]
app=list((root/'src').glob('*.c'))+list((root/'src').glob('*.cpp'))+list((root/'voice/src').glob('*.c'))
core_src=[p for p in core.iterdir() if p.suffix in ('.c','.cpp','.S')]
lib_src=[libs/'Wire/Wire.cpp',libs/'Wire/WireIMXRT.cpp',libs/'SPI/SPI.cpp',lf/'LittleFS.cpp',lf/'littlefs/lfs.c',lf/'littlefs/lfs_util.c']
obj=build/'objects';obj.mkdir(exist_ok=True)
def compile_one(item):
 index,p=item;o=obj/(str(index)+'_'+p.name+'.o');cpp=p.suffix=='.cpp'
 compiler=arm/('arm-none-eabi-g++' if cpp else 'arm-none-eabi-gcc')
 extra=['-std=gnu++17','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-Wno-psabi'] if cpp else ['-std=gnu11'] if p.suffix=='.c' else ['-x','assembler-with-cpp']
 result=subprocess.run([str(compiler),*flags,*extra,'-c',str(p),'-o',str(o)],capture_output=True,text=True)
 if result.stdout or result.stderr:print(p.name+'\n'+result.stdout+result.stderr,flush=True)
 if result.returncode:raise RuntimeError('Compile failed: '+str(p))
 return o
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
 objects=list(pool.map(compile_one,enumerate(app+core_src+lib_src)))
elf=build/'StepMother_firmware.elf'
cmd=[str(arm/'arm-none-eabi-g++'),*flags,'-Wl,--gc-sections,--no-warn-rwx-segments',
 '-T'+str(core/'imxrt1062_t41.ld'),'-Wl,-Map='+str(build/'StepMother_firmware.map'),'-o',str(elf),
 *map(str,objects),'-lm','-lstdc++']
subprocess.run(cmd,check=True)
subprocess.run([str(arm/'arm-none-eabi-objcopy'),'-O','ihex','-R','.eeprom',str(elf),str(build/'StepMother_firmware.hex')],check=True)
subprocess.run([str(arm/'arm-none-eabi-size'),str(elf)],check=True)
print('PASS Teensy 4.1 target build: 600 MHz, native USB MIDI + 48 kHz audio, HEX generated')
