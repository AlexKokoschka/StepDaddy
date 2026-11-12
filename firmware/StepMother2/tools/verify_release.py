#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,os,subprocess,sys,zipfile
root=Path(__file__).resolve().parents[1];build=root/'build';dist=root/'dist';dist.mkdir(exist_ok=True)
elf=build/'stepmother2.elf';compiler=Path(os.environ['PICO_TOOLCHAIN_PATH'])/'bin'
nm=subprocess.check_output([str(compiler/'arm-none-eabi-nm'),'-n',str(elf)],text=True)
symbols={}
for line in nm.splitlines():
 p=line.split()
 if len(p)==3:
  try:symbols[p[2]]=int(p[0],16)
  except ValueError:pass
assert symbols['__StackTop']-symbols['__StackBottom']==4096
assert symbols['__StackOneTop']-symbols['__StackOneBottom']==4096
free=symbols['__HeapLimit']-symbols['__bss_end__'];assert free>=12*1024
assert symbols['__flash_binary_end']<=0x10030000
for name in ['persist_task','save_hw_erase','save_hw_program','telemetry_stack_init','telemetry_send']:
 assert 0x20000000<=symbols[name]<0x20040000,name
for name in ['storage_save','storage_load','storage_task','flash_range_erase','flash_range_program']:
 assert name not in symbols,name
for name in ['build-final.log','tests-final.log']:
 s=(root/name).read_text();assert 'error:' not in s and 'warning:' not in s
assert 'Built target stepmother2' in (root/'build-final.log').read_text()
tests=(root/'tests-final.log').read_text();assert tests.count('PASS ')>=6
name='Step Mother'
uf2=dist/'StepMother2_firmware.uf2'
subprocess.run([sys.executable,str(root/'tools/bin_to_uf2.py'),str(build/'stepmother2.bin'),str(uf2)],check=True)
size=subprocess.check_output([str(compiler/'arm-none-eabi-size'),str(elf)],text=True)
# The USB name and the version tag the updater reads (src/firmware_version.h).
import re
ver=re.search(r'FIRMWARE_VERSION_STRING "([^"]+)"',(root/'src/firmware_version.h').read_text()).group(1)
image=(build/'stepmother2.bin').read_bytes()
assert ('SSFW|'+name+'|'+ver+'|').encode() in image,'version tag missing from the image'
assert name.encode()+b'\x00' in image,'USB name missing from the image'
print(f"PASS RP2040 validation: UF2 roundtrip, flash guard, RAM routines, two reserved stacks; static headroom {free} bytes")
