// MIDI ahead of the sound while synced: every note-on and note-off goes out
// `lead` samples earlier than with no lead, the notes are the same, and an
// edit made after a step's notes went out ahead is put right when it starts.
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "seq303.h"
typedef struct { long t; uint8_t note, vel; } ev_t;
static ev_t ev[4000]; static int nev; static long now_t;
static void hook(uint8_t note, uint8_t vel) { if (nev < 4000) ev[nev++] = (ev_t){ now_t, note, vel }; }
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static int run(uint32_t lead, ev_t *out, int edit_at_step) {
  voice_t v; voice_init(&v); seq_init(); seq_bpm = 120; seq_ext_lead = 128;
  seq_midi_hook = hook; seq_midi_enabled = true; seq_midi_lead = lead; nev = 0;
  seq_start_ext(&v);
  long t = 0, next_tick = 6000; int c = 0;
  while (c < 48) {
    if (t >= next_tick) { seq_ext_step(&v, (uint32_t)c); c++; next_tick = 6000 + (long)c * 6000; }
    if (edit_at_step >= 0 && c == edit_at_step && t == next_tick - 6000 + 4800) {
      // 1 ms before the tick of step `edit_at_step`, after its notes went ahead: change it
      seq_patterns[0].steps[edit_at_step % 16].pitch += 1;
    }
    now_t = t;
    seq_tick(&v, 16);
    t += 16;
  }
  now_t = t; seq_stop(&v);
  memcpy(out, ev, sizeof(ev_t) * (size_t)nev);
  return nev;
}

int main(void) {
  static ev_t a[4000], b[4000], e[4000];
  int na = run(0, a, -1);
  uint32_t lead = 1440;                       // 30 ms
  int nb = run(lead, b, -1);
  printf("events: no lead %d, lead %d\n", na, nb);
  
  CHECK(na == nb && na > 20);
  int bad = 0; long dmin = 1 << 30, dmax = -(1 << 30);
  for (int i = 0; i < na - 1 && i < nb - 1; i++) {   // the last: note-off at stop
    if (a[i].note != b[i].note || a[i].vel != b[i].vel) bad++;
    if (i < 3) continue;                       // the first steps can't go ahead (no tempo yet)
    long d = a[i].t - b[i].t;
    if (d < dmin) dmin = d;
    if (d > dmax) dmax = d;
  }
  printf("same notes: %s; earlier by %ld..%ld samples (want %u, +-16)\n", bad ? "NO" : "yes", dmin, dmax, lead);
  CHECK(!bad);
  CHECK(dmin >= (long)lead - 16 && dmax <= (long)lead + 16);

  // An edit after the notes went out ahead: step 21 (index 20, a note in the
  // demo) is changed 25 ms before its tick, after its notes went out. Its old note went out ahead; when
  // the step starts, the new one must replace it, and nothing may hang.
  int ne = run(lead, e, 21);
  int newp = seq_patterns[0].steps[5].pitch;          // after the edit (count 21 = index 5 of bar 2)
  long tick20 = 6000 + 21L * 6000;
  int got_new = 0, on[128] = {0}, hang = 0;
  for (int i = 0; i < ne; i++) {
    if (e[i].vel && e[i].note == newp && e[i].t >= tick20 - 1000 && e[i].t <= tick20) got_new = 1;
    if (e[i].vel) on[e[i].note]++; else if (on[e[i].note]) on[e[i].note]--;
  }
  for (int k = 0; k < 128; k++) hang += on[k];
  printf("edit: new note %d sent at the step: %s; hanging notes: %d\n", newp, got_new ? "yes" : "NO", hang);
  CHECK(got_new && hang == 0);
  printf(fails ? "FAILED\n" : "MIDI lead tests passed\n");
  return fails != 0;
}
