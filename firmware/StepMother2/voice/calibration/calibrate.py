#!/usr/bin/env python3
"""Reproducible Yummy renders + MIDI + settings; optional reference synthesizer spectral comparison.
Render needs Python 3 and a C compiler. Compare additionally needs NumPy/SciPy.
"""
import argparse
import array
import csv
import ctypes as ct
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import wave

ROOT = Path(__file__).resolve().parents[1]
RATE, LENGTH, PPQ = 48000, 144000, 960  # three seconds, 120 bpm
KNOBS = ('cutoff', 'resonance', 'envmod', 'decay', 'accent', 'distortion')


def cases():
    jobs = []
    def add(group, **kw):
        p = dict(cutoff=.3, resonance=.7, envmod=.6, decay=.4,
                 accent=.7, distortion=0., type=0, square=0,
                 scenario='held', note=45)
        p.update(kw)
        p['id'] = f'{len(jobs)+1:03d}_{group}'
        jobs.append(p)
    for square in (0, 1):
        for cut in (0., .25, .5, .75, 1.):
            for res in (0., .5, .9, 1.):
                add('filter', cutoff=cut, resonance=res, envmod=0., accent=0., square=square)
    for cut in (.2, .6):
        for env in (0., .25, .5, .75, 1.):
            add('envmod', cutoff=cut, envmod=env, accent=0., decay=.5)
    for decay in (0., .25, .5, .75, 1.):
        for accented in (False, True):
            add('decay', decay=decay, scenario='accent_held' if accented else 'held')
    for res in (0., .5, .95):
        for accent in (0., .5, 1.):
            add('accent', resonance=res, accent=accent, scenario='accents')
    for typ in (0, 1):
        for res in (.2, .9):
            for dist in (0., .25, .5, .75, 1.):
                add('distortion', type=typ, resonance=res, distortion=dist,
                    envmod=0., accent=0.)
    add('phrase', scenario='phrase')
    add('phrase_square', scenario='phrase', square=1)
    add('phrase_distortion', scenario='phrase', distortion=.5)
    add('slide', scenario='slide')
    return jobs


def events(p):
    # (sample, on/off, note, velocity). Same events generate MIDI and Yummy audio.
    start, step = 12000, 6000
    kind = p['scenario']
    if kind in ('held', 'accent_held'):
        return [(start, 1, p['note'], 127 if kind == 'accent_held' else 80),
                (start+96000, 0, p['note'], 0)]
    ev = []
    phrase = (45,45,57,55,None,45,48,51,45,45,57,45,43,None,48,50)
    for i in range(16):
        note = (45 if kind == 'accents' else (45,52,48,55)[i%4] if kind=='slide'
                else phrase[i])
        if note is None:
            continue
        accent = i < 8 if kind == 'accents' else i in (0,5,10,14)
        t = start+i*step
        duration = step+48 if kind == 'slide' and i<15 else step//2
        ev.extend(((t,1,note,127 if accent else 80), (t+duration,0,note,0)))
    return sorted(ev, key=lambda e: (e[0], e[1]))


def vlq(n):
    b = [n&127]
    while n >> 7:
        n >>= 7
        b.append((n&127)|128)
    return bytes(reversed(b))


def write_midi(path, ev):
    data = bytearray(b'\x00\xff\x51\x03\x07\xa1\x20') # 120 bpm
    data += b'\x00\xff\x58\x04\x04\x02\x18\x08'
    prev = 0
    for sample, on, note, vel in ev:
        tick = round(sample * PPQ / (RATE*.5))
        data += vlq(tick-prev) + bytes((0x90 if on else 0x80,note,vel))
        prev = tick
    end = round(LENGTH * PPQ/(RATE*.5))
    data += vlq(end-prev) + b'\xff\x2f\x00'
    path.write_bytes(b'MThd'+struct.pack('>IHHH',6,0,1,PPQ)+b'MTrk'+struct.pack('>I',len(data))+data)


def load_engine(build, source):
    build.mkdir(parents=True, exist_ok=True)
    lib = build/'yummy_calibration.so'
    command = shlex.split(os.environ.get('CC','cc'))
    command += ['-O2','-std=c11','-Wall','-Wextra','-fPIC',
                '-dynamiclib' if sys.platform == 'darwin' else '-shared',
                '-I'+str(source),str(ROOT/'calibration/bridge.c'),
                str(source/'voice303.c'),'-lm','-o',str(lib)]
    subprocess.run(command, check=True)
    dll = ct.CDLL(str(lib.resolve()))
    dll.calibration_init.argtypes = [ct.c_float]*6+[ct.c_int]*2
    dll.calibration_note.argtypes = [ct.c_int]*3
    dll.calibration_off.argtypes = []
    dll.calibration_render.argtypes = [ct.POINTER(ct.c_int16),ct.c_int]
    for name in ('calibration_init','calibration_note','calibration_off','calibration_render'):
        getattr(dll,name).restype=None
    return dll


def render(args):
    out = args.output.resolve()
    for d in ('yummy','midi'):
        (out/d).mkdir(parents=True, exist_ok=True)
    source = args.source_dir.resolve()
    dll = load_engine(out/'build',source)
    jobs = cases()
    manifest = dict(sample_rate=RATE, frames=LENGTH, bpm=120, volume=.5,
                    delay=0, normal_velocity=80, accent_velocity=127,
                    source_sha256=hashlib.sha256((source/'voice303.c').read_bytes()).hexdigest(),
                    similarity_status='Yummy pass 3, uncalibrated: no reference synthesizer reference recordings', cases=jobs)
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    with (out/'settings.csv').open('w', newline='') as f:
        writer=csv.DictWriter(f, fieldnames=list(jobs[0]))
        writer.writeheader(); writer.writerows(jobs)
    # A reusable 16-sample transfer buffer; events split it when needed.
    block=(ct.c_int16*16)()
    for p in jobs:
        dll.calibration_init(*(p[k] for k in KNOBS),p['type'],p['square'])
        ev=events(p); write_midi(out/'midi'/f"{p['id']}.mid",ev)
        audio=array.array('h'); pos=0; held=[]
        for target,on,note,vel in ev+[(LENGTH,-1,0,0)]:
            while pos<target:
                n=min(16,target-pos)
                dll.calibration_render(block,n)
                audio.extend(block[:n]); pos+=n
            if on==1:
                dll.calibration_note(note,int(vel>=100),int(bool(held)))
                held.append(note)
            elif on==0:
                if note in held: held.remove(note)
                if not held: dll.calibration_off()
        if sys.byteorder != 'little': audio.byteswap()
        with wave.open(str(out/'yummy'/f"{p['id']}.wav"),'wb') as f:
            f.setnchannels(1); f.setsampwidth(2); f.setframerate(RATE)
            f.writeframes(audio.tobytes())
    print(f'Rendered {len(jobs)} cases to {out}; MIDI and exact settings included.')


def read_audio(path):
    import numpy as np
    from scipy.io import wavfile
    sr, a=wavfile.read(path)
    if sr!=RATE: raise ValueError(f'{path}: expected 48000 Hz, got {sr}')
    if a.dtype.kind in 'iu':
        if a.dtype.kind=='u': a=a.astype(np.float64)-128; a/=128
        else: a=a.astype(np.float64)/(2.**(a.dtype.itemsize*8-1))
    else: a=a.astype(np.float64)
    if a.ndim==2:
        if a.shape[1]!=2: raise ValueError(f'{path}: expected mono or duplicated mono stereo')
        if np.max(np.abs(a[:,0]-a[:,1]))>1e-5:
            raise ValueError(f'{path}: stereo channels differ; export mono without stereo effects')
        a=a[:,0]
    if not np.all(np.isfinite(a)): raise ValueError(f'{path}: non-finite samples')
    if len(a)!=LENGTH: raise ValueError(f'{path}: expected {LENGTH} frames, got {len(a)}')
    if np.max(np.abs(a))<1e-6: raise ValueError(f'{path}: silent file')
    return a


def compare(args):
    import numpy as np
    from scipy.signal import stft
    out=args.suite.resolve()
    manifest=json.loads((out/'manifest.json').read_text())
    rows=[]; missing=[]
    frequencies=np.fft.rfftfreq(2048,1/RATE)
    mask=(frequencies>=40)&(frequencies<=18000)
    def rms_envelope(a):
        a=a[12000:120000]
        return np.sqrt(np.mean(a.reshape(-1,480)**2,axis=1)+1e-15)
    for p in manifest['cases']:
        name=p['id']; ref=args.reference/f'{name}.wav'
        if not ref.exists(): missing.append(name); continue
        a=read_audio(out/'yummy'/f'{name}.wav'); b=read_audio(ref)
        # Apply known host latency only; don't independently align/warp envelopes.
        lag=args.latency_samples
        if abs(lag)>=12000: raise ValueError('Latency must be under 250 ms')
        if lag>0: b=np.pad(b[lag:],(0,lag))
        elif lag<0: b=np.pad(b,( -lag,0))[:LENGTH]
        slice_=slice(12000,120000)
        ar=float(np.sqrt(np.mean(a[slice_]**2))); br=float(np.sqrt(np.mean(b[slice_]**2)))
        if min(ar,br)<1e-7: raise ValueError(f'{name}: silent comparison window')
        a/=ar; b/=br  # Remove one global level difference, preserve envelope shape.
        _,_,az=stft(a,fs=RATE,nperseg=2048,noverlap=1536,boundary=None)
        _,_,bz=stft(b,fs=RATE,nperseg=2048,noverlap=1536,boundary=None)
        # Same time/frequency bins, amplitude floor -60 dB relative to joint peak.
        am=np.abs(az)[mask]; bm=np.abs(bz)[mask]
        floor=max(am.max(),bm.max())*.001
        active=(am>floor)|(bm>floor)
        delta=20*np.log10(np.maximum(am,floor))-20*np.log10(np.maximum(bm,floor))
        ae=rms_envelope(a); be=rms_envelope(b)
        env_db=20*np.log10(np.maximum(ae,.001))-20*np.log10(np.maximum(be,.001))
        rows.append(dict(id=name,level_delta_db=20*math.log10(ar/br),
                         active_spectral_mae_db=float(np.mean(np.abs(delta[active]))),
                         envelope_mae_db=float(np.mean(np.abs(env_db))),
                         yummy_peak=float(np.max(np.abs(a))*ar),
                         reference_peak=float(np.max(np.abs(b))*br)))
    report=out/'comparison.csv'
    if rows:
        with report.open('w',newline='') as f:
            w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    summary=dict(compared=len(rows),missing=missing,latency_samples=args.latency_samples,
                 interpretation='Diagnostic errors only; no percent-similarity claim. Lower errors are better.')
    (out/'comparison_status.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(f'Compared {len(rows)}; missing {len(missing)}. Results: {report}')
    if not rows: raise SystemExit('No reference cases found; no similarity score calculated.')
    if missing and not args.allow_partial: raise SystemExit('Incomplete reference set; use --allow-partial for an intentional subset.')


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    sub=ap.add_subparsers(required=True)
    p=sub.add_parser('render');p.add_argument('--output',type=Path,default=ROOT/'calibration/out')
    p.add_argument('--source-dir',type=Path,default=ROOT/'src');p.set_defaults(func=render)
    p=sub.add_parser('compare');p.add_argument('--suite',type=Path,default=ROOT/'calibration/out')
    p.add_argument('--reference',type=Path,required=True)
    p.add_argument('--latency-samples',type=int,default=0)
    p.add_argument('--allow-partial',action='store_true');p.set_defaults(func=compare)
    args=ap.parse_args();args.func(args)

if __name__=='__main__': main()
