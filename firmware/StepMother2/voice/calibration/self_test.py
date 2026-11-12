#!/usr/bin/env python3
"""Verify generated WAV/MIDI pairs and the comparison pipeline using synthetic references."""
import argparse
import csv
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import wave
import numpy as np
from scipy.io import wavfile
import calibrate as c

p=argparse.ArgumentParser();p.add_argument('suite',type=Path);args=p.parse_args()
suite=args.suite.resolve()
manifest=json.loads((suite/'manifest.json').read_text())

def read_vlq(data, pos):
    v=0
    while True:
        b=data[pos];pos+=1;v=(v<<7)|(b&127)
        if b<128:return v,pos

for case in manifest['cases']:
    name=case['id']
    c.read_audio(suite/'yummy'/f'{name}.wav')
    midi=(suite/'midi'/f'{name}.mid').read_bytes()
    assert midi[:4]==b'MThd' and midi[14:18]==b'MTrk'
    data=midi[22:]; assert len(data)==int.from_bytes(midi[18:22],'big')
    pos=0;tick=0;ev=[]
    while pos<len(data):
        delta,pos=read_vlq(data,pos);tick+=delta
        status=data[pos];pos+=1
        if status==255:
            meta=data[pos];pos+=1
            n,pos=read_vlq(data,pos);pos+=n
            if meta==47:assert tick==5760
        else:
            note,vel=data[pos:pos+2];pos+=2
            ev.append((tick,status==0x90,note,vel))
    expected=[(round(t/25),bool(on),note,vel) for t,on,note,vel in c.events(case)]
    assert ev==expected,name
print(f"PASS {len(manifest['cases'])} WAVs and MIDI event timelines")

# A synthetic test is deliberately separate from reference synthesizer evidence.
with tempfile.TemporaryDirectory() as temp:
    root=Path(temp);(root/'yummy').mkdir();ref=root/'references';ref.mkdir()
    selected=[manifest['cases'][i] for i in (8,44,68)]
    (root/'manifest.json').write_text(json.dumps({'cases':selected}))
    for case in selected:
        name=case['id'];a=c.read_audio(suite/'yummy'/f'{name}.wav')
        wavfile.write(root/'yummy'/f'{name}.wav',c.RATE,a.astype(np.float32))
        shifted=np.pad(a*.5,(96,0))[:c.LENGTH]
        wavfile.write(ref/f'{name}.wav',c.RATE,shifted.astype(np.float32))
    c.compare(SimpleNamespace(suite=root,reference=ref,latency_samples=96,allow_partial=False))
    rows=list(csv.DictReader((root/'comparison.csv').open()))
    assert len(rows)==3
    for row in rows:
        assert abs(float(row['level_delta_db'])-6.0206)<.001,row
        assert float(row['active_spectral_mae_db'])<.001,row
        assert float(row['envelope_mae_db'])<.001,row
    wavfile.write(ref/'silent.wav',c.RATE,np.zeros(c.LENGTH,dtype=np.float32))
    try:c.read_audio(ref/'silent.wav')
    except ValueError:pass
    else:raise AssertionError('Silent reference accepted')
    wavfile.write(ref/'short.wav',c.RATE,np.zeros(10,dtype=np.float32))
    try:c.read_audio(ref/'short.wav')
    except ValueError:pass
    else:raise AssertionError('Wrong-length reference accepted')
print('PASS comparison: known 6.0206 dB gain, 96-sample latency, silence/length rejection')
