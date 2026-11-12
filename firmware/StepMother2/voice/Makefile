CC ?= cc
CFLAGS = -O2 -Wall -Wextra -std=c11 -Isrc
SANITIZERS ?= undefined

render: host/render.c src/voice303.c src/voice303.h src/seq303.c src/seq303.h
	$(CC) $(CFLAGS) -o $@ host/render.c src/voice303.c src/seq303.c -lm

# Same, with overflow / undefined-behaviour checks
render_check: host/render.c src/voice303.c src/voice303.h src/seq303.c src/seq303.h
	$(CC) $(CFLAGS) -g -fsanitize=$(SANITIZERS) -fno-sanitize-recover=all -o $@ host/render.c src/voice303.c src/seq303.c -lm

# Sequencer MIDI sent ahead of the sound while synced (seq_midi_lead)
midi_lead_test: host/midi_lead_test.c src/voice303.c src/seq303.c src/seq303.h
	$(CC) $(CFLAGS) -g -fsanitize=$(SANITIZERS) -fno-sanitize-recover=all -o $@ host/midi_lead_test.c src/voice303.c src/seq303.c -lm

# Paraphonic level/drive regression (name retained from the original project)
od_chord_test: host/od_chord_test.c src/voice303.c src/voice303.h src/voice_profile.h
	$(CC) $(CFLAGS) -g -fsanitize=$(SANITIZERS) -fno-sanitize-recover=all -o $@ host/od_chord_test.c src/voice303.c -lm

test: midi_lead_test od_chord_test
	./midi_lead_test
	./od_chord_test

wavs: render
	mkdir -p out && ./render out

clean:
	rm -rf render render_check midi_lead_test od_chord_test knob_behavior_test dsp_curve_test midi_lead_ubsan od_chord_ubsan automation_stress_test out

# Control timing, note semantics and all six-knob extremes (UBSan only).
knob_behavior_test: host/knob_behavior_test.c src/voice303.c src/voice303.h src/voice_profile.h
	$(CC) $(CFLAGS) -g -fsanitize=undefined -fno-sanitize-recover=all -o $@ host/knob_behavior_test.c src/voice303.c -lm

calibration-test: knob_behavior_test
	./knob_behavior_test

calibration:
	python3 calibration/calibrate.py render

.PHONY: test wavs clean calibration calibration-test

dsp_curve_test: host/dsp_curve_test.c src/voice303.c src/voice303.h src/voice_profile.h
	$(CC) $(CFLAGS) -g -fsanitize=undefined -fno-sanitize-recover=all -o $@ host/dsp_curve_test.c -lm

profile-test: knob_behavior_test dsp_curve_test
	./knob_behavior_test
	./dsp_curve_test

.PHONY: profile-test

automation_stress_test: host/automation_stress_test.c src/voice303.c src/voice303.h src/voice_profile.h
	$(CC) $(CFLAGS) -g -fsanitize=undefined -fno-sanitize-recover=all -o $@ host/automation_stress_test.c src/voice303.c -lm

stress-test: automation_stress_test
	./automation_stress_test

.PHONY: stress-test
