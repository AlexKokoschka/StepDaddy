#!/usr/bin/env python3
"""Read channel-16 merged v0.2 diagnostics from an exported Standard MIDI File.
No third-party Python packages needed. Records the Note and CC diagnostics only.
"""
import argparse,json,struct
from pathlib import Path
METRICS={17:'save_sequence',18:'loaded_saved_state',19:'save_aborts',20:'core0_stack_free_bytes',21:'core1_stack_free_bytes',22:'audio_missing_samples',23:'usb_late_packets',24:'usb_midi_tx_drops',25:'audio_event_queue_drops',26:'synth_midi_queue_drops',27:'save_successes',28:'save_errors',29:'save_phase',30:'static_ram_headroom_units16',31:'audio_peak_since_boot_percent'}
METRICS.update({64:'profile_generation',65:'session_peak_percent',66:'session_over_budget_blocks',
 67:'worst_block_events_us',68:'worst_block_params_us',69:'worst_block_sequencer_us',
 70:'worst_block_render_us',71:'worst_block_output_us',72:'session_peak_boot_seconds',
 73:'session_elapsed_seconds',74:'first_five_seconds_peak_percent',75:'after_five_seconds_peak_percent',
 76:'session_peak_us',77:'lifetime_peak_boot_seconds',78:'lifetime_peak_after_first_five_seconds',
 79:'diagnostic_schema',80:'profile_reset_pending'})
METRICS[83]='save_blockers_bits'
METRICS[84]='since_last_activity_units100ms'
def vlq(data,pos):
 value=0
 for _ in range(4):
  b=data[pos];pos+=1;value=(value<<7)|(b&127)
  if b<128:return value,pos
 raise ValueError('Invalid MIDI variable-length value')
def events(data):
 if data[:4]!=b'MThd':raise ValueError('Not a Standard MIDI File')
 size=int.from_bytes(data[4:8],'big');pos=8+size;out=[]
 while pos<len(data):
  tag=data[pos:pos+4];n=int.from_bytes(data[pos+4:pos+8],'big');track=data[pos+8:pos+8+n];pos+=8+n
  if tag!=b'MTrk':continue
  i=0;tick=0;running=0
  while i<len(track):
   delta,i=vlq(track,i);tick+=delta;b=track[i]
   if b>=128:status=b;i+=1
   elif running:status=running
   else:raise ValueError('MIDI running status missing')
   if status==255:
    i+=1;n,i=vlq(track,i);i+=n;continue
   if status in (240,247):
    n,i=vlq(track,i);i+=n;running=0;continue
   if status>=240:raise ValueError('Unsupported MIDI system event')
   running=status;n=1 if status>>4 in (12,13) else 2
   values=track[i:i+n]
   if len(values)!=n or any(v>127 for v in values):raise ValueError('Invalid/truncated MIDI channel event')
   i+=n;out.append((tick,len(out),status,values))
 return sorted(out)
def decode(data,channel=16):
 reports=[];current=None;msb={}
 for tick,_,status,v in events(data):
  if status&15!=channel-1:continue
  if status>>4==9 and v[1]:
   current={'midi_tick':tick,'audio_average_percent':v[0],'audio_peak_last_window_percent':v[1]};reports.append(current);msb={}
  elif status>>4==11 and current is not None:
   cc,value=v
   if cc in METRICS:msb[cc]=value
   elif cc-32 in METRICS and cc-32 in msb:
    key=METRICS[cc-32];val=msb[cc-32]*128+value;current[key]=val
    if key=='static_ram_headroom_units16':current['static_ram_headroom_bytes']=val*16
 return reports
def decode_flattened(data,channel=1):
 # DAW clip exports can move channels, group CCs at clip start, and discard
 # unchanged repeats. Report ONE collapsed CC snapshot, never invented chronology.
 notes=[];cc={}
 for tick,_,status,v in events(data):
  if status&15!=channel-1:continue
  if status>>4==9 and v[1]:notes.append({'midi_tick':tick,'audio_average_percent':v[0],'audio_peak_last_window_percent':v[1]})
  if status>>4==11:cc[v[0]]=v[1]
 metrics={name:cc[k]*128+cc[k+32] for k,name in METRICS.items() if k in cc and k+32 in cc}
 if 'static_ram_headroom_units16' in metrics:metrics['static_ram_headroom_bytes']=metrics['static_ram_headroom_units16']*16
 return {'warning':'Collapsed CC values: per-request timing is unavailable; export each test phase as a separate clip.',
         'cpu_readings':notes,'collapsed_cc_snapshot':metrics}
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('midi',type=Path)
 p.add_argument('--channel',type=int,choices=range(1,17),default=16)
 p.add_argument('--flattened',action='store_true',help='Decode a DAW export that collapses/reorders CC data')
 a=p.parse_args();data=a.midi.read_bytes()
 reports=decode_flattened(data,a.channel) if a.flattened else decode(data,a.channel)
 print(json.dumps(reports,indent=2))
 if not reports:raise SystemExit('No CPU diagnostic notes found on selected channel. For remapped DAW exports use --channel 1 --flattened.')
