# Yummy / reference synthesizer comparison — pass 3

This pass contains an experimental Yummy voice implementation, not a
verified 90% match or new flashable firmware. See the package IMPLEMENTATION.md
for the distinction between measured mappings and approximate DSP stages.

## Run locally

From `source/voice`, with a C compiler and Python 3:

```sh
make calibration-test
python3 calibration/calibrate.py render --output calibration/out
```

The supplied `captures/after/` folder already contains the generated 93 cases. Each
is three seconds, mono, 48 kHz, with matching MIDI at 120 bpm and exact normalized
settings in `manifest.json` and `settings.csv`. All cases disable Yummy delay and
use Volume = 0.5. The starting silence is 250 ms. Render the entire 3 seconds,
including that starting silence. Knob values are 0–1: 0.25 means 25%.

## Capture reference synthesizer references: start with twelve cases

Use a legally installed reference synthesizer instance in **MIDI Note mode**, with host tempo
120 bpm. Fix one engine model and oscillator model for the entire batch. Use the
latest reference synthesizer model offered as the initial target, and write down its exact name.
Set reference synthesizer Drive to 50% for this initial profile. Keep emulation trims at factory defaults; disable delay/reverb/external effects,
optional limiter and any host normalization. Keep reference synthesizer volume and drive fixed
across cases; record their settings. Apply any monitoring trim after the plugin.

For each case below:

1. Import its MIDI file at time zero. Route channel 1 to reference synthesizer (or remap the track
   to the channel accepted by your instance). Do not run reference synthesizer's internal pattern
   sequencer at the same time.
2. Set Cutoff, Resonance, Env Mod, Decay and Accent to the values in settings.csv.
   `square=0` means saw and `square=1` means square.
3. Set Distortion to its listed value. The CSV's `type` now means rational soft clipping (0) or sine shaping (1)
   in this pass. Match the corresponding reference synthesizer selector position where available,
   and record its actual visible name. The old tube/Rat labels no longer apply. Cases 001–069 use zero
   distortion, so its mode is irrelevant to those cases.
4. Export 0–3 seconds as mono PCM 24-bit or 32-bit float WAV, 48 kHz, retaining the
   silence. Disable normalization, dither and fades. Name the file exactly as its
   case ID, e.g. `009_filter.wav`. Put it in a new `reference_reference/` directory.

| Case | Purpose |
|---|---|
| 001_filter | Low cutoff, zero resonance |
| 009_filter | Mid cutoff, zero resonance |
| 012_filter | Mid cutoff, full resonance |
| 020_filter | High cutoff, full resonance |
| 041_envmod | Zero envelope amount at low cutoff |
| 045_envmod | Full envelope amount at low cutoff |
| 051_decay | Short normal decay |
| 059_decay | Long normal decay |
| 063_accent | Repeated accents at zero resonance |
| 069_accent | Repeated accents at high resonance |
| 074_distortion | Full distortion, low resonance |
| 090_phrase | Mixed notes, rests and accents |

Save a preset/screenshot of the reference setup and fill `reference_settings.json`
from the provided template. reference synthesizer accent is triggered by MIDI velocity >=100;
MIDI uses 80 for normal notes and 127 for accents. The slide case uses overlapping
notes; calibrate it later, after the basic filter/envelopes match.

There is no assumption that the two instruments have identical knob ranges.
Matching the *same normalized knob values* is the objective of later remapping.
If reference synthesizer has quantized controls, record the nearest actual values rather than
claiming the requested settings were exact.

## Compare

With NumPy and SciPy installed, from `source/voice`:

```sh
python3 calibration/calibrate.py compare --suite ../../captures/after --reference /absolute/path/to/reference_reference --allow-partial
python3 calibration/self_test.py ../../captures/after
```

Use `--latency-samples N` only for a known export/host latency: positive N means
reference synthesizer is delayed by N samples. The comparer does not independently time-warp each
note to hide envelope differences. It accepts mono or identical-channel stereo
and rejects wrong-length, wrong-rate, silent and non-finite files. Do not export
stereo effects; differing stereo channels are rejected.

`comparison.csv` reports one global level difference, magnitude-spectrum error
and 10 ms RMS-envelope error per case. Level is normalized once per file for the
last two metrics. This avoids phase cancellation from free-running oscillators,
but it does not make those metrics a perceptual similarity percentage. A
comparison of a file with itself is a pipeline check, not reference synthesizer validation.
Missing cases are reported; `--allow-partial` explicitly permits the twelve-case
first pass. No synthetic reference synthesizer reference files are supplied.

## What changes next

Fit cutoff and feedback curves first, then the envelope depth/offset and decay,
then the resonance-dependent accent charging/release, and finally the selected
reference synthesizer distortion mode. If the current four-stage Moog-style ladder cannot fit
both resonant and nonresonant cases, replace its core rather than compensating
with extreme knob curves. Assess mono first: reference synthesizer reference behaviour does not
specify what Yummy's four-note paraphony should do.

After a fit, rerun the whole suite with square-wave cases and the phrase/slide
cases, listen at matched levels, and measure real board CPU/underruns. The
RP2040 budget must be checked on hardware before issuing a new UF2.
