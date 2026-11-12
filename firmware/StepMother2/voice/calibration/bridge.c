// Host-only interface for Python's ctypes. Never linked into firmware.
#include "voice303.h"
static voice_t voice;
void calibration_init(float cutoff, float resonance, float envmod, float decay,
                      float accent, float distortion, int type, int square) {
  voice_init(&voice);
  voice_params_t p=voice.p;
  p.cutoff=cutoff; p.resonance=resonance; p.envmod=envmod; p.decay=decay;
  p.accent=accent; p.distortion=distortion; p.dist_type=(uint8_t)type;
  p.wave=square ? WAVE_SQUARE : WAVE_SAW;
  p.volume=0.5f; p.delay_amount=0;
  voice_set_params(&voice,&p);
}
void calibration_note(int note, int accent, int slide) { voice_note_on(&voice,note,accent!=0,slide!=0); }
void calibration_off(void) { voice_note_off(&voice); }
void calibration_render(int16_t *out, int n) { voice_render(&voice,out,n); }
