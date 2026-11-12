from pathlib import Path
import importlib.util,struct
p=Path(__file__).parents[1]/'tools/decode_diagnostics.py'
spec=importlib.util.spec_from_file_location('decoder',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
# Real SMF structure, delta times, metadata and MIDI running status.
track=bytes([0,0x99,36,100,0,0x9F,63,83,0,0x8F,63,0,0,0xBF,20,12,0,52,0,0,21,16,0,53,0,0,31,0,0,63,83,0,0xFF,0x2F,0])
smf=b'MThd'+struct.pack('>IHHH',6,0,1,480)+b'MTrk'+struct.pack('>I',len(track))+track
r=m.decode(smf);assert len(r)==1
assert r[0]['audio_average_percent']==63 and r[0]['audio_peak_last_window_percent']==83
assert r[0]['core0_stack_free_bytes']==1536 and r[0]['core1_stack_free_bytes']==2048
assert r[0]['audio_peak_since_boot_percent']==83
# New timing CC pairs and explicit collapsed/remapped DAW export support.
track2=bytes([0,0x90,63,79,0,0xB0,65,1,0,97,19,0,68,2,0,100,44,0,79,0,0,111,2,0,0xFF,0x2F,0])
smf2=b'MThd'+struct.pack('>IHHH',6,0,1,480)+b'MTrk'+struct.pack('>I',len(track2))+track2
assert m.decode(smf2)==[]
r=m.decode(smf2,1);assert r[0]['session_peak_percent']==147
assert r[0]['worst_block_params_us']==300 and r[0]['diagnostic_schema']==2
f=m.decode_flattened(smf2,1);assert f['collapsed_cc_snapshot']['session_peak_percent']==147
assert len(f['cpu_readings'])==1 and 'warning' in f
print('PASS MIDI diagnostic decoder: SMF, running status, channel filtering and 14-bit metrics.')
