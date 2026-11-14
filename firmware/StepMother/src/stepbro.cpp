// StepBro sequencer: one stream, Pico shared I/O, combined stopped-only persistence.
#include "bro_port.h"
#include "persist.h"
namespace bro {
inline uint16_t readKnob();
inline uint8_t streamOfSlot(uint8_t globalSlot);
inline uint8_t slotForPad(uint8_t padIndex);
inline void toggleDimension();
bool isPlayingNow();
inline bool patternSlotOverlayActive();
inline void tryDimensionChord();
inline void tryBankClearChord();
inline bool anyNoteRepeatActive();
inline uint8_t streamLoopLen(uint8_t s);
inline uint8_t globalLoopLen();
bool rawKeyDown(int8_t key);
void checkBootloaderCombo();
void setup();
void loop();
void scanMatrixAndDispatch();
void printKeyName(int8_t key);
void maskStuckKeysAtBoot();
void onKeyEvent(int8_t key, bool pressed);
void handleBankPress();
void copyCurrentBankToOtherBank();
void clearCurrentBank();
void recordLiveHit(uint8_t voice);
void handleNotePad(uint8_t padIndex, bool pressed);
void handlePatternPad(uint8_t padIndex, bool pressed);
void handlePatternHeldPad(uint8_t padIndex);
void requestSlotSwitch(uint8_t s, uint8_t targetSlot);
void applySlotSwitch(uint8_t s, uint8_t targetSlot);
void copySlot(uint8_t src, uint8_t dst);
void handleShiftPlayOverlayPad(uint8_t padIndex);
void clearVoice(uint8_t voice);
void clearAllVoices();
void clearSlot(uint8_t slot);
void recalcHasContent(uint8_t slot);
void unmuteAll();
void recalcLoopLengthFor(uint8_t s);
void recalcLoopLength();
bool anyPatternPadHeld();
void handleKnob();
void handlePlayStopPress();
void onMidiClock();
void resetMidiLead();
void serviceMidiLead();
void loadMidiLead();
void saveMidiLead();
void onMidiStart();
void onMidiContinue();
void onMidiStop();
void advancePlayhead();
inline uint8_t playSlot(uint8_t s);
void chainSelect(uint8_t s, uint8_t slot);
void startChain(uint8_t s);
uint8_t streamStepFor(uint8_t s, uint8_t thisStep);
void enterChainEdit();
void chainCancel(uint8_t s);
void finishChainEdit();
void chainEditPad(uint8_t padIndex);
void handlePendingSwingTrigger();
void suppressNextPlayback(uint8_t voice, uint8_t step);
bool consumeSuppression(uint8_t voice, uint8_t step);
void triggerStreamStep(uint8_t s, uint8_t localStep);
float shuffleDelayMsForStep(bool offBeat, uint8_t shufflePercent, uint32_t stepMs);
void allNotesOff();
uint8_t midiNoteForVoice(uint8_t voice);
uint32_t gateMsForStream(uint8_t s);
void sendVoiceNoteOn(uint8_t s, uint8_t voice, uint8_t velocity);
void sendVoiceNoteOff(uint8_t s, uint8_t voice);
void servicePendingNoteOffs();
void flushStreamNotes(uint8_t s);
void playVoiceNote(uint8_t voice, uint8_t velocity);
void handleNoteRepeatPad(uint8_t padIndex, bool pressed);
void fireNoteRepeats(int16_t globalStep);
void serviceNoteRepeat();
void setLed(uint8_t index, uint8_t brightness);
void ledFastPathBegin();
inline void setBankLed(uint8_t level);
void runBootLedSequence();
void handleLedBringupSerial();
void refreshLeds();
bool bankHasContent(uint8_t bank);
bool voiceHasContent(uint8_t voice);
void markDirty();
void maybeAutosave();
void forceSave();
bool stateLooksValid();
void sanitizeLoadedState();
void loadStateFromFlash();
void initDefaultState();
const uint8_t PIN_ROWS[5] = {0, 1, 2, 3, 4};
const uint8_t PIN_COLS[5] = {5, 6, 7, 8, 9};
const uint8_t PIN_KNOB = 14; // bounded rotary pot

// PIN_I2C_SDA / PIN_I2C_SCL are defined per board in board_config.h.

const uint16_t ADC_MAX = 1023; // 10-bit default; change if you reconfigure resolution

// v26.8: every knob read goes through here. setup() sets the resolution to
// match ADC_MAX explicitly rather than trusting the core's default: on the
// Step Mother the combined firmware's power-up menu leaves the ADC at Yummy's
// 12 bits, which made every knob control run four times past its range —
// velocity wrapped to 0 (erasing the step) and swing went past 100%. The
// constrain() is the belt to that brace: map() below can never leave its
// range whatever the ADC is set to.
inline uint16_t readKnob() {
  return merged_knob() >> 2;
}

// ---------------------------------------------------------------------------
// KEY IDENTIFIERS — matches Appendix A of the design brief exactly
// (Row 1: Note, Pattern, Shift, Mute, unused. Row 3 Col E: Bank, not Row 1.)
// ---------------------------------------------------------------------------
#define K_NONE         -1
#define K_NOTE          0
#define K_PATTERN       1
#define K_SHIFT         2
#define K_MUTE          3
#define K_PAD_BASE     10   // 10..25 = pad 1..16 (index 0..15)
#define K_BANK         30
#define K_CLEAR        31
#define K_PLAY         32

const int8_t keyMap[5][5] = {
  {K_NOTE,           K_PATTERN,        K_SHIFT,          K_MUTE,           K_NONE},
  {K_PAD_BASE + 0,   K_PAD_BASE + 1,   K_PAD_BASE + 2,   K_PAD_BASE + 3,   K_NONE},
  {K_PAD_BASE + 4,   K_PAD_BASE + 5,   K_PAD_BASE + 6,   K_PAD_BASE + 7,   K_BANK},
  {K_PAD_BASE + 8,   K_PAD_BASE + 9,   K_PAD_BASE + 10,  K_PAD_BASE + 11,  K_CLEAR},
  {K_PAD_BASE + 12,  K_PAD_BASE + 13,  K_PAD_BASE + 14,  K_PAD_BASE + 15,  K_PLAY},
};
const uint8_t MAX_KEY_INDEX = 33; // highest key id (K_PLAY) + 1

const int8_t BASE_MIDI_NOTE = 36; // C1 — pad 1 of bank 0, per Section 3.2.
// Calibrated against Ableton Live's default note naming (note 60 = "C3"),
// confirmed empirically: pad 1 sent note 24 and displayed as "C0" in Ableton
// before this fix — one octave low relative to the intended "C1" labeling.

// ---------------------------------------------------------------------------
// SEQUENCER STATE
// ---------------------------------------------------------------------------
const uint8_t NUM_VOICES = 32;
const uint8_t MAX_STEPS = 32;
const uint8_t DEFAULT_VELOCITY = 100; // used for both manual step toggle-on and initial voice velocity

// ---------------------------------------------------------------------------
// STREAMS (v26) — three MIDI channels playing at once
// ---------------------------------------------------------------------------
// The device drives three independent streams simultaneously, laid out across
// two UI "dimensions" because there is only one 4x4 grid to show them on:
//
//   dimension 0 : stream A  — 16 slots, the drum stream, pads 1-16
//   dimension 1 : stream B  —  8 slots, pads 1-8
//               : stream C  —  8 slots, pads 9-16
//
// Shift+Play+Bank toggles between dimensions. The 16/8/8 split is deliberate:
// this is primarily a drum sequencer, so stream A gets the pattern budget.
//
// activeSlot[] and every other slot index in this file is a GLOBAL slot
// number (0..31). STREAM_BASE maps a stream to where its slots start.
// ---------------------------------------------------------------------------
const uint8_t NUM_STREAMS = 1;
const uint8_t NUM_SLOTS = 16;                                  // 16 + 8 + 8
const uint8_t STREAM_BASE[NUM_STREAMS]  = {0};
const uint8_t STREAM_SLOTS[NUM_STREAMS] = {16};
const uint8_t STREAM_DEFAULT_CHANNEL[NUM_STREAMS] = {1};

// Which stream owns a given global slot.
inline uint8_t streamOfSlot(uint8_t globalSlot) { return 0;
}

// One byte per step: 0 = inactive, 1..127 = active at that velocity.
struct PatternSlot {
  uint8_t steps[NUM_VOICES][MAX_STEPS];
  uint8_t shuffle;         // 50..75 (%), Section 3.4
  uint8_t gatePercent;     // v26.1: note length as a % of one step, 5..100.
                           // Per pattern, like shuffle — so a bass pattern can
                           // hold long notes while a drum pattern stays tight,
                           // and since each stream has its own active slot it
                           // is effectively per stream too.
  uint8_t noteBankWindow;  // 0 or 1 — persisted Note-mode voice window, Section 3.2
  bool hasContent;         // true if any step in any voice is active — drives the
                            // dim/off distinction in the Pattern-held slot overlay
};

struct SavedState {
  uint32_t magic;                            // sanity check on load
  uint16_t version;                          // v26: layout version — see below
  PatternSlot slots[NUM_SLOTS];
  uint8_t lastActiveSlot[NUM_STREAMS];       // global slot index, per stream
  uint8_t streamChannel[NUM_STREAMS];        // 1..16, per stream (Section 3.12)
};

// v26: "SEQ4". The layout changed (16 -> 32 slots, per-stream channels), and
// loadStateFromFlash() requires an exact size match before it will read, so
// every v25 save is discarded on first boot of v26. That wipe was accepted
// deliberately — nothing had shipped and there were no patterns worth keeping.
//
// SAVE_VERSION exists so this never has to be a wipe again. The old scheme
// could only tell "this file is wrong", never "this file is old". With a
// version number, a future layout change can read the previous one and
// migrate it instead of throwing it away.
const uint32_t SAVE_MAGIC = 0x53455134; // "SEQ4"
const uint16_t SAVE_VERSION = 2; // v26.1 added PatternSlot::gatePercent
SavedState state;

// ---------------------------------------------------------------------------
// PER-STREAM RUNTIME STATE (v26)
// ---------------------------------------------------------------------------
// Everything a stream needs in order to be edited and played independently.
// Indexed by stream (0 = A, 1 = B, 2 = C). Editing always targets
// currentStream; playback iterates all three.
// ---------------------------------------------------------------------------
uint8_t dimension = 0;        // 0 = stream A, 1 = streams B and C
uint8_t currentStream = 0;    // the stream the grid is currently editing
uint8_t lastStreamInDim2 = 1; // so toggling back into dimension 1 lands where
                              // you left off rather than always on B

uint8_t activeSlot[NUM_STREAMS] = {0}; // GLOBAL slot index per stream
int8_t pendingSlotSwitch[NUM_STREAMS] = {-1}; // awaiting bar boundary
int8_t armedCopySource = -1;  // -1 = none, else GLOBAL source slot

// Voice mutes, indexed by GLOBAL SLOT (v26.5 — was per stream).
//
// Per stream was already right as far as it went: muting the bassline must not
// silence the kick. But it also meant a mute followed you from one pattern to
// the next, so muting the hats in pattern 1 silenced them in patterns 2-16 as
// well, which is not what "mute this pattern's hats" means to anyone.
//
// Indexing by slot gives per-pattern mutes AND keeps the per-stream property
// for free, since no two streams share a slot number (STREAM_BASE partitions
// 0..31). Costs 1KB of RAM, which both chips have in abundance.
//
// Still deliberately NOT persisted — see streamMuted below for why.
bool mutedVoice[NUM_SLOTS][NUM_VOICES] = {{false}};

// Whole-stream mutes (Shift+Mute). Deliberately NOT persisted — powering on
// to silence because a stream was left muted last week is a bad first five
// seconds.
bool streamMuted[NUM_STREAMS] = {false};

// Which GLOBAL slot does grid pad p address, given the dimension we are in?
// Dimension 0 shows stream A across all 16 pads. Dimension 1 splits the grid:
// pads 1-8 are stream B, pads 9-16 are stream C.
inline uint8_t slotForPad(uint8_t padIndex) {
  return padIndex;
}

// Shift+Play+Bank. Dimension 0 is always stream A; dimension 1 returns to
// whichever of B or C was last edited, so toggling out and back does not
// lose your place.
inline void toggleDimension() {
  // Level switching is owned by the merged panel router.
}

// v25.1 note gate, per stream as of v26: the same voice number is a different
// destination on each stream's channel, so each needs its own deadline.
// See the NOTE GATE block further down for why this exists at all. Declared
// here, with the rest of the per-voice state, because the pad dispatcher
// reads it and that sits earlier in the file than the note helpers do.
// v26.1: the gate is a PERCENTAGE of one step, not a fixed millisecond value.
// A fixed 15ms made every note a staccato blip whatever the tempo: inaudible
// on drums (one-shot samplers discard note length) but a stab on a synth.
// stepDurationEstimateMs is already measured step-to-step and averaged,
// specifically to survive USB clock jitter, so a percentage is both cheap and
// tempo-correct where any fixed value is wrong at one end of the range.
const uint8_t  GATE_PERCENT_DEFAULT = 80;  // near-legato; musical for synths
const uint8_t  GATE_PERCENT_MIN     = 5;
const uint8_t  GATE_PERCENT_MAX     = 100;
const uint16_t NOTE_GATE_MIN_MS     = 10;  // floor: always open an envelope
const uint16_t AUDITION_GATE_MS     = 150; // when there is no tempo to measure
uint32_t voiceNoteOffAt[NUM_STREAMS][NUM_VOICES] = {{0}}; // 0 = none pending

bool voiceHeldLive[NUM_STREAMS][NUM_VOICES] = {{false}}; // true while a
                                           // Note-mode pad is physically held
                                           // for that voice — stops playback
                                           // retriggering a note already
                                           // sounding live

enum GridMode { MODE_NOTE, MODE_PATTERN };
GridMode gridMode = MODE_NOTE; // default

// v26: per stream, so switching dimension and back finds the grid exactly as
// you left it rather than resetting.
uint8_t lastSelectedVoice[NUM_STREAMS] = {0};   // 0..31
uint8_t noteBankWindow[NUM_STREAMS] = {0};      // 0 or 1 — live copy of the active slot's saved value
uint8_t currentStepBank[NUM_STREAMS] = {0};     // 0 or 1 — which 16-step window Pattern mode shows (not persisted)

// Transport
bool dawPlaying = false;
bool locallyStopped = false;
uint8_t playhead = 0;
uint8_t lastTriggeredStep = 0; // the step whose content actually fired most
                                // recently — see triggerStep(). playhead
                                // itself already points at the NEXT step by
                                // the time a trigger fires.
uint32_t lastTriggeredStepMillis = 0; // wall-clock time triggerStep() last ran —
                                       // NOT the same as when advancePlayhead()
                                       // decided to trigger it, since a swung
                                       // (delayed) step's actual trigger can run
                                       // later than that decision point.
uint32_t clockTickCount = 0;
const uint8_t TICKS_PER_STEP = 6; // 24 MIDI clock ticks/quarter, 16th-note steps

// For live-record quantization: when the current step actually started, and
// a running estimate of how long one step lasts at the current tempo.
uint32_t lastStepAdvanceMillis = 0;
uint32_t stepDurationEstimateMs = 0;

// v26.9: MIDI LEAD. Every note StepBro plays from the sequencer used to go
// out when the clock tick for its step ARRIVED, so it reached the DAW late by
// the whole round trip (clock out over USB, note back over USB, through the
// DAW's own buffers) and recorded late — about 30ms at 120 BPM into Ableton.
// Now each step is sent midiLeadMs BEFORE its tick is due: the tick spacing
// is measured, the step's due time predicted from the latest tick, and the
// step fired early from loop() (serviceMidiLead()). When the tick does come
// it only re-syncs, so the timing never drifts. The same idea as Yummy's
// MIDI lead.
//
// Set with Play + knob while the DAW is stopped: 0..16ms in 1ms steps, shown
// as one pad per ms (v26.9.1; 26.9 had 0..48ms in 3ms steps, too coarse —
// the right value sat between two settings). Saved in its own file (LEAD_PATH), so the pattern save
// layout is untouched. Only sequenced steps (and note-repeat hits, which ride
// the same step) can be early; a pad played live goes out when it is played.
const uint8_t MIDI_LEAD_STEP_MS    = 1;   // USB MIDI moves in 1ms frames: no finer
const uint8_t MIDI_LEAD_MAX_MS     = 16;  // 16 pads x 1ms
const uint8_t MIDI_LEAD_DEFAULT_MS = 4;   // 0.3.6: 4 ms, chosen by listening in Ableton (was 8)
uint8_t midiLeadMs = MIDI_LEAD_DEFAULT_MS;
bool midiLeadDirty = false;               // changed since the last save
uint16_t playKnobBaseline = 0;            // knob deadzone for Play+knob
bool playKnobMoved = false;
uint32_t lastBoundaryMicros = 0;          // when the last step's tick came
uint32_t lastTickMicros = 0;              // when the last clock tick came (0 = none yet)
uint32_t stepDurationEstimateUs = 0;      // the same estimate, in microseconds
bool stepFiredEarly = false;              // the coming step already went out
uint8_t leadOfLastAdvance = 0;            // ms early the last step was fired

// Non-blocking swing scheduling: an off-beat step's actual trigger (audio +
// visual flash) is delayed by shuffleDelayMsForStep(), but we can't just
// call delay() and block loop() for that long — that's the exact freezing
// risk fixed earlier. Instead the delayed trigger is scheduled here and
// fired from the main loop once its time arrives.
// v26: per stream, because shuffle is per stream — two streams can owe their
// swung hit at different moments within the same global step.
int16_t pendingTriggerStep[NUM_STREAMS] = {-1};
uint32_t pendingTriggerAtMillis[NUM_STREAMS] = {0};

bool isPlayingNow() { return dawPlaying && !locallyStopped; }
// 0.3.3: Play pressed while locally stopped arms a restart on the next beat
// (Play blinks); pressing again cancels. The timeline keeps counting while
// locally stopped, so the restart lands on the step the DAW is on.
bool resumePending = false;
// 0.3.3: density, set by the knob on its own. 1023 = every hit plays; turning
// left drops the weakest hits first (lowest velocity, then off-beats), down
// to the strongest eighth of the pattern. Not saved; power-up is full.
uint16_t densityLevel = 1023;
bool densityKnobMoved = false;
bool densityRearm = true;                  // take a new baseline on the next knob pass
uint16_t densityKnobBaseline = 0;

// Held-modifier tracking
bool shiftHeld = false;
bool muteHeld = false;
bool clearHeld = false;
bool patternEngagedDuringClearHold = false; // true if Pattern was held at any point
                                             // during the current Clear hold, in
                                             // either press order — see K_CLEAR /
                                             // K_PATTERN handlers and
                                             // handlePatternHeldPad()
bool patternHeld = false;
bool playHeld = false;

// v26.5: Pattern held in NOTE mode opens the slot overlay.
//
// Until v26.5, Pattern's press edge in note mode switched straight to Pattern
// mode and set patternHeld = false, so the slot overlay was unreachable
// without first being in Pattern mode. Holding Pattern to "go and pick a
// different pattern" instead showed the step grid of whichever voice was last
// selected — the right keypress for the wrong screen.
//
// Now the press edge only starts a hold, and the gesture is resolved on
// release by how long the key was down:
//
//   quick tap  -> enter Pattern mode (the old behaviour, unchanged)
//   long hold  -> slot overlay while held; stay in Note mode on release
//
// Staying in Note mode after a long hold is the point: the whole reason to
// reach for a different pattern from note mode is to carry on playing notes
// into it, so dropping into the step editor afterwards would be a detour.
// v26.5.1: 250ms felt like a second in use. Measured, it never was — the
// overlay appears at the threshold plus at most one LED refresh (30ms), so
// ~280ms worst case. What made it feel slow is that NOTHING acknowledged the
// press during the wait, so there was no way to tell the device had registered
// it, and the natural response to that is to hold longer than necessary "to be
// safe". Two changes together: a shorter threshold, and the Pattern lamp
// lighting the instant the key goes down (see refreshLeds) so the wait is
// acknowledged rather than silent.
//
// 150ms is close to the floor. The threshold's whole job is to tell a tap from
// a hold, and a deliberate tap on a mechanical key runs 50-100ms; going much
// below this starts misreading taps as holds, which costs you the mode switch.
const uint16_t PATTERN_HOLD_MS = 150;   // long-press threshold
uint32_t patternPressedAtMillis = 0;
bool patternHoldFromNote = false;       // this hold began in Note mode
bool patternSlotPicked = false;         // a pad was used during this hold

// True whenever the pads should be showing/acting as the slot overlay: always
// while Pattern is held in Pattern mode, and in Note mode once the hold has
// outlasted PATTERN_HOLD_MS. Read by both the pad dispatcher and refreshLeds()
// so the two can never disagree about what the grid currently means.
inline bool patternSlotOverlayActive() {
  if (!patternHeld) return false;
  if (!patternHoldFromNote) return true;
  return (millis() - patternPressedAtMillis) >= PATTERN_HOLD_MS;
}

// v26.1: Bank is now tracked as a held modifier so the Shift+Play+Bank chord
// is order-independent. It previously checked shiftHeld && playHeld at the
// instant Bank was pressed — but Bank sits at matrix row 2 and Play at row 4,
// and the scanner walks rows 0->4, so Bank is evaluated BEFORE Play in every
// pass. Unless Play's press landed in an earlier pass, playHeld was still
// false, the chord was missed, and the press fell through to
// handleBankPress() and silently shifted the bank window instead. Confirmed
// from a Serial log: two "BANK step window now ..." lines immediately before
// the first successful "DIMENSION 2".
bool bankHeld = false;
bool bankConsumed = false; // this Bank press was used by a chord, so its own
                           // action must not fire on release

// Fires the dimension toggle from whichever of Shift / Play / Bank completes
// the chord, rather than only from Bank's press. bankConsumed stops it firing
// twice for one Bank hold, and marks that press as spoken for so Bank's own
// action is suppressed on release. Defined here, below the held flags, rather
// than beside toggleDimension() — it reads shiftHeld and playHeld, which are
// declared further down the file than the dimension state is.
inline void tryDimensionChord() {

}

// v26.5: Pattern+Bank+Clear — wipe the 16-step bank currently on screen, for
// every voice of the active pattern. The missing counterpart to Pattern+Shift+
// Bank (duplicate this bank onto the other one): there was a way to fill the
// second bar and no way to empty it again short of tapping out 16 steps by
// hand or wiping the whole slot and starting over.
//
// Like tryDimensionChord(), this is called from the press edge of all three
// keys rather than just one, so the chord fires whatever order they go down
// in. Both of the other keys' own actions have to be suppressed: bankConsumed
// stops Bank shifting the step window on release, and
// patternEngagedDuringClearHold stops Clear running its own release action.
// Gated on MODE_PATTERN because "the bank on screen" only means anything there.
void clearCurrentBank();

inline void tryBankClearChord() {
  if (patternHeld && bankHeld && clearHeld &&
      gridMode == MODE_PATTERN && !bankConsumed) {
    bankConsumed = true;
    patternEngagedDuringClearHold = true;
    clearCurrentBank();
  }
}
bool noteHeld = false; // true while the Note button is physically held —
                        // drives the "which sounds have content" overview

// v26.8: RECORD ARM. Double-tap Note to arm (the Note LED blinks), tap once to
// disarm; switching to Pattern mode disarms too. While armed, every pad played
// in Note mode is recorded — this replaces holding Shift for each hit, and
// frees Shift+pad for the live note-repeat roll.
//
// Only a CLEAN tap of Note counts: a press with nothing else pressed during
// it, released within NOTE_TAP_MS. Note is also a chord modifier (Note+Shift+
// pad records a roll, Note held shows the content overview), and letting go
// of it after a chord must never arm or disarm by accident.
bool recordArmed = false;
bool noteUsedInCombo = false;   // anything else pressed while Note was held
bool noteFromPattern = false;   // this Note press started in Pattern mode
uint32_t notePressedAtMillis = 0;
uint32_t lastNoteTapMillis = 0; // 0 = no first tap pending
const uint16_t NOTE_TAP_MS = 300;        // longest press that counts as a tap
const uint16_t NOTE_DOUBLE_TAP_MS = 400; // max gap between the two taps
// Pattern-mode step-pad hold state — tracked PER PAD (v19), not as a single
// shared scalar. A single heldPadIndex meant that holding several pads at
// once (e.g. 4 keys for a kick pattern) only let ONE of them ever toggle
// correctly on release — whichever pad's index happened to still be sitting
// in the shared variable at the moment of ITS release. The other held pads'
// releases compared against the wrong value and silently did nothing.
bool padHeld[16] = {false};
uint16_t padKnobBaseline[16] = {0}; // knob position captured when each pad was pressed
bool padKnobMoved[16] = {false};    // per-pad: has the knob moved enough since THIS pad was pressed
int8_t heldNoteVoice = -1; // voice currently held in Note mode (velocity edits)
uint8_t voiceVelocity[NUM_VOICES]; // per-voice audition/live velocity, session-only (not saved to flash)

// ---------------------------------------------------------------------------
// NOTE REPEAT (v26.5) — Shift+pad rolls that voice in 1/16ths (v26.8; it was
// Shift+Note+pad). Recorded into the pattern when record is armed, or always
// with Note held too (Note+Shift+pad).
// ---------------------------------------------------------------------------
// Indexed by PAD, holding the voice it is rolling, rather than indexed by
// voice. Bank can be pressed mid-roll, which moves the pad-to-voice mapping
// under our feet; keyed by pad, the release always finds the entry it made
// and stops the right note. Keyed by voice it would not, and the roll would
// run forever.
//
// Several pads can roll at once — hold a kick and a hat and you get both.
//
// Timing comes from the sequencer when there is a sequencer to follow, so the
// roll is locked to the same grid as everything else and inherits nothing from
// millis() drift. advancePlayhead() calls fireNoteRepeats() once per 16th.
// With the transport stopped there is no clock at all (this device has no
// internal one — isPlayingNow() requires the DAW), so serviceNoteRepeat()
// free-runs instead, at the last measured step duration if we have ever seen
// one and REPEAT_FALLBACK_MS if we have not.
int8_t repeatVoiceForPad[16];              // -1 = this pad is not rolling
uint8_t repeatStreamForPad[16] = {0};      // stream each roll is sounding on
bool repeatRecordForPad[16] = {false};     // v26.8: this roll writes its hits
uint32_t repeatNextAtMillis = 0;           // free-running path only
const uint16_t REPEAT_FALLBACK_MS = 125;   // one 16th at 120 BPM

void handleNoteRepeatPad(uint8_t padIndex, bool pressed);
void fireNoteRepeats(int16_t globalStep);
void serviceNoteRepeat();

inline bool anyNoteRepeatActive() {
  for (uint8_t p = 0; p < 16; p++) if (repeatVoiceForPad[p] >= 0) return true;
  return false;
}
uint16_t knobBaselineRaw = 0;   // Note mode only (v19): knob position captured when
                                  // a note pad was pressed (Pattern mode now uses the
                                  // per-pad padKnobBaseline[] array above instead,
                                  // since Note mode still only ever holds one voice
                                  // at a time and didn't need the same fix)
bool knobMovedSinceHold = false; // Note mode only (v19) — see above

// v26.2: the same deadzone treatment for the two MODIFIER-held knob controls,
// gate length (Mute+knob) and shuffle (Shift+knob). Without it, merely
// pressing the modifier snapped the value to wherever the knob physically
// happened to be sitting — left over from editing a velocity a moment
// earlier. v19 fixed exactly this for the pad-held velocity editors; these
// two were never given the same guard. Reported as "sounds get shorter just
// from pressing Mute", which is this bug with the knob parked low.
uint16_t muteKnobBaseline = 0;   // 0.3.4: Mute + knob = gate length again
bool muteKnobMoved = false;
uint16_t shiftKnobBaseline = 0;
bool shiftKnobMoved = false;
const uint16_t KNOB_MOVE_DEADZONE = 12; // ADC counts of wiggle room before counting as "moved"

// Key debounce state
bool keyPressed[MAX_KEY_INDEX] = {false};
uint32_t keyLastChange[MAX_KEY_INDEX] = {0};

// v26.4: keys that already read as pressed at power-up are ignored until they
// are seen released. A switch held down by a fault — a stuck key, a shorted
// column line, enclosure pressure — otherwise hijacks the instrument: a stuck
// Mute turns every pad press into a mute toggle and nothing else works.
// Masking it costs that one key and leaves the rest of the board usable.
bool keyMasked[MAX_KEY_INDEX] = {false};
const uint16_t DEBOUNCE_MS = 8;

const uint8_t LED_I2C_ADDR = 0x74; // ledDriver.begin()'s default address
bool ledDriverOk = false; // v23: false = driver not found at boot, skip all LED writes

// v26: per stream. Only the current stream's flashes are ever displayed, but
// all three streams fire, so each needs its own set.
uint32_t stepFlashUntil[NUM_STREAMS][MAX_STEPS] = {{0}};
uint32_t voiceFlashUntil[NUM_STREAMS][NUM_VOICES] = {{0}};
const uint16_t FLASH_MS = 40;

// Loop length is a property of each stream's active pattern. A 1-bar stream
// running against a 2-bar stream simply repeats — see advancePlayhead().
bool loopIsTwoBar[NUM_STREAMS] = {false};

// Length of the global loop: the longest active pattern among streams that
// have content. Every stream indexes into it modulo its own length, which is
// what makes a short pattern repeat against a longer one.
inline uint8_t streamLoopLen(uint8_t s) { return loopIsTwoBar[s] ? 32 : 16; }

// The global loop is as long as the longest pattern that actually has
// something in it. Empty streams are ignored, so a single 1-bar drum pattern
// still loops every bar rather than being stretched to two by an empty
// 2-bar slot sitting in another stream.
// ---------------------------------------------------------------------------
// v26.10: PATTERN CHAINING — per stream, not saved.
// Double-tap Pattern: chain mode (the Pattern LED blinks). Each pad tapped adds
// that slot to the chain, in order, as often as you like (pad 1, 1, 2, 3, 3, 5
// is a chain of six). Tap Pattern once, or press Note, to leave chain mode: the
// chain starts when the current pattern ends, from its first entry, and loops.
// Every entry plays its whole pattern — two bars when bank 2 has notes.
// Stopping: pick a pattern the usual way (hold Pattern + pad), or enter chain
// mode and leave it without tapping a slot. A new chain replaces the old one
// when the entry playing ends.
//
// A chained stream counts its own steps (chainStep) instead of reading the
// shared playhead modulo its length, so each entry starts on its first bar
// and moves on at the end of its OWN length, not the longest loop across the
// streams (a one-bar entry would otherwise play twice against a two-bar
// stream). Every entry is 16 or 32 steps, so it stays on the bar.
// ---------------------------------------------------------------------------
const uint8_t CHAIN_MAX = 32;
uint8_t chainSlots[NUM_STREAMS][CHAIN_MAX];  // the chain playing (global slots)
uint8_t chainLen[NUM_STREAMS]      = {0};
bool    chainOn[NUM_STREAMS]       = {false};
uint8_t chainPos[NUM_STREAMS]      = {0};   // entry playing
uint8_t chainStep[NUM_STREAMS]     = {0};   // next step within it
bool    chainWrap[NUM_STREAMS]     = {false};  // entry ended: next at the next step
uint8_t chainNext[NUM_STREAMS][CHAIN_MAX];   // a new chain, waiting to start
uint8_t chainNextLen[NUM_STREAMS]  = {0};
bool    chainStartPending[NUM_STREAMS] = {false};
bool    chainStopPending[NUM_STREAMS]  = {false};  // at the next global boundary
bool    chainEditing = false;                // chain mode
uint8_t chainEditStream = 0;
uint8_t chainEditList[CHAIN_MAX];
uint8_t chainEditLen = 0;
int8_t  chainFlashPad = -1;                  // the pad just added, flashed
uint32_t chainFlashUntil = 0;
uint32_t lastPatternTapMillis = 0;           // 0 = no first tap pending
bool    patternUsedInCombo = false;          // anything else used while Pattern was held
const uint16_t PATTERN_TAP_MS = 300;         // longest press that counts as a tap (double-tap)
const uint16_t PATTERN_DOUBLE_TAP_MS = 400;  // max gap between the two taps

uint8_t streamLocalNow[NUM_STREAMS] = {0};   // real step each stream is on

inline uint8_t globalLoopLen() {
  uint8_t len = 16;
  for (uint8_t s = 0; s < NUM_STREAMS; s++) {
    if (!state.slots[activeSlot[s]].hasContent) continue;
    if (streamLoopLen(s) > len) len = streamLoopLen(s);
  }
  return len;
}

// Autosave
bool dirty = false;
uint32_t lastEditMillis = 0;
const uint32_t AUTOSAVE_DEBOUNCE_MS = 1500;

// ---------------------------------------------------------------------------
// BOOTLOADER KEY COMBO (v24) — Shift + Clear held at power-up
// ---------------------------------------------------------------------------
const uint16_t BOOT_COMBO_HOLD_MS = 500;   // both keys must stay down this long
const uint16_t BOOT_COMBO_SAMPLE_MS = 10;  // re-check interval while holding

// Reads one key directly from the matrix, without debounce or any side
// effects. Used only at boot, before the normal scanner has run.
bool rawKeyDown(int8_t key) {
  return false;
}

// Must be called after the row/column pins are configured.
void checkBootloaderCombo() {

}

// No manager identity tag: standalone RAM-only test image.

// ---------------------------------------------------------------------------
// SETUP
// ---------------------------------------------------------------------------
void setup() {
  for (uint8_t v=0;v<NUM_VOICES;v++) voiceVelocity[v]=DEFAULT_VELOCITY;
  for (uint8_t p=0;p<16;p++) repeatVoiceForPad[p]=-1;
  initDefaultState();
  activeSlot[0]=0; currentStream=0; dimension=0; noteBankWindow[0]=0;
  recalcLoopLengthFor(0);
}

// ---------------------------------------------------------------------------
// MAIN LOOP
// ---------------------------------------------------------------------------
uint32_t lastLedRefreshMillis = 0;
const uint16_t LED_REFRESH_INTERVAL_MS = 30; // ~33Hz — plenty smooth for LEDs,
                                              // a small fraction of the I2C
                                              // traffic of doing it every loop

void loop() {
  serviceMidiLead();
  handlePendingSwingTrigger();
  serviceNoteRepeat();
  servicePendingNoteOffs();
  if (merged_bro_visible()) {
    handleKnob();
    if (millis()-lastLedRefreshMillis>=LED_REFRESH_INTERVAL_MS) {
      lastLedRefreshMillis=millis(); refreshLeds();
    }
  }
}

// ---------------------------------------------------------------------------
// MATRIX SCANNING
// ---------------------------------------------------------------------------
void scanMatrixAndDispatch() {

}

// v26.4: name a key on serial, so the boot report is readable without the
// source to hand.
void printKeyName(int8_t key) {
  switch (key) {
    case K_NOTE:    Serial.print("Note");    return;
    case K_PATTERN: Serial.print("Pattern"); return;
    case K_SHIFT:   Serial.print("Shift");   return;
    case K_MUTE:    Serial.print("Mute");    return;
    case K_BANK:    Serial.print("Bank");    return;
    case K_CLEAR:   Serial.print("Clear");   return;
    case K_PLAY:    Serial.print("Play");    return;
    default: break;
  }
  if (key >= K_PAD_BASE && key < K_PAD_BASE + 16) {
    Serial.print("pad ");
    Serial.print(key - K_PAD_BASE + 1);
  } else {
    Serial.print("key ");
    Serial.print(key);
  }
}

// v26.4: scan once at startup and mask anything already down. Runs after the
// Shift+Clear bootloader check, so that gesture is unaffected.
void maskStuckKeysAtBoot() {

}

// ---------------------------------------------------------------------------
// KEY EVENT DISPATCH
// ---------------------------------------------------------------------------
void onKeyEvent(int8_t key, bool pressed) {
  // v26.8: any other key going down while Note is held makes this Note press
  // a chord, not a tap — see recordArmed. And any other key between two Note
  // taps means they were not a double-tap.
  if (pressed && key != K_NOTE) {
    if (noteHeld) noteUsedInCombo = true;
    lastNoteTapMillis = 0;
  }
  // v26.10: the same for Pattern (double-tap = chain mode).
  if (pressed && key != K_PATTERN) {
    if (patternHeld) patternUsedInCombo = true;
    lastPatternTapMillis = 0;
  }

  if (key == K_SHIFT) {
    shiftHeld = pressed;
    if (pressed) {
      shiftKnobBaseline = readKnob(); // v26.2: see muteKnobBaseline
      shiftKnobMoved = false;
      tryDimensionChord();                      // v26.1: order-independent chord
    }
    return;
  }

  if (key == K_MUTE) {
    muteHeld = pressed;
    if (pressed) {
      muteKnobBaseline = readKnob(); // v26.2: don't snap gate length
      muteKnobMoved = false;
      if (clearHeld) unmuteAll(); // Clear+Mute — the only unmute-all combo
    }
    return;
  }

  if (key == K_CLEAR) {
    if (pressed) {
      clearHeld = true;
      // Covers "Pattern already held, then Clear pressed" — the other
      // order (Clear first, then Pattern) is covered in the K_PATTERN
      // handler below.
      if (patternHeld) patternEngagedDuringClearHold = true;
      tryBankClearChord(); // v26.5: Pattern+Bank+Clear, Clear completing it
      // Action deferred to release (below) so that a Clear+Pattern combo
      // can be told apart from a plain Clear tap before anything fires —
      // previously this fired immediately on press, which meant "hold
      // Clear then Pattern" already wiped the current voice as a side
      // effect before Pattern even got involved.
      return;
    } else {
      clearHeld = false;
      if (!patternEngagedDuringClearHold) {
        if (shiftHeld) clearAllVoices();
        else if (muteHeld) unmuteAll();
        // v22: a plain Clear tap with no modifier and no pad pressed
        // during the hold no longer clears lastSelectedVoice[currentStream] — that was
        // too easy to trigger by accident (just select a sound, then tap
        // Clear). Clearing a single voice's pattern now requires the
        // explicit Clear+pad gesture instead — see handleNotePad()'s
        // clearHeld branch, handled on the pad's own press edge.
      }
      patternEngagedDuringClearHold = false;
      return;
    }
  }

  if (key == K_NOTE) {
    noteHeld = pressed;
    if (!pressed) {
      uint32_t now = millis();
      if (noteUsedInCombo) {
        // v26.8: Note used as a modifier from Pattern mode (Note+Shift+pad to
        // record a roll, Note+pad to pick a sound) hands you back to Pattern
        // mode, so you stay where you were editing.
        if (noteFromPattern) gridMode = MODE_PATTERN;
      } else if (now - notePressedAtMillis < NOTE_TAP_MS) {
        if (recordArmed) {
          recordArmed = false;
          lastNoteTapMillis = 0; // a disarming tap never starts a double-tap
          Serial.println("RECORD disarmed");
        } else if (lastNoteTapMillis != 0 &&
                   now - lastNoteTapMillis < NOTE_DOUBLE_TAP_MS) {
          recordArmed = true;
          lastNoteTapMillis = 0;
          Serial.println("RECORD armed");
        } else {
          lastNoteTapMillis = now;
        }
      } else {
        lastNoteTapMillis = 0; // a long hold (overview) breaks a double-tap
      }
      noteFromPattern = false;
      noteUsedInCombo = false;
      return;
    }
    if (pressed) {
      if (chainEditing) finishChainEdit();   // v26.10: a mode change leaves chain mode
      noteUsedInCombo = false;
      notePressedAtMillis = millis();
      noteFromPattern = (gridMode == MODE_PATTERN);
      // v22: no longer force-saves here. This used to call forceSave()
      // unconditionally, which is exactly as blocking as every other
      // direct forceSave() call — switching back to Note mode to pick a
      // different sound while playing hit the same silent ~1s stutter as
      // pattern edits did before v21. Saving is now handled entirely by
      // the debounced autosave (only when !isPlayingNow()) and the
      // explicit flush on transport stop — see maybeAutosave(), onMidiStop(),
      // and handlePlayStopPress().
      gridMode = MODE_NOTE;
    }
    return;
  }

  if (key == K_PATTERN) {
    if (pressed) {
      patternHeld = true;
      patternPressedAtMillis = millis();
      patternSlotPicked = false;
      patternUsedInCombo = false;           // v26.10
      // v26.5: in note mode the mode switch is no longer done here. It waits
      // for the release, which is the only point at which a tap and a hold can
      // be told apart. See PATTERN_HOLD_MS.
      patternHoldFromNote = (gridMode == MODE_NOTE);
      // Covers "Clear already held, then Pattern pressed" — see the
      // K_CLEAR handler above for the other order.
      if (clearHeld) patternEngagedDuringClearHold = true;
      tryBankClearChord(); // v26.5: Pattern+Bank+Clear, Pattern completing it
    } else {
      uint32_t heldFor = millis() - patternPressedAtMillis;
      bool wasQuickTap = heldFor < PATTERN_HOLD_MS;
      // v26.10: a clean tap — short, nothing else used — counts towards the
      // double-tap into chain mode; in chain mode it leaves it.
      bool cleanTap = heldFor < PATTERN_TAP_MS && !patternSlotPicked &&
                      !patternUsedInCombo;
      bool wasEditing = chainEditing;
      if (chainEditing) {
        if (cleanTap) finishChainEdit();
        lastPatternTapMillis = 0;
      } else if (cleanTap) {
        if (lastPatternTapMillis != 0 && millis() - lastPatternTapMillis < PATTERN_DOUBLE_TAP_MS) {
          enterChainEdit();
          lastPatternTapMillis = 0;
        } else {
          lastPatternTapMillis = millis();
        }
      } else {
        lastPatternTapMillis = 0;
      }
      if (!wasEditing && !chainEditing && patternHoldFromNote && wasQuickTap && !patternSlotPicked) {
        // A quick tap from note mode means the same thing it always did:
        // show me this voice's steps.
        gridMode = MODE_PATTERN;
        // v26.8: leaving Note mode disarms recording, so coming back to it
        // later can never record by surprise.
        if (recordArmed) Serial.println("RECORD disarmed (Pattern mode)");
        recordArmed = false;
        lastNoteTapMillis = 0;
      }
      // Every other case leaves gridMode alone — a long hold from note mode
      // was a trip to the slot overlay and belongs back in note mode, and a
      // hold that started in Pattern mode never changed mode to begin with.
      patternHeld = false;
      patternHoldFromNote = false;
      patternSlotPicked = false;
      armedCopySource = -1; // releasing Pattern cancels any pending copy
    }
    return;
  }

  if (key == K_PLAY) {
    playHeld = pressed;
    if (pressed) {
      playKnobBaseline = readKnob(); // v26.9: Play+knob = MIDI lead (stopped)
      playKnobMoved = false;
    } else if (midiLeadDirty) {
      saveMidiLead();                // stopped by definition: safe to write
    }
    if (pressed && !shiftHeld) handlePlayStopPress();
    if (pressed) tryDimensionChord(); // v26.1: order-independent chord
    return;
  }

  if (key == K_BANK) {
    if (pressed) {
      bankHeld = true;
      bankConsumed = false;

      // v22: Pattern+Shift+Bank — copy the currently-viewed 16-step bank
      // (for every voice) onto the other bank, e.g. programming only steps
      // 1-16 and duplicating them into 17-32 to seed a two-bar loop before
      // tweaking the second half. patternHeld only becomes true while
      // already in Pattern mode (see the K_PATTERN handler), so this can't
      // fire from Note mode by accident. Unambiguous on the press edge,
      // because both modifiers are already held by definition.
      if (patternHeld && shiftHeld && gridMode == MODE_PATTERN) {
        bankConsumed = true;
        copyCurrentBankToOtherBank();
        return;
      }

      // v26.5: Pattern+Bank+Clear, with Bank completing the chord. Same
      // press-edge reasoning as the copy above — both modifiers are already
      // held by definition, so there is nothing ambiguous to defer.
      tryBankClearChord();
      if (bankConsumed) return;

      // Fires only if Shift+Play are ALREADY held. If Bank goes down first,
      // the chord completes later from the Shift or Play handler instead.
      tryDimensionChord();

      // v26.1: Bank's own action is deliberately NOT performed here. It is
      // deferred to release so a press that turns out to be part of a chord
      // never shifts the bank window on its way through — the same reason
      // K_CLEAR defers its action, and the fix for the missed-chord bug.
      return;
    }

    bankHeld = false;
    if (!bankConsumed) handleBankPress(); // a plain tap, used by nothing else
    bankConsumed = false;
    return;
  }

  // Everything below here is a pad (K_PAD_BASE..K_PAD_BASE+15)
  if (key < K_PAD_BASE || key > K_PAD_BASE + 15) return;
  uint8_t padIndex = key - K_PAD_BASE;

  // Highest-priority overlay: Shift+Play held = MIDI channel select,
  // regardless of grid mode.
  if (shiftHeld && playHeld) {
    if (pressed) handleShiftPlayOverlayPad(padIndex);
    return;
  }

  // Next: the slot recall/copy overlay. v26.5: reachable from note mode too,
  // once Pattern has been held past PATTERN_HOLD_MS — see
  // patternSlotOverlayActive().
  if (patternSlotOverlayActive()) {
    if (pressed) {
      patternSlotPicked = true; // so the release does not also switch mode
      handlePatternHeldPad(padIndex);
    }
    return;
  }

  // Note repeat, 1/16ths for as long as the pad is held. v26.8: Shift+pad
  // rolls (it used to be Shift+Note+pad, with Shift+pad recording a single
  // hit — record arm replaced that). The roll is recorded when armed, or
  // when Note is held as well: Note+Shift+pad always records.
  //
  // Shift+Mute+pad (stream mutes) and Shift+Play+pad (channels) keep their
  // meanings; the latter is already handled above.
  //
  // "Is this pad rolling" is checked first, and in either grid mode: it is a
  // stuck-note guard, not a nicety. The chord is tested on the pad's PRESS,
  // so releasing Shift while still holding the pad — or releasing Note, which
  // now drops back to Pattern mode — would otherwise send the release down
  // another path and leave the roll running with nothing left to stop it.
  if (repeatVoiceForPad[padIndex] >= 0 ||
      (gridMode == MODE_NOTE && pressed && shiftHeld && !muteHeld && !clearHeld)) {
    handleNoteRepeatPad(padIndex, pressed);
    return;
  }

  // v26: Shift+Mute held = WHOLE-STREAM mutes, on the first three pads.
  // Shift and Mute are adjacent in matrix row 0, so this is a one-hand grip
  // that leaves the other hand free for the pads — it is a performance
  // gesture, not a settings screen. "Mute = voice, Shift+Mute = the level
  // above" is the mapping to remember.
  if (shiftHeld && muteHeld) {
    if (pressed && padIndex < NUM_STREAMS) {
      streamMuted[padIndex] = !streamMuted[padIndex];
      // Resolve anything that stream already has sounding, or its notes hang
      // until something else happens to close them.
      if (streamMuted[padIndex]) flushStreamNotes(padIndex);
      Serial.print("STREAM "); Serial.print((char)('A' + padIndex));
      Serial.println(streamMuted[padIndex] ? " MUTED" : " unmuted");
    }
    // 0.3.2: pads 2 and 3 mute the Yummy level: its MIDI notes, its audio.
    if (pressed && padIndex == 1) ui_yummy_midi_mute_toggle();
    if (pressed && padIndex == 2) ui_yummy_audio_mute_toggle();
    return;
  }

  // v25.1: Mute+pad works in BOTH modes. It used to live inside
  // handleNotePad(), so muting was unreachable from Pattern mode. The
  // pad-to-voice mapping follows noteBankWindow, matching the Mute-held LED
  // overlay in refreshLeds(). Clear still wins if both are held, preserving
  // the precedence handleNotePad() had. v26: mutes only the current stream.
  if (muteHeld && !clearHeld) {
    if (pressed) {
      uint8_t voice = noteBankWindow[currentStream] * 16 + padIndex;
      uint8_t slot = activeSlot[currentStream]; // v26.5: mutes are per pattern
      mutedVoice[slot][voice] = !mutedVoice[slot][voice];
      // v26.2: muting also SELECTS the voice, so switching to Pattern mode
      // shows the pattern you just silenced. The workflow this enables is
      // mute it, look at it, edit it, unmute it. Before this, you were still
      // looking at whichever voice you last edited. Muting several in a row
      // leaves the last one selected, which is the useful end of that trade.
      lastSelectedVoice[currentStream] = voice;
      // If this voice is mid-gate, close it now rather than leaving the
      // pending Note Off to fire into a voice the user just silenced.
      if (mutedVoice[slot][voice] && voiceNoteOffAt[currentStream][voice] != 0) {
        sendVoiceNoteOff(currentStream, voice);
      }
      Serial.print("MUTE toggle voice "); Serial.print(voice);
      Serial.print(" in slot "); Serial.print(slot);
      Serial.print(" now "); Serial.println(mutedVoice[slot][voice] ? "MUTED" : "unmuted");
    }
    return;
  }

  if (chainEditing) {                       // v26.10: chain mode: pads add slots
    if (pressed) chainEditPad(padIndex);
    return;
  }

  if (gridMode == MODE_NOTE) {
    handleNotePad(padIndex, pressed);
  } else {
    handlePatternPad(padIndex, pressed);
  }
}

// ---------------------------------------------------------------------------
// BANK — different meaning per mode, per Section 3.2/3.3
// ---------------------------------------------------------------------------
void handleBankPress() {
  if (gridMode == MODE_NOTE) {
    noteBankWindow[currentStream] = noteBankWindow[currentStream] ? 0 : 1;
    state.slots[activeSlot[currentStream]].noteBankWindow = noteBankWindow[currentStream];
    Serial.print("BANK (Note mode) voice window now "); Serial.println(noteBankWindow[currentStream]);
    markDirty();
  } else {
    currentStepBank[currentStream] = currentStepBank[currentStream] ? 0 : 1; // not persisted — resets on slot load
    Serial.print("BANK (Pattern mode) step window now "); Serial.println(currentStepBank[currentStream]);
  }
}

// v22: Pattern+Shift+Bank — duplicate the currently-viewed 16-step bank
// onto the other one, for every voice in the active slot. E.g. with only
// steps 1-16 programmed, this fills 17-32 with the same content, turning a
// one-bar pattern into a two-bar loop ready to be varied. Overwrites
// whatever was in the destination bank.
void copyCurrentBankToOtherBank() {
  uint8_t srcBase = currentStepBank[currentStream] * 16;
  uint8_t dstBase = srcBase ? 0 : 16;
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    for (uint8_t i = 0; i < 16; i++) {
      state.slots[activeSlot[currentStream]].steps[v][dstBase + i] = state.slots[activeSlot[currentStream]].steps[v][srcBase + i];
    }
  }
  recalcHasContent(activeSlot[currentStream]);
  markDirty();
  recalcLoopLength();
  Serial.print("COPY BANK "); Serial.print(currentStepBank[currentStream]);
  Serial.print(" -> "); Serial.println(currentStepBank[currentStream] ? 0 : 1);
}

// v26.5: Pattern+Bank+Clear — empty the 16-step bank currently being viewed,
// across every voice, leaving the other bank untouched. Fired from
// tryBankClearChord(); see the comment there for how the chord is detected.
//
// recalcLoopLength() matters here: clearing bank 1 is how a two-bar loop
// becomes a one-bar loop again, and nothing else would notice.
void clearCurrentBank() {
  uint8_t slot = activeSlot[currentStream];
  uint8_t base = currentStepBank[currentStream] * 16;
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    for (uint8_t i = 0; i < 16; i++) state.slots[slot].steps[v][base + i] = 0;
  }
  recalcHasContent(slot);
  markDirty();
  recalcLoopLength();
  Serial.print("CLEAR BANK "); Serial.print(currentStepBank[currentStream]);
  Serial.print(" of slot "); Serial.println(slot);
}

// ---------------------------------------------------------------------------
// NOTE MODE
// ---------------------------------------------------------------------------
// Writes one live-played hit into the pattern being edited, on the step it was
// played nearest to. Used by record arm (handleNotePad) and for the first hit
// of a recorded roll (handleNoteRepeatPad). Only meaningful while playing:
// there is no step position to write to otherwise.
void recordLiveHit(uint8_t voice) {
  // Round to the NEAREST step boundary, not always the one that
  // already fired. lastTriggeredStep alone is a strictly backward-
  // looking reference — it only ever tells you what already happened,
  // so a press timed slightly AHEAD of the beat (anticipating it,
  // which is a completely normal and common way to play in time)
  // always got recorded onto the previous step instead of the
  // intended upcoming one — e.g. a clap aimed at beat 2 landing one
  // step early. Using the time elapsed since lastTriggeredStep fired,
  // against the real measured step duration (stepDurationEstimateMs,
  // fixed in v16 to track actual step-to-step timing), tells us which
  // boundary the press is actually closer to.
  uint8_t loopLen = loopIsTwoBar[currentStream] ? 32 : 16;
  uint8_t recordStep = lastTriggeredStep;
  if (stepDurationEstimateMs > 0) {
    // v26.9: signed. lastTriggeredStepMillis is when the step is HEARD in the
    // DAW, which is up to the MIDI lead after it was sent, so a press right on
    // the beat can come before it — and belongs to that step.
    int32_t elapsedSinceTrigger = (int32_t)(millis() - lastTriggeredStepMillis);
    if (elapsedSinceTrigger > (int32_t)(stepDurationEstimateMs / 2)) {
      recordStep = (lastTriggeredStep + 1) % loopLen;
    }
  }
  state.slots[activeSlot[currentStream]].steps[voice][recordStep] = voiceVelocity[voice];
  suppressNextPlayback(voice, recordStep);
  Serial.print("LIVE RECORD voice "); Serial.print(voice);
  Serial.print(" step "); Serial.print(recordStep);
  Serial.print(" note "); Serial.println(midiNoteForVoice(voice));
  state.slots[activeSlot[currentStream]].hasContent = true;
  markDirty();
  recalcLoopLength();
}

void handleNotePad(uint8_t padIndex, bool pressed) {
  uint8_t voice = noteBankWindow[currentStream] * 16 + padIndex;

  if (clearHeld) {
    // v22: explicit Clear+pad gesture — the only way to clear a single
    // voice's pattern now. Clears whichever voice's pad is pressed while
    // Clear is held, regardless of which voice was last selected, and does
    // NOT select/audition it — a clean, deliberate clear action, not a
    // side effect of picking a sound. Handled entirely on the press edge,
    // like Mute+pad; the matching release does nothing. Holding Clear and
    // tapping several pads in a row clears each of them.
    if (pressed) {
      clearVoice(voice);
      Serial.print("CLEAR voice "); Serial.println(voice);
    }
    return;
  }

  // Mute+pad moved to the pad dispatcher in v25.1 so it works in Pattern
  // mode too — see the muteHeld branch there. It is still a pure toggle:
  // no audition trigger, no change to which voice is selected for editing,
  // handled entirely on the press edge.

  if (pressed) {
    lastSelectedVoice[currentStream] = voice;
    heldNoteVoice = voice; // enables hold+turn velocity editing in handleKnob()
    knobBaselineRaw = readKnob();
    knobMovedSinceHold = false;
    Serial.print("NOTE MODE selected voice "); Serial.println(voice);
    playVoiceNote(voice, voiceVelocity[voice]);
    voiceHeldLive[currentStream][voice] = true;

    // v26.8: recorded when record is armed (double-tap Note), instead of
    // while Shift is held — Shift+pad is the live roll now.
    if (recordArmed && isPlayingNow()) recordLiveHit(voice);
  } else {
    // No Note Off needed here anymore — playVoiceNote() already sends a
    // complete Note On + Note Off pair immediately on press. Sending a
    // second Note Off here was exactly what produced an audible sound on
    // release.
    voiceHeldLive[currentStream][voice] = false;
    if (heldNoteVoice == voice) heldNoteVoice = -1;
  }
}

// ---------------------------------------------------------------------------
// PATTERN MODE — normal step editing (Pattern NOT held)
// ---------------------------------------------------------------------------
void handlePatternPad(uint8_t padIndex, bool pressed) {
  if (pressed) {
    padHeld[padIndex] = true;
    padKnobBaseline[padIndex] = readKnob();
    padKnobMoved[padIndex] = false;
    // Toggle decision is deferred to release (see below) — pressing a step
    // doesn't erase it immediately, so holding it down and turning the
    // knob to adjust velocity doesn't accidentally switch it off first.
    // Each pad tracks its own hold/knob state now, so this works correctly
    // no matter how many other pads are already held at the same time.
  } else {
    if (padHeld[padIndex]) {
      if (!padKnobMoved[padIndex]) {
        // A genuine tap (no knob movement during this pad's hold): toggle
        // on/off, same as before.
        uint8_t globalStep = currentStepBank[currentStream] * 16 + padIndex;
        uint8_t &v = state.slots[activeSlot[currentStream]].steps[lastSelectedVoice[currentStream]][globalStep];
        v = v ? 0 : DEFAULT_VELOCITY; // toggle off, or on at default velocity
        Serial.print("PATTERN EDIT voice "); Serial.print(lastSelectedVoice[currentStream]);
        Serial.print(" step "); Serial.print(globalStep);
        Serial.print(" now "); Serial.println(v);
        if (v > 0) state.slots[activeSlot[currentStream]].hasContent = true;
        markDirty();
        recalcLoopLength();
      }
      // If the knob did move during this pad's hold, the velocity was
      // already adjusted live in handleKnob() — releasing just ends the
      // hold, no toggle.
      padHeld[padIndex] = false;
      padKnobMoved[padIndex] = false;
    }
  }
}

// ---------------------------------------------------------------------------
// PATTERN MEMORY — recall / copy, Section 3.6
// Reached only while Pattern is held and gridMode == MODE_PATTERN.
// ---------------------------------------------------------------------------
void handlePatternHeldPad(uint8_t padIndex) {
  // v26: the pad no longer maps straight onto a slot number — in dimension 1
  // the grid is split between streams B and C. See slotForPad().
  uint8_t targetSlot = slotForPad(padIndex);
  uint8_t targetStream = streamOfSlot(targetSlot);

  if (clearHeld) {
    // Clear+Pattern+tap: wipe that entire slot (all 32 voices, all 32
    // steps, shuffle back to default) — a deliberate, explicit action,
    // separate from plain Clear (current voice only) and Shift+Clear
    // (all voices, but only in the currently active slot).
    clearSlot(targetSlot);
    return;
  }

  if (shiftHeld) {
    // Copy gesture: first tap = source, second tap = destination.
    if (armedCopySource < 0) {
      armedCopySource = targetSlot;
    } else {
      copySlot((uint8_t)armedCopySource, targetSlot);
      armedCopySource = -1;
      // v26.5: the DESTINATION becomes the active pattern. Previously the
      // source kept playing and stayed active, so the obvious next move —
      // start adding notes to the copy you just made — silently edited the
      // original instead. Copying is almost always "give me a variation to
      // work on", so landing on the copy is the behaviour that matches the
      // intent; going back to the source is one ordinary recall tap away.
      //
      // Quantised like every other recall (requestSlotSwitch), so it lands on
      // the bar rather than jumping mid-pattern. The destination is a byte-
      // for-byte duplicate of the source at this instant, so the switch is
      // inaudible anyway — the overlay shows the queued pad in the meantime.
      currentStream = targetStream;
      if (dimension == 1) lastStreamInDim2 = targetStream;
      chainEditing = false;                 // v26.10: as a recall
      chainCancel(targetStream);
      requestSlotSwitch(targetStream, targetSlot);
      Serial.print("COPY -> slot "); Serial.print(targetSlot);
      Serial.println(isPlayingNow() ? " queued as active at the next bar"
                                    : " now active");
    }
    return;
  }

  // Recall gesture. v26: picking a slot also decides which stream the grid
  // now edits — in dimension 1 that is how you move between B and C. Set
  // before the switch request, because requestSlotSwitch() returns early if
  // the slot is already active and we still want the editing focus to move.
  currentStream = targetStream;
  if (dimension == 1) lastStreamInDim2 = targetStream;
  // v26.10: picking a pattern ends a chain on that stream (and chain mode).
  chainEditing = false;
  chainCancel(targetStream);
  requestSlotSwitch(targetStream, targetSlot);
}

// v26: both take an explicit stream. advancePlayhead() resolves queued
// switches for every stream at the same global boundary, so this can no
// longer assume it is acting on whichever stream happens to be on screen.
void requestSlotSwitch(uint8_t s, uint8_t targetSlot) {
  if (targetSlot == activeSlot[s]) return;

  if (dawPlaying) {   // 0.3.3: also locally stopped: the timeline still runs
    pendingSlotSwitch[s] = targetSlot; // applied at the next global boundary
  } else {
    applySlotSwitch(s, targetSlot); // no boundary coming — apply now
  }
}

void applySlotSwitch(uint8_t s, uint8_t targetSlot) {
  // v20: forceSave() here is only safe to actually run when nothing is
  // playing — see the long note in maybeAutosave(). A quantized slot
  // recall lands exactly at a bar boundary via advancePlayhead(), while
  // playing, which is the worst possible place for a ~1s blocking flash
  // write. markDirty() still records that a save is owed; the deferred
  // paths in onMidiStop()/handlePlayStopPress() flush it once it's safe.
  // requestSlotSwitch() only reaches this function immediately when
  // !isPlayingNow() anyway (otherwise it defers via pendingSlotSwitch[currentStream]), so
  // in practice this check is almost always already true here — it's kept
  // explicit rather than assumed, in case that ever changes.
  if (!isPlayingNow()) {
    forceSave(); // flush the outgoing slot before we move on
  } else {
    markDirty();
  }

  activeSlot[s] = targetSlot;
  noteBankWindow[s] = state.slots[activeSlot[s]].noteBankWindow;
  currentStepBank[s] = 0; // viewing window resets; step data is untouched
  pendingSlotSwitch[s] = -1;

  // v26: the playhead is global and shared, so a slot switch on ONE stream
  // must not reset it — that would yank the other two back to step 0 and
  // break the sync that makes this a multi-channel sequencer. Switches are
  // quantised to the global boundary anyway, so the playhead is already at 0
  // whenever this runs during playback.
  if (!dawPlaying) playhead = 0;   // 0.3.3: never while the timeline runs

  state.lastActiveSlot[s] = activeSlot[s];
  if (!isPlayingNow()) {
    forceSave(); // persist the new lastActiveSlot immediately
  } else {
    markDirty();
  }
  recalcLoopLengthFor(s);
}

void copySlot(uint8_t src, uint8_t dst) {
  if (src == dst) return;
  state.slots[dst] = state.slots[src];
  // v26.5: carry the mutes across too. Now that mutes are per slot, a copy
  // that left them behind would sound different from its source the instant
  // it went active — copy a pattern with the hats muted and the copy would
  // come up with hats blaring. "Copy" should mean what you hear, not just
  // what is programmed.
  for (uint8_t v = 0; v < NUM_VOICES; v++) mutedVoice[dst][v] = mutedVoice[src][v];
  // v26: the destination may belong to a stream other than the visible one.
  uint8_t ds = streamOfSlot(dst);
  if (dst == activeSlot[ds]) {
    noteBankWindow[ds] = state.slots[activeSlot[ds]].noteBankWindow;
    recalcLoopLengthFor(ds);
  }
  markDirty();
}

// ---------------------------------------------------------------------------
// SHIFT+PLAY OVERLAY — MIDI channel select, Section 3.12
// ---------------------------------------------------------------------------
void handleShiftPlayOverlayPad(uint8_t padIndex) {
  // v26: sets the channel of the GROUP the currently selected slot belongs
  // to, rather than one global channel. Deliberately NOT "select slot 1
  // first, it is the master" — nothing on the device would tell you that.
  // Any slot in the group works, so there is nothing to remember.
  //
  // v26.2: channels are now EXCLUSIVE — no two streams may share one. This
  // removes the case where two streams sent identical note numbers to the
  // same destination and cut each other's notes. Layering three streams onto
  // one instrument is still perfectly possible; it just belongs on the DAW
  // side, by pointing three channels at the same plugin.
  uint8_t wanted = padIndex + 1; // pad 1..16 -> channel 1..16
  uint8_t mine = state.streamChannel[currentStream];
  if (wanted == mine) return;

  // If another stream holds the channel you picked, SWAP with it rather than
  // refusing the press. Both streams always end up distinct, no channel is
  // lost, and pressing again puts things back — where a refusal would just
  // look like a dead pad.
  for (uint8_t s = 0; s < NUM_STREAMS; s++) {
    if (s == currentStream) continue;
    if (state.streamChannel[s] != wanted) continue;

    flushStreamNotes(s); // before its channel moves out from under it
    state.streamChannel[s] = mine;
    Serial.print("CHANNEL SWAP: stream "); Serial.print((char)('A' + s));
    Serial.print(" now on "); Serial.println(mine);
    break;
  }

  flushStreamNotes(currentStream);
  state.streamChannel[currentStream] = wanted;
  Serial.print("MIDI CHANNEL for stream "); Serial.print((char)('A' + currentStream));
  Serial.print(" set to "); Serial.println(wanted);
  markDirty();
}

// ---------------------------------------------------------------------------
// MUTE / CLEAR
// ---------------------------------------------------------------------------

void clearVoice(uint8_t voice) {
  for (uint8_t i = 0; i < MAX_STEPS; i++) state.slots[activeSlot[currentStream]].steps[voice][i] = 0;
  recalcHasContent(activeSlot[currentStream]);
  markDirty();
  recalcLoopLength();
}

void clearAllVoices() {
  for (uint8_t v = 0; v < NUM_VOICES; v++) clearVoice(v);
}

void clearSlot(uint8_t slot) {
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    for (uint8_t i = 0; i < MAX_STEPS; i++) state.slots[slot].steps[v][i] = 0;
  }
  state.slots[slot].shuffle = 50; // back to straight/no swing
  state.slots[slot].gatePercent = GATE_PERCENT_DEFAULT;
  state.slots[slot].noteBankWindow = false;
  state.slots[slot].hasContent = false;
  // v26.5: wipe this slot's mutes as well. Leaving them behind on an emptied
  // slot sets an invisible trap — the next thing programmed there would be
  // silent with nothing on screen to explain why, since the Mute overlay only
  // flashes voices you think to go and look at.
  for (uint8_t v = 0; v < NUM_VOICES; v++) mutedVoice[slot][v] = false;
  if (slot == activeSlot[currentStream]) {
    noteBankWindow[currentStream] = false;
    recalcLoopLength();
  }
  markDirty();
  Serial.print("SLOT "); Serial.print(slot); Serial.println(" cleared");
}

void recalcHasContent(uint8_t slot) {
  // Only needed after a clear — writes elsewhere just set the flag true directly,
  // since a scan is only required to detect a slot becoming empty, not active.
  bool has = false;
  for (uint8_t v = 0; v < NUM_VOICES && !has; v++) {
    for (uint8_t i = 0; i < MAX_STEPS; i++) {
      if (state.slots[slot].steps[v][i] > 0) { has = true; break; }
    }
  }
  state.slots[slot].hasContent = has;
}

// v26.5: unmutes the pattern you are looking at, not the stream. Mutes are
// per slot now, and Clear+Mute is a "get this pattern audible again" gesture;
// reaching across every slot in the stream would silently undo mutes you set
// in patterns that are not even on screen.
void unmuteAll() {
  uint8_t slot = activeSlot[currentStream];
  for (uint8_t v = 0; v < NUM_VOICES; v++) mutedVoice[slot][v] = false;
}

void recalcLoopLengthFor(uint8_t s) {
  loopIsTwoBar[s] = false;
  for (uint8_t v = 0; v < NUM_VOICES && !loopIsTwoBar[s]; v++) {
    for (uint8_t i = 16; i < 32; i++) {
      if (state.slots[activeSlot[s]].steps[v][i] > 0) { loopIsTwoBar[s] = true; break; }
    }
  }
}

// Convenience for the editing paths, which always act on the visible stream.
void recalcLoopLength() { recalcLoopLengthFor(currentStream); }

// ---------------------------------------------------------------------------
// ROTARY KNOB — bounded/absolute.
//   Play held, DAW stopped        -> MIDI lead (v26.9)
//   shift held, nothing else held -> global shuffle for the active slot
//     (Section 3.4). Works in either mode — it's a global groove setting,
//     not tied to Pattern mode specifically. (Previously this whole
//     function returned immediately unless already in Pattern mode, which
//     is why Shift+knob appeared to do nothing outside Pattern mode.)
//   Pattern mode, pad held, no shift -> that step's velocity (Section 3.3);
//     v26.8: an empty step is created at that velocity
//   Note mode, pad held, no shift    -> that voice's audition/live velocity,
//     with real-time audible + LED feedback (dim = soft, bright = hard)
// ---------------------------------------------------------------------------
bool anyPatternPadHeld() {
  for (uint8_t p = 0; p < 16; p++) {
    if (padHeld[p]) return true;
  }
  return false;
}

void handleKnob() {
  uint16_t raw = readKnob();

  // 0.3.3: any modifier or held pad re-arms the density's "moved" check.
  bool bare = !playHeld && !shiftHeld && !muteHeld && !patternHeld && !clearHeld &&
              !bankHeld && !noteHeld && !anyPatternPadHeld() && heldNoteVoice < 0;
  if (!bare || densityRearm) {
    densityKnobMoved = false;
    densityKnobBaseline = raw;
    densityRearm = false;
  }

  // v26.9: Play held while the DAW is stopped sets the MIDI lead, 0..16ms in
  // 1ms steps (v26.9.1). Only while stopped: Play is the local stop/start while the DAW
  // runs, and the lead should not shift under a take being recorded.
  if (playHeld && !shiftHeld && !dawPlaying) {
    if (!playKnobMoved) {
      int16_t delta = (int16_t)raw - (int16_t)playKnobBaseline;
      if (delta < 0) delta = -delta;
      if (delta > KNOB_MOVE_DEADZONE) playKnobMoved = true;
    }
    if (playKnobMoved) {
      uint8_t pads = (uint8_t)(((uint32_t)raw * 17) / (ADC_MAX + 1)); // 0..16
      uint8_t lead = pads * MIDI_LEAD_STEP_MS;
      if (lead != midiLeadMs) {
        midiLeadMs = lead;
        midiLeadDirty = true;
      }
    }
    return;
  }

  // 0.3.3: the knob on its own sets the density. Like the other knob
  // controls it takes over only once moved, so touching a modifier and
  // letting go never jumps it.
  if (bare) {
    if (!densityKnobMoved) {
      int16_t delta = (int16_t)raw - (int16_t)densityKnobBaseline;
      if (delta < 0) delta = -delta;
      if (delta > KNOB_MOVE_DEADZONE) densityKnobMoved = true;
    }
    if (densityKnobMoved) densityLevel = raw;
    return;
  }

  // 0.3.2: Pattern + knob (was the beat repeat) does nothing.
  if (patternHeld && !shiftHeld && !clearHeld && !muteHeld) return;

  // v26.1, back in 0.3.4: Mute held (without Shift) sets this pattern's gate
  // length, the note length as a percentage of a step. Only once the knob has
  // moved since Mute went down, so pressing Mute alone never changes it.
  if (muteHeld && !shiftHeld && !anyPatternPadHeld() && heldNoteVoice < 0) {
    if (!muteKnobMoved) {
      int16_t delta = (int16_t)raw - (int16_t)muteKnobBaseline;
      if (delta < 0) delta = -delta;
      if (delta > KNOB_MOVE_DEADZONE) muteKnobMoved = true;
    }
    if (muteKnobMoved) {
      uint8_t gate = map(raw, 0, ADC_MAX, GATE_PERCENT_MIN, GATE_PERCENT_MAX);
      if (state.slots[activeSlot[currentStream]].gatePercent != gate) {
        state.slots[activeSlot[currentStream]].gatePercent = gate;
        markDirty();
      }
    }
    return;
  }

  // !muteHeld added in v26.1 so Shift+Mute (whole-stream mutes) cannot alter
  // shuffle as a side effect of touching the knob mid-gesture.
  if (shiftHeld && !muteHeld && !anyPatternPadHeld() && heldNoteVoice < 0) {
    // Confirmed via Serial debug data: raw increases with clockwise
    // rotation, so clockwise = more shuffle needs an increasing map().
    // v26.2: same deadzone guard as gate length above. This flaw predates
    // the gate control — pressing Shift alone would snap shuffle to the
    // knob's parked position — it was just less noticeable than note length.
    if (!shiftKnobMoved) {
      int16_t delta = (int16_t)raw - (int16_t)shiftKnobBaseline;
      if (delta < 0) delta = -delta;
      if (delta > KNOB_MOVE_DEADZONE) shiftKnobMoved = true;
    }
    if (shiftKnobMoved) {
      uint8_t shuffle = map(raw, 0, ADC_MAX, 50, 75);
      if (state.slots[activeSlot[currentStream]].shuffle != shuffle) {
        state.slots[activeSlot[currentStream]].shuffle = shuffle;
        markDirty();
      }
    }
    return;
  }

  if (gridMode == MODE_PATTERN && !shiftHeld && anyPatternPadHeld()) {
    // Loop over every currently-held pad so hold+turn velocity editing
    // still works correctly even with several pads held at once — each
    // pad judges knob movement against its OWN baseline, captured at the
    // moment IT was pressed, not a single shared one.
    for (uint8_t p = 0; p < 16; p++) {
      if (!padHeld[p]) continue;
      if (!padKnobMoved[p]) {
        int16_t delta = (int16_t)raw - (int16_t)padKnobBaseline[p];
        if (delta < 0) delta = -delta;
        if (delta > KNOB_MOVE_DEADZONE) padKnobMoved[p] = true;
      }
      if (padKnobMoved[p]) {
        uint8_t globalStep = currentStepBank[currentStream] * 16 + p;
        uint8_t vel = map(raw, 0, ADC_MAX, 1, 127); // 0 is reserved for "off"
        uint8_t slot = activeSlot[currentStream];
        uint8_t &s = state.slots[slot].steps[lastSelectedVoice[currentStream]][globalStep];
        // v26.8: an EMPTY step held and turned is created at the knob's
        // velocity, so placing a step and setting its velocity is one
        // gesture. Before, only an already-active step was adjusted, and a
        // new step had to be tapped in first and then held again. The
        // release sees padKnobMoved and leaves the step on (no toggle).
        if (s != vel) {
          bool created = (s == 0);
          s = vel;
          if (created) {
            state.slots[slot].hasContent = true;
            recalcLoopLength();
          }
          markDirty();
        }
      }
    }
    return;
  }

  if (gridMode == MODE_NOTE && !shiftHeld && heldNoteVoice >= 0) {
    // Same deadzone-gated "has the knob actually moved since this pad was
    // pressed" check used for Pattern-mode step velocity, just above.
    // Without it, merely pressing a note pad immediately snapped that
    // voice's velocity to wherever the knob physically happened to be
    // sitting — e.g. leftover from adjusting shuffle a moment earlier —
    // even if you never touched the knob for this press at all.
    if (!knobMovedSinceHold) {
      int16_t delta = (int16_t)raw - (int16_t)knobBaselineRaw;
      if (delta < 0) delta = -delta;
      if (delta > KNOB_MOVE_DEADZONE) knobMovedSinceHold = true;
    }
    if (knobMovedSinceHold) {
      uint8_t vel = map(raw, 0, ADC_MAX, 1, 127);
      // Silent: only the stored value and its LED brightness update while
      // turning the knob. No audible retrigger here anymore — press the
      // pad again (a fresh press) to hear the newly-set velocity.
      voiceVelocity[heldNoteVoice] = vel;
    }
    return;
  }
}

// ---------------------------------------------------------------------------
// TRANSPORT
// ---------------------------------------------------------------------------
void handlePlayStopPress() {
  if (!dawPlaying) return;
  if (!locallyStopped) {                 // stop now
    locallyStopped = true;
    resumePending = false;
  } else {
    resumePending = !resumePending;      // arm (or cancel) the restart on the next beat
  }
  Serial.print("LOCAL "); Serial.println(locallyStopped ? (resumePending ? "RESUME ARMED" : "STOPPED") : "PLAYING");
  // 0.3.3: the clock keeps being followed while locally stopped, so the
  // step timing and the MIDI lead stay measured: nothing to reset here.
  // Locally stopping is also a safe moment to flush anything pending — see
  // the note by forceSave()'s call sites about why saves are held off while
  // isPlayingNow() is true.
  if (locallyStopped && dirty) forceSave();
}

void onMidiClock() {
  if (!dawPlaying) return;   // 0.3.3: counted while locally stopped too (silently)

  // v26.2: fire on the FIRST clock tick after Start, not the sixth.
  //
  // The counter used to be incremented BEFORE the comparison, so with
  // clockTickCount reset to 0 by onMidiStart(), step 0 did not fire until the
  // sixth tick — a full 16th note, 125ms at 120bpm, behind the downbeat. The
  // spacing between steps was correct, so it never drifted or sounded wrong
  // on its own; the whole pattern simply sat late against the DAW. Audible
  // immediately against a metronome, which is how it was found.
  //
  // v26.9: the latency left after that (USB both ways, the DAW's buffers) is
  // what the MIDI lead takes out — see midiLeadMs and serviceMidiLead().
  uint32_t nowUs = micros();
  if (clockTickCount == 0) {
    // A step's own tick. Step duration is measured here, between these ticks,
    // rather than between advancePlayhead() calls, which now move with the
    // lead.
    uint32_t now = millis();
    if (lastStepAdvanceMillis > 0) {
      uint32_t interval = now - lastStepAdvanceMillis;
      stepDurationEstimateMs = (stepDurationEstimateMs == 0)
        ? interval
        : (stepDurationEstimateMs * 3 + interval) / 4;
    }
    lastStepAdvanceMillis = now;
    if (lastBoundaryMicros != 0) {
      uint32_t us = nowUs - lastBoundaryMicros;
      // Lighter smoothing than the ms estimate above: this one predicts
      // when the next step is due, so it has to follow a tempo change
      // within a beat. Step-to-step intervals are already steady (USB
      // jitter is on single ticks, and a step spans six).
      stepDurationEstimateUs = (stepDurationEstimateUs == 0)
        ? us
        : (stepDurationEstimateUs + us) / 2;
    }
    lastBoundaryMicros = nowUs;

    if (stepFiredEarly) {
      stepFiredEarly = false;          // already sent ahead of this tick
    } else {
      leadOfLastAdvance = 0;
      advancePlayhead();               // first step, or no lead: on the tick
    }
  }
  lastTickMicros = nowUs;
  clockTickCount++;
  if (clockTickCount >= TICKS_PER_STEP) clockTickCount = 0;
  serviceMidiLead(); // the lead may already be due on this very tick
}

// v26.9: sends the coming step midiLeadMs before its tick is due. Called from
// loop() and after every clock tick. The due time is predicted from the most
// recent tick plus the ticks still to come, so a tempo change is followed
// within a tick. Needs one measured step first, so the very first step after
// Start goes out on its tick.
void resetMidiLead() {
  stepFiredEarly = false;
  lastTickMicros = 0;
  lastBoundaryMicros = 0;
  stepDurationEstimateUs = 0;
  leadOfLastAdvance = 0;
}

void serviceMidiLead() {
  if (midiLeadMs == 0 || stepFiredEarly || !(isPlayingNow() || resumePending)) return;
  if (stepDurationEstimateUs == 0 || lastTickMicros == 0) return;
  // Ticks already counted in this step, the step's own tick included.
  uint8_t done = (clockTickCount == 0) ? TICKS_PER_STEP : clockTickCount;
  uint32_t tickUs = stepDurationEstimateUs / TICKS_PER_STEP;
  uint32_t dueUs = lastTickMicros + (uint32_t)(TICKS_PER_STEP + 1 - done) * tickUs;
  // Never more than 3/4 of a step early, so a step can't go out before the
  // one in front of it has started (fast tempos with a long lead).
  uint32_t leadUs = (uint32_t)midiLeadMs * 1000;
  if (leadUs > stepDurationEstimateUs * 3 / 4) leadUs = stepDurationEstimateUs * 3 / 4;
  if ((int32_t)(micros() - (dueUs - leadUs)) < 0) return;
  stepFiredEarly = true;
  // How early this step really is, for live-record placement.
  int32_t early = (int32_t)(dueUs - micros());
  leadOfLastAdvance = early > 0 ? (uint8_t)((early + 500) / 1000) : 0;
  advancePlayhead();
}

// v26.9: MIDI lead persistence — a two-byte file of its own, so the pattern
// save (SavedState) keeps its layout.
const char *LEAD_PATH = "/lead.dat";
const uint8_t LEAD_MAGIC = 0x4C; // 'L'

void loadMidiLead() {
  midiLeadMs=MIDI_LEAD_DEFAULT_MS;
}

void saveMidiLead() {
  midiLeadDirty=false;
}

void onMidiStart() {
  dawPlaying = true;
  locallyStopped = false;
  resumePending = false;
  playhead = 0;
  clockTickCount = 0;
  stepDurationEstimateMs = 0; // discard any estimate from before — fresh start
  lastStepAdvanceMillis = 0;
  resetMidiLead();
  for (uint8_t s = 0; s < NUM_STREAMS; s++) pendingTriggerStep[s] = -1;
  // v26.10: chains start again from their first entry.
  for (uint8_t s = 0; s < NUM_STREAMS; s++) {
    if (chainStopPending[s]) { chainOn[s] = false; chainStopPending[s] = false; }
    if (chainStartPending[s]) {
      startChain(s);
    } else if (chainOn[s]) {
      chainPos[s] = 0; chainStep[s] = 0; chainWrap[s] = false;
      chainSelect(s, chainSlots[s][0]);
    }
  }
  Serial.println("DAW START");
}

void onMidiContinue() {
  dawPlaying = true;
  locallyStopped = false;
  resumePending = false;
  lastStepAdvanceMillis = 0; // the gap since the stop is not a step
  resetMidiLead();
  Serial.println("DAW CONTINUE");
}

void onMidiStop() {
  dawPlaying = false;
  locallyStopped = false;
  resumePending = false;
  resetMidiLead();
  for (uint8_t s = 0; s < NUM_STREAMS; s++) pendingTriggerStep[s] = -1;
  allNotesOff();
  Serial.println("DAW STOP");
  // Transport has genuinely stopped, so this is a safe moment to take the
  // flash-write pause without it costing an audible gap in playback — see
  // the note by forceSave()'s call sites.
  if (dirty) forceSave();
}

void advancePlayhead() {
  // v26: playhead is now the GLOBAL step. Each stream reduces it modulo its
  // own loop length, which is the whole mechanism by which a 1-bar pattern
  // repeats twice against a 2-bar one while staying locked to it.
  uint8_t globalLen = globalLoopLen();
  uint8_t thisStep = playhead % globalLen; // guards a shrink since last pass

  // Pattern switches are quantised to the GLOBAL loop boundary, not each
  // stream's own, so all three land together and stay in sync. v26.10:
  // applied here, just BEFORE step 0 plays, instead of just after the last
  // step: a swung last step fires a little later, and was played from the
  // NEW pattern. A pending chain stop lands here too.
  if (thisStep == 0) {
    for (uint8_t s = 0; s < NUM_STREAMS; s++) {
      if (chainStopPending[s] || pendingSlotSwitch[s] >= 0) {
        chainOn[s] = false;
        chainStopPending[s] = false;
      }
      if (pendingSlotSwitch[s] >= 0) applySlotSwitch(s, (uint8_t)pendingSlotSwitch[s]);
    }
    globalLen = globalLoopLen();
  }
  playhead = (thisStep + 1) % globalLen;

  // v26.10: each stream's real step (chains count their own).
  for (uint8_t s = 0; s < NUM_STREAMS; s++) streamLocalNow[s] = streamStepFor(s, thisStep);

  // 0.3.3: locally stopped = the timeline runs on silently. An armed restart
  // comes in on the next beat (every 4th step of the DAW's bar).
  if (resumePending && thisStep % 4 == 0) { resumePending = false; locallyStopped = false; }
  if (locallyStopped) return;

  // (Step duration is measured in onMidiClock() — v26.9: on the step's own
  // tick, since this can now run ahead of it by the MIDI lead.)

  // v26: every stream fires on the same global step, but each swings by its
  // OWN active pattern's shuffle — swung drums against a straight bass. Both
  // loop lengths are even, so the on/off-beat test is the same whether taken
  // on the global or the local step.
  bool offBeat = (thisStep % 2) == 1;
  for (uint8_t s = 0; s < NUM_STREAMS; s++) {
    uint8_t localStep = streamLocalNow[s];
    uint8_t shuffleAmt = state.slots[playSlot(s)].shuffle;
    float delayMs = shuffleDelayMsForStep(offBeat, shuffleAmt, stepDurationEstimateMs);

    if (delayMs > 0.5f) {
      // Schedule it instead of firing now — see pendingTriggerStep comment.
      pendingTriggerStep[s] = localStep;
      pendingTriggerAtMillis[s] = millis() + (uint32_t)delayMs;
    } else {
      triggerStreamStep(s, localStep);
    }
  }

  // v26.5: note repeat rides the same step clock, so a roll is locked to the
  // grid instead of drifting against it. Fired here, outside the per-stream
  // loop above, which also makes it deliberately UNSWUNG: the request was a
  // straight 1/16th roll, and swinging it would turn an even roll into a
  // shuffle the moment the underlying pattern had any.
  fireNoteRepeats(thisStep);
}

// The pattern a stream plays from (0.3.2: the beat repeat that could hold an
// older one is gone, so this is always the active pattern).
inline uint8_t playSlot(uint8_t s) { return activeSlot[s]; }

// v26.10: a chain entry becomes the stream's active pattern — what a recall
// does, minus the parts that only make sense for a deliberate switch.
void chainSelect(uint8_t s, uint8_t slot) {
  if (slot != activeSlot[s]) {
    activeSlot[s] = slot;
    noteBankWindow[s] = state.slots[slot].noteBankWindow;
    state.lastActiveSlot[s] = slot;
    markDirty();
  }
  recalcLoopLengthFor(s);
  if (!loopIsTwoBar[s]) currentStepBank[s] = 0;
}

void startChain(uint8_t s) {
  memcpy(chainSlots[s], chainNext[s], chainNextLen[s]);
  chainLen[s] = chainNextLen[s];
  chainOn[s] = chainLen[s] > 0;
  chainStartPending[s] = false;
  chainPos[s] = 0;
  chainStep[s] = 0;
  chainWrap[s] = false;
  if (chainOn[s]) chainSelect(s, chainSlots[s][0]);
  Serial.print("CHAIN start, stream "); Serial.print((char)('A' + s));
  Serial.print(", "); Serial.print(chainLen[s]); Serial.println(" entries");
}

// v26.10: the step stream s plays now. Unchained: the shared playhead modulo
// its length, as always; a new chain waiting starts when that wraps (the
// current pattern has ended). Chained: its own count through the entry, and
// the next entry once one has ended.
uint8_t streamStepFor(uint8_t s, uint8_t thisStep) {
  if (!chainOn[s]) {
    uint8_t local = thisStep % streamLoopLen(s);
    if (!(chainStartPending[s] && local == 0)) return local;
    startChain(s);
    if (!chainOn[s]) return local;
  } else if (chainWrap[s]) {
    chainWrap[s] = false;
    if (chainStartPending[s]) {
      startChain(s);                      // a new chain replaces this one
    } else {
      chainPos[s] = (uint8_t)((chainPos[s] + 1) % chainLen[s]);
      chainSelect(s, chainSlots[s][chainPos[s]]);
    }
  }
  uint8_t len = streamLoopLen(s);
  uint8_t local = chainStep[s] % len;
  chainStep[s] = (uint8_t)((local + 1) % len);
  if (chainStep[s] == 0) chainWrap[s] = true;
  return local;
}

// v26.10: chain mode.
void enterChainEdit() {
  chainEditing = true;
  chainEditStream = currentStream;
  chainEditLen = 0;
  gridMode = MODE_PATTERN;
  recordArmed = false;
  Serial.print("CHAIN mode, stream "); Serial.println((char)('A' + chainEditStream));
}

void chainCancel(uint8_t s) {
  chainStartPending[s] = false;
  if (chainOn[s]) {
    if (dawPlaying) chainStopPending[s] = true;   // at the next global boundary
    else chainOn[s] = false;
  }
}

void finishChainEdit() {
  chainEditing = false;
  uint8_t s = chainEditStream;
  if (chainEditLen == 0) {                 // left without a slot: the chain stops
    chainCancel(s);
    Serial.println("CHAIN off");
    return;
  }
  memcpy(chainNext[s], chainEditList, chainEditLen);
  chainNextLen[s] = chainEditLen;
  pendingSlotSwitch[s] = -1;
  chainStopPending[s] = false;
  if (dawPlaying) chainStartPending[s] = true;   // when the current pattern ends
  else startChain(s);
}

void chainEditPad(uint8_t padIndex) {
  uint8_t gs = slotForPad(padIndex);
  if (streamOfSlot(gs) != chainEditStream) return;   // the other stream's half of the grid
  if (chainEditLen >= CHAIN_MAX) return;
  chainEditList[chainEditLen++] = gs;
  chainFlashPad = (int8_t)padIndex;
  chainFlashUntil = millis() + 150;
}

// Checked once per loop() pass — fires any swing-delayed step whose time has
// arrived. Cancels (without firing) if playback stopped in the meantime, so
// a stray note-on can't sneak out after Play/Stop or a DAW stop.
void handlePendingSwingTrigger() {
  for (uint8_t s = 0; s < NUM_STREAMS; s++) {
    if (pendingTriggerStep[s] < 0) continue;
    if (!isPlayingNow()) {
      pendingTriggerStep[s] = -1;
      continue;
    }
    if ((int32_t)(millis() - pendingTriggerAtMillis[s]) >= 0) {
      triggerStreamStep(s, (uint8_t)pendingTriggerStep[s]);
      pendingTriggerStep[s] = -1;
    }
  }
}

// Suppresses the very next natural playback of a freshly live-recorded hit.
// Without this, a note recorded live (especially one rounded
// forward onto the imminent next step — see the live-record quantization
// above) could get played again by the normal playback engine within
// milliseconds, often right around when the pad is physically released.
// That's not an extra Note On/Off from the button handling itself — it's
// the sequencer correctly playing back what was just written, just very
// soon after writing it — but it's perceived as an unwanted second trigger.
// voiceHeldLive[currentStream][] alone doesn't prevent this because it only guards while
// the pad is still physically held; releasing before the natural trigger
// point (very possible, since the round-up case specifically targets the
// step that's about to start) lets it through.
struct PendingSuppression { int8_t voice; uint8_t step; uint32_t expiresAt; bool active; };
const uint8_t NUM_SUPPRESS_SLOTS = 8;
PendingSuppression pendingSuppress[NUM_SUPPRESS_SLOTS];

void suppressNextPlayback(uint8_t voice, uint8_t step) {
  for (uint8_t i = 0; i < NUM_SUPPRESS_SLOTS; i++) {
    if (!pendingSuppress[i].active) {
      pendingSuppress[i] = {(int8_t)voice, step, millis() + 1000, true};
      return;
    }
  }
  pendingSuppress[0] = {(int8_t)voice, step, millis() + 1000, true}; // slots full: reuse slot 0
}

bool consumeSuppression(uint8_t voice, uint8_t step) {
  uint32_t now = millis();
  for (uint8_t i = 0; i < NUM_SUPPRESS_SLOTS; i++) {
    if (pendingSuppress[i].active && pendingSuppress[i].voice == (int8_t)voice
        && pendingSuppress[i].step == step) {
      pendingSuppress[i].active = false;
      return now <= pendingSuppress[i].expiresAt; // only actually suppress if still fresh
    }
  }
  return false;
}

// v26: fires one stream's step. localStep is already reduced modulo that
// stream's own loop length by advancePlayhead(), which is what makes a 1-bar
// pattern repeat twice against a 2-bar one.
// 0.3.3: density. Each hit gets a strength: its velocity, plus 40 on a beat
// and 15 on an off-beat eighth. The knob keeps the strongest share of the
// pattern's audible hits; at the far left the strongest eighth. Worked out
// from the pattern every step, so edits count straight away.
uint8_t densityScore(uint8_t vel, uint8_t step) {
  return (uint8_t)(vel + (step % 4 == 0 ? 40 : step % 2 == 0 ? 15 : 0));   // at most 167
}
uint8_t densityThreshold(uint8_t s) {
  if (densityLevel >= ADC_MAX - 8) return 0;            // full: everything plays
  static uint16_t hist[168];
  memset(hist, 0, sizeof(hist));
  const uint8_t slotIdx = playSlot(s);
  const PatternSlot &slot = state.slots[slotIdx];
  uint8_t len = streamLoopLen(s);
  uint16_t total = 0;
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    if (mutedVoice[slotIdx][v]) continue;
    for (uint8_t st = 0; st < len; st++) {
      uint8_t vel = slot.steps[v][st];
      if (vel) { hist[densityScore(vel, st)]++; total++; }
    }
  }
  if (total == 0) return 0;
  uint32_t keep = ((uint32_t)total * densityLevel + ADC_MAX / 2) / ADC_MAX;
  uint32_t least = (total + 7) / 8;
  if (keep < least) keep = least;
  uint32_t n = 0;
  for (int sc = 167; sc > 0; sc--) {                    // ties at the cut all play
    n += hist[sc];
    if (n >= keep) return (uint8_t)sc;
  }
  return 0;
}

void triggerStreamStep(uint8_t s, uint8_t localStep) {
  // The step whose content just actually fired — distinct from playhead,
  // which is already incremented to the NEXT step by the time this runs
  // (see advancePlayhead()). Live-record uses this, not playhead, as the
  // reference for "which step did the user just hear", so it tracks the
  // stream being edited.
  if (s == currentStream) {
    lastTriggeredStep = localStep;
    // v26.9: when the step was sent early (MIDI lead), it is heard that much
    // later — that is the moment a player lines up with.
    lastTriggeredStepMillis = millis() + leadOfLastAdvance;
  }

  // Playhead indicator: flash this step regardless of whether anything
  // actually plays here. Previously this was set only inside the per-voice
  // loop below, guarded by "if no note here, skip" — so a step only ever
  // flashed if a note happened to fire there. That made the moving
  // playhead invisible on every empty step, which looked like it was
  // "stuck" on whichever steps had content instead of sweeping through
  // the whole pattern in time with playback. This also now fires at the
  // swung (delayed) time for off-beat steps, so the visual flash and the
  // audible hit stay in sync.
  stepFlashUntil[s][localStep] = millis() + FLASH_MS;

  // A muted stream still counts — its playhead keeps advancing, so unmuting
  // drops back in sync mid-pattern instead of restarting. The flash above is
  // deliberately set first, so the grid still shows the playhead sweeping on
  // a stream you have silenced.
  if (streamMuted[s]) return;

  const PatternSlot &slot = state.slots[playSlot(s)];
  uint8_t keepFrom = densityThreshold(s);
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    uint8_t vel = slot.steps[v][localStep];
    if (vel == 0) continue;
    if (mutedVoice[playSlot(s)][v]) continue; // v26.5: per-pattern mutes
    if (densityScore(vel, localStep) < keepFrom) continue; // 0.3.3: thinned out
    if (voiceHeldLive[s][v]) continue; // already sounding live — don't retrigger
    // Live-record suppression only ever concerns the stream being edited,
    // which is the only one a pad press can write into.
    if (s == currentStream && consumeSuppression(v, localStep)) continue;

    // Debug kept to the edited stream only: with three streams firing, three
    // Serial lines per voice per step is enough traffic to matter.
    if (s == currentStream) {
      Serial.print("PLAYBACK firing voice "); Serial.print(v);
      Serial.print(" step "); Serial.print(localStep);
      Serial.print(" note "); Serial.println(midiNoteForVoice(v));
    }

    sendVoiceNoteOn(s, v, vel);
    voiceFlashUntil[s][v] = millis() + FLASH_MS;
    // v25.1: the Note Off is NOT sent here any more. It is scheduled by
    // sendVoiceNoteOn() and sent one gate length later by
    // servicePendingNoteOffs(), so the note has a real duration and will
    // actually trigger an envelope-based instrument. See the NOTE GATE
    // block above the note helpers.
  }
}

float shuffleDelayMsForStep(bool offBeat, uint8_t shufflePercent, uint32_t stepMs) {
  // 50% = no swing. 75% = maximum, i.e. the off-beat step lands a quarter of
  // a step-duration late. Uses the real, live-measured step duration for the
  // current tempo (stepDurationEstimateMs) instead of an assumed fixed
  // tempo, so swing amount stays correct whatever the DAW's tempo is.
  if (!offBeat) return 0.0f;
  if (stepMs == 0) return 0.0f; // no tempo estimate yet (e.g. right at start)
  float swingFraction = (shufflePercent - 50) / 100.0f; // 0..0.25
  return stepMs * swingFraction;
}

// v26: silences every stream, not just the one being edited. Called on DAW
// stop, so leaving two streams hanging would be a real stuck-note bug.
void allNotesOff() {
  for (uint8_t s = 0; s < NUM_STREAMS; s++) {
    for (uint8_t v = 0; v < NUM_VOICES; v++) sendVoiceNoteOff(s, v);
  }
}

uint8_t midiNoteForVoice(uint8_t voice) {
  int16_t note = BASE_MIDI_NOTE + voice; // fixed mapping, no octave transpose
  if (note < 0) note = 0;
  if (note > 127) note = 127;
  return (uint8_t)note;
}

// ---------------------------------------------------------------------------
// NOTE GATE (v25.1) — why notes are no longer zero-length
// ---------------------------------------------------------------------------
// Until v25.1 both note paths sent Note On immediately followed by Note Off,
// producing a note with NO duration. Drum samplers never noticed: they are
// one-shot, so Note On fires the whole sample and the Note Off is discarded.
// A synth gates its envelope on Note On/Note Off, so a zero-duration note
// opened and shut the envelope in the same instant — silence, or a click.
//
// Confirmed on hardware 11 Oct 2026: in Logic Pro the notes landed in the
// piano roll ONE TICK long and virtual instruments produced nothing, while
// Ultrabeat and Drum Machine Designer worked perfectly. The same device
// worked in Ableton Live, which appears to let the Note Off fall into a later
// processing block and so gives the note a buffer's worth of length.
//
// The fix is a real gate: the Note Off is scheduled a gate length later and
// sent from servicePendingNoteOffs() in loop(). Non-blocking, so sequencer
// timing is untouched.
//
// DO NOT "fix" this by moving the Note Off back to pad release. That is what
// v13 removed, and it reintroduces the audible double-trigger on release
// (release samples, or the abrupt cutoff of a still-sounding note). The
// scheduled gate keeps both problems solved at once.
// ---------------------------------------------------------------------------
// The gate constants and voiceNoteOffAt[][] are declared further up, with the
// other per-voice runtime state: the pad dispatcher reads them when handling
// a mute, and that sits much earlier in the file than this.

// v26: every note send names its stream explicitly. The same voice number is
// a different destination on each stream's channel, so the channel, the
// pending-gate slot and the note all have to agree.
// v26.1: how long this stream's notes should be held, in ms. A percentage of
// the measured step duration, so it tracks tempo. Falls back to a fixed
// audition length when nothing is playing and there is no step to measure —
// otherwise a pad press with the transport stopped would be a click.
uint32_t gateMsForStream(uint8_t s) {
  if (stepDurationEstimateMs == 0) return AUDITION_GATE_MS;
  uint8_t pct = state.slots[activeSlot[s]].gatePercent;   // 0.3.4: per pattern again
  if (pct < GATE_PERCENT_MIN) pct = GATE_PERCENT_DEFAULT; // unset/corrupt
  uint32_t g = ((uint32_t)stepDurationEstimateMs * pct) / 100;
  if (g < NOTE_GATE_MIN_MS) g = NOTE_GATE_MIN_MS;
  return g;
}

void sendVoiceNoteOn(uint8_t s, uint8_t voice, uint8_t velocity) {
  // Close a still-open note on this voice first, so note boundaries stay
  // clean when a step retriggers inside the gate window — which happens at
  // fast tempos, where a 16th can be shorter than the gate.
  if (voiceNoteOffAt[s][voice] != 0) {
    boardSendNoteOff(midiNoteForVoice(voice), 0, state.streamChannel[s]);
  }
  boardSendNoteOn(midiNoteForVoice(voice), velocity, state.streamChannel[s]);
  uint32_t due = millis() + gateMsForStream(s);
  if (due == 0) due = 1; // 0 is the "nothing pending" sentinel
  voiceNoteOffAt[s][voice] = due;
}

// Immediate Note Off. Also cancels any pending gate for this voice, so the
// deferred send can't fire a second, spurious Note Off afterwards.
void sendVoiceNoteOff(uint8_t s, uint8_t voice) {
  voiceNoteOffAt[s][voice] = 0;
  boardSendNoteOff(midiNoteForVoice(voice), 0, state.streamChannel[s]);
}

// Called every loop pass, for every stream. Signed comparison so millis()
// rollover is handled.
void servicePendingNoteOffs() {
  uint32_t now = millis();
  for (uint8_t s = 0; s < NUM_STREAMS; s++) {
    for (uint8_t v = 0; v < NUM_VOICES; v++) {
      if (voiceNoteOffAt[s][v] == 0) continue;
      if ((int32_t)(now - voiceNoteOffAt[s][v]) >= 0) {
        voiceNoteOffAt[s][v] = 0;
        boardSendNoteOff(midiNoteForVoice(v), 0, state.streamChannel[s]);
      }
    }
  }
}

// v26.2: closes everything a stream currently has sounding. Must run BEFORE
// that stream's channel changes, because sendVoiceNoteOff() addresses the
// channel the stream holds right now — change it first and the Note Offs go
// to the new channel while the notes hang on the old one.
void flushStreamNotes(uint8_t s) {
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    if (voiceNoteOffAt[s][v] != 0) sendVoiceNoteOff(s, v);
  }
}

void playVoiceNote(uint8_t voice, uint8_t velocity) {
  // Audition and live-record hits use the same gate as step playback, so a
  // pad press and a sequenced hit sound identical. Always the stream being
  // edited — auditioning is an editing action. Nothing is sent on pad
  // release; see the note above about why that must stay true.
  sendVoiceNoteOn(currentStream, voice, velocity);
}

// ---------------------------------------------------------------------------
// NOTE REPEAT (v26.5) — see the state block up with the other pad state
// ---------------------------------------------------------------------------

// Shift+pad (or Note+Shift+pad) down / up. v26.8: a roll can now be recorded
// — when record is armed, or with Note held as well. Each hit lands on the
// step it plays on, added to what is there (nothing is erased between hits).
// A plain Shift+pad with record off is still a pure performance roll that
// writes nothing.
void handleNoteRepeatPad(uint8_t padIndex, bool pressed) {
  if (pressed) {
    uint8_t voice = noteBankWindow[currentStream] * 16 + padIndex;
    repeatVoiceForPad[padIndex] = (int8_t)voice;
    repeatRecordForPad[padIndex] = recordArmed || noteHeld;
    // The stream is remembered per pad, not once globally: the editing stream
    // can be changed mid-roll (Shift is already held, so Play+Bank completes
    // the dimension chord with the pad still down). A single shared value
    // would then send this roll's hits — and worse, its cleanup on release —
    // to a stream it never started on, leaving voiceHeldLive stuck set and
    // that voice permanently skipped by playback.
    repeatStreamForPad[padIndex] = currentStream;

    // Select it, exactly as an ordinary note-pad press would, so the grid and
    // a following switch to Pattern mode both land on what was just played.
    lastSelectedVoice[currentStream] = voice;
    // Claiming heldNoteVoice keeps the knob out of mischief: handleKnob()'s
    // shuffle branch requires heldNoteVoice < 0, so a knob nudged mid-roll
    // cannot quietly rewrite this pattern's swing. Its velocity branch needs
    // !shiftHeld and so stays out of the way too.
    heldNoteVoice = (int8_t)voice;

    // Stop playback double-triggering a voice the roll already owns, the same
    // way a plain held pad does.
    voiceHeldLive[currentStream][voice] = true;

    // First hit lands on the press, not one period later — a roll that began
    // with a silent gap would feel broken however correct the grid.
    uint32_t period = stepDurationEstimateMs ? stepDurationEstimateMs
                                             : REPEAT_FALLBACK_MS;
    repeatNextAtMillis = millis() + period;
    if (!mutedVoice[activeSlot[currentStream]][voice]) {
      sendVoiceNoteOn(currentStream, voice, voiceVelocity[voice]);
      voiceFlashUntil[currentStream][voice] = millis() + FLASH_MS;
    }
    // The first hit is recorded like a single played note — on the step it
    // was nearest to. The rest are written by fireNoteRepeats() on the step
    // each one plays on.
    if (repeatRecordForPad[padIndex] && isPlayingNow()) recordLiveHit(voice);
    Serial.print(repeatRecordForPad[padIndex] ? "NOTE REPEAT (recording) start voice "
                                              : "NOTE REPEAT start voice ");
    Serial.print(voice);
    Serial.print(" note "); Serial.println(midiNoteForVoice(voice));
    return;
  }

  int8_t voice = repeatVoiceForPad[padIndex];
  if (voice < 0) {
    // This pad is not rolling, so its press went down the ordinary note path —
    // Shift+Note went down after the pad did. Hand the release back there
    // rather than returning: handleNotePad() is what clears voiceHeldLive, and
    // leaving that set would stop playback ever retriggering this voice again.
    handleNotePad(padIndex, false);
    return;
  }
  uint8_t s = repeatStreamForPad[padIndex];
  repeatVoiceForPad[padIndex] = -1;
  repeatRecordForPad[padIndex] = false;
  voiceHeldLive[s][voice] = false;
  if (heldNoteVoice == voice) heldNoteVoice = -1;
  // Close the note now rather than leaving the last hit's gate to expire on
  // its own, so the roll stops the instant the pad comes up.
  sendVoiceNoteOff(s, (uint8_t)voice);
  // (int) cast, not bare int8_t: int8_t is signed char, which binds to
  // Print::print(char) and would print a glyph instead of a number.
  Serial.print("NOTE REPEAT stop voice "); Serial.println((int)voice);
}

// One hit for every rolling pad. Called from advancePlayhead() while the
// transport runs, with the global step being played, and from
// serviceNoteRepeat() while it does not, with -1: nothing is recorded then,
// as there is no step to record onto.
void fireNoteRepeats(int16_t globalStep) {
  for (uint8_t p = 0; p < 16; p++) {
    int8_t v = repeatVoiceForPad[p];
    if (v < 0) continue;
    uint8_t s = repeatStreamForPad[p];
    // Checked per hit, not once at the start: Mute+pad can land mid-roll, and
    // a muted voice should fall silent immediately like any other. The slot is
    // re-read per hit for the same reason — a queued pattern switch can land
    // under a running roll, and the roll should then follow the new pattern's
    // mutes rather than the ones it started with.
    if (mutedVoice[activeSlot[s]][v]) continue;
    if (streamMuted[s]) continue;
    sendVoiceNoteOn(s, (uint8_t)v, voiceVelocity[v]);
    voiceFlashUntil[s][v] = millis() + FLASH_MS;
    // v26.8: a recorded roll writes the hit onto this stream's step. Only
    // what is heard is recorded: a muted voice or stream (skipped above)
    // writes nothing, just as it plays nothing.
    if (repeatRecordForPad[p] && globalStep >= 0) {
      uint8_t localStep = streamLocalNow[s];   // v26.10: chains count their own steps
      uint8_t slot = activeSlot[s];
      if (state.slots[slot].steps[v][localStep] != voiceVelocity[v]) {
        state.slots[slot].steps[v][localStep] = voiceVelocity[v];
        state.slots[slot].hasContent = true;
        markDirty();
      }
    }
  }
}

// The stopped-transport path. Does nothing while playing — advancePlayhead()
// owns the timing then, and running both would double every hit.
void serviceNoteRepeat() {
  if (!anyNoteRepeatActive()) return;
  if (isPlayingNow()) return;
  if ((int32_t)(millis() - repeatNextAtMillis) < 0) return;
  uint32_t period = stepDurationEstimateMs ? stepDurationEstimateMs
                                           : REPEAT_FALLBACK_MS;
  repeatNextAtMillis = millis() + period;
  fireNoteRepeats(-1);
}

// ---------------------------------------------------------------------------
// LED INDICATORS — 20 total: 16 pads + Note + Pattern + Bank + Play.
// Assign real channel numbers to match your physical wiring.
// ---------------------------------------------------------------------------
const uint8_t LED_NOTE = 16;
const uint8_t LED_PATTERN = 17;
const uint8_t LED_BANK = 18;
const uint8_t LED_PLAY = 19;

// ---------------------------------------------------------------------------
// LED BRIGHTNESS LEVELS (v25.1)
// ---------------------------------------------------------------------------
// These were bare literals scattered through refreshLeds() until v25.1, which
// made them dangerous to tune: 220 appeared in four unrelated roles and 60
// meant both "populated slot" and "muted voice", so changing one state
// silently changed others. Named here so a change hits only what it should.
//
// NOTE ON PERCEIVED BRIGHTNESS: the IS31FL3731's PWM is linear in luminance,
// but a transparent keycap behaves as a light pipe rather than a diffuser —
// it fills with light at fairly low drive and then saturates visually, so
// extra drive mostly adds glare rather than apparent brightness. Two levels
// that look far apart on paper can be hard to tell apart through a clear cap.
// That is why the active slot blinks (below) instead of just sitting brighter.
// ---------------------------------------------------------------------------
const uint8_t LED_FULL             = 255; // flashes, channel overlay, bring-up
const uint8_t LED_MODE_ON          = 220; // Note/Pattern/Play/Bank "on"
const uint8_t LED_SLOT_ACTIVE      = 220; // active/queued slot, Pattern held
const uint8_t LED_SLOT_HAS_CONTENT = 60;  // populated but inactive slot
const uint8_t LED_VOICE_OVERVIEW   = 200; // voice has content (Note/Mute held)
const uint8_t LED_VOICE_MUTED      = 60;  // muted voice in the normal Note grid
const uint8_t LED_CHANNEL_TAKEN    = 60;  // channel held by a DIFFERENT stream
const uint8_t LED_COPY_SOURCE_DIM  = 80;  // copy-armed source pad, dim phase
const uint8_t LED_DELETE_ARM_DIM   = 30;  // delete-armed slot, dim phase
const uint8_t LED_BANK_NUDGE_HI    = 90;  // "there's more over here" nudge
const uint8_t LED_BANK_NUDGE_LO    = 10;
const uint8_t LED_VEL_MIN          = 40;  // velocity 1   -> this brightness
const uint8_t LED_VEL_MAX          = 210; // velocity 127 -> this brightness
const uint8_t LED_PLAYHEAD         = 30;  // 1.2.0: Note mode, record armed: the moving step
const uint8_t LED_PLAYHEAD_BAR     = 90;  // ...brighter on the first step of each bar
                                          // (headroom below 255 so the
                                          //  playback flash still reads)

// Active-slot blink (v25.1). The Pattern-held overlay is momentary — often
// visible for only a second — so a slow symmetric pulse can be caught on its
// off-phase and misread as "not active". This is a brief DIP out of solid
// instead: the pad never looks off, and it stays visually distinct from the
// existing symmetric pulses used for armed/destructive states.
const uint16_t SLOT_BLINK_PERIOD_MS = 300; // ~3.3 Hz
const uint16_t SLOT_BLINK_DIP_MS    = 80;  // dark portion of each cycle

// Muted-voice flash in the Mute-held overlay (v25.1). Symmetric is fine here:
// this overlay is held open while you work, not glanced at.
const uint16_t MUTE_FLASH_PERIOD_MS = 400; // 2.5 Hz

// Dimension indicator (v26): Play and Bank blink together while in dimension
// 1. Deliberately the slowest rate in the UI — 1 Hz against the 2 Hz "armed"
// pulse and the 3.3 Hz active-slot dip — because all three can be on screen
// at once and the eye reads rate far more reliably than brightness.
const uint16_t DIM_BLINK_HALF_MS = 500;
bool dimBlinkNow = false; // recomputed once per refreshLeds() pass

// Every Bank LED write goes through here so the dimension blink can layer on
// top of whatever Bank is otherwise saying (solid / nudge-pulse / off)
// without each branch having to know about dimensions.
inline void setBankLed(uint8_t level);

// ---------------------------------------------------------------------------
// LED_ADDR_MAP — logical LED index (0-15 = pads, LED_NOTE/LED_PATTERN/
// LED_BANK/LED_PLAY = 16-19) -> the IS31FL3731's actual internal PWM
// address for that physical LED.
//
// CONFIRMED on hardware via led_map_builder.ino — each entry was verified
// by pressing the physical key next to the lit LED, not guessed from the
// schematic. Order: Pad1-16, then Note, Pattern, Bank, Play.
// ---------------------------------------------------------------------------
uint8_t LED_ADDR_MAP[20] = {
  48, 64, 80, 96, 112, 128, 0, 33, 65, 81, 97, 113, 129, 1, 17, 50, 16, 32, 49, 66
};

// Cache of the last brightness actually sent to each logical LED. 255 is a
// valid brightness, so this starts at -1 (via int16_t) to guarantee the
// very first refreshLeds() pass writes every LED at least once.
int16_t lastSentBrightness[20] = {
  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

void setLed(uint8_t index, uint8_t brightness) {
  if (merged_bro_visible()) merged_led(index,brightness);
}

// v26.9: 400kHz (what Yummy runs this same chip at) and page 0
// selected once, so setLed() needs a single write. Called after
// ledDriver.begin(), which sets the chip up and may reset the bus speed.
void ledFastPathBegin() {

}

// ---------------------------------------------------------------------------
// BOOT SEQUENCE — runs once in setup(), right after the LED driver is
// confirmed present. Chases through all 20 LEDs in order so the user gets
// a clear "device is powered and the driver is alive" signal as soon as
// it's plugged into USB. Blocking is fine here — nothing else needs the
// CPU during this ~1 second window at power-on.
// ---------------------------------------------------------------------------
inline void setBankLed(uint8_t level) {
  if (dimBlinkNow) level = level ? 0 : LED_MODE_ON;
  setLed(LED_BANK, level);
}

void runBootLedSequence() {

}

// ---------------------------------------------------------------------------
// LED BRING-UP — verify LED_ADDR_MAP, and measure brightness through caps.
//
//   led N          light logical LED N (0-19) at full, all others off
//   led N B        ...at brightness B (0-255)                      [v25.1]
//   ledadd N B     ...without clearing what is already lit         [v25.1]
//   ledoff         clear the test, resume normal refreshLeds()
//
// Confirm each of the 20 positions before trusting LED_ADDR_MAP for real use.
// The brightness argument exists because perceived brightness depends heavily
// on what sits over the LED — a transparent keycap saturates visually at
// fairly low drive, so levels that look far apart numerically can be hard to
// tell apart in the hand. Use "led"/"ledadd" to put two levels side by side
// and find the real threshold instead of guessing at PWM values.
// ---------------------------------------------------------------------------
bool ledBringupActive = false;

void handleLedBringupSerial() {

}

void refreshLeds() {
  if (ledBringupActive) return; // frozen on whatever the bring-up test lit


  uint32_t now = millis();
  bool pulse = ((now / 250) % 2) == 0; // ~2Hz pulse for "armed" states

  // v26.8: blinks at the 2 Hz "armed" rate while record is armed.
  setLed(LED_NOTE, gridMode != MODE_NOTE ? 0
                   : (recordArmed && !pulse) ? 0 : LED_MODE_ON);
  // v26.5.1: also lit while Pattern is merely HELD, which in note mode is the
  // only feedback during the PATTERN_HOLD_MS wait — without it the press is
  // unacknowledged and the hold feels far longer than it is. Reads
  // consistently either way: the lamp is on whenever Pattern is doing
  // something, rather than strictly meaning "grid is in Pattern mode".
  // v26.10: blinks in chain mode, at the "armed" rate like record arm.
  setLed(LED_PATTERN, chainEditing ? (pulse ? LED_MODE_ON : 0)
                    : (gridMode == MODE_PATTERN || patternHeld) ? LED_MODE_ON : 0);
  // v26: Play and Bank blink together while in dimension 1 (streams B/C).
  // Implemented as an inversion rather than an override, so both LEDs keep
  // saying what they always said — Play still reads as "is playing", Bank
  // still carries its three states — and the dimension shows up as motion on
  // top. Either way round it reads as blinking.
  dimBlinkNow = (dimension == 1) && ((now / DIM_BLINK_HALF_MS) % 2 == 0);
  bool playOn = isPlayingNow();
  if (resumePending) playOn = (now / 100) % 2;   // 0.3.3: restart armed
  if (dimBlinkNow) playOn = !playOn;
  setLed(LED_PLAY, playOn ? LED_MODE_ON : 0);

  if (shiftHeld && playHeld) {
    // Channel-select overlay. v26.2: channels are exclusive, so the pads
    // other streams occupy are shown dim. Picking one of those swaps with
    // that stream — showing them makes the swap predictable instead of a
    // surprise.
    //   bright = this stream's channel
    //   dim    = held by another stream (pressing it swaps)
    //   off    = free
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t ch = i + 1;
      if (ch == state.streamChannel[currentStream]) {
        setLed(i, LED_FULL);
        continue;
      }
      bool heldByOther = false;
      for (uint8_t st = 0; st < NUM_STREAMS; st++) {
        if (st != currentStream && state.streamChannel[st] == ch) heldByOther = true;
      }
      setLed(i, heldByOther ? LED_CHANNEL_TAKEN : 0);
    }
    setBankLed(0);
    return;
  }

  // v26.9: Play held while stopped shows the MIDI lead, one pad per ms.
  if (playHeld && !shiftHeld && !dawPlaying) {
    uint8_t lit = midiLeadMs / MIDI_LEAD_STEP_MS;
    for (uint8_t i = 0; i < 16; i++) setLed(i, i < lit ? LED_MODE_ON : 0);
    setBankLed(0);
    return;
  }

  // v26.5: patternSlotOverlayActive() rather than patternHeld && MODE_PATTERN,
  // so the grid lights up as the slot picker in note mode too the moment the
  // hold passes PATTERN_HOLD_MS. The pad dispatcher reads the same helper, so
  // what the pads DO and what they SHOW cannot drift apart.
  if (patternSlotOverlayActive()) {
    if (clearHeld) {
      // Delete-slot armed (Clear+Pattern): every populated slot pulses as
      // a clear "this will get wiped, not loaded" warning, since this is
      // destructive and has no undo. Empty slots stay off — nothing there
      // to lose.
      for (uint8_t i = 0; i < 16; i++) {
        uint8_t gs = slotForPad(i); // v26: which slot this pad addresses
        setLed(i, state.slots[gs].hasContent ? (pulse ? LED_FULL : LED_DELETE_ARM_DIM) : 0);
      }
      setBankLed(0);
      return;
    }

    // Slot recall/copy overlay: bright = active/queued slot, dim = has content,
    // off = genuinely empty. Lets a glance tell you what's worth checking out or
    // safe to overwrite via copy, not just which slot is currently active.
    // v25.1: the active slot now DIPS briefly instead of merely sitting
    // brighter. Through transparent keycaps the old 220-vs-60 pair was hard
    // to tell apart — a clear cap fills with light and saturates visually, so
    // the extra drive read as glare rather than as brightness. Motion is
    // unambiguous whatever the light path. Fast and asymmetric because this
    // overlay is momentary: a slow symmetric pulse can be glanced at during
    // its off-phase and misread as "not the active slot".
    // v26: each pad addresses a global slot that depends on the dimension,
    // and "is this the active one" is asked of that slot's OWN stream — in
    // dimension 1 the grid shows two streams at once, each with its own
    // active slot, so two pads are legitimately lit as active.
    bool slotDip = (now % SLOT_BLINK_PERIOD_MS) < SLOT_BLINK_DIP_MS;
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t gs = slotForPad(i);
      uint8_t gstream = streamOfSlot(gs);
      uint8_t shownSlot = (pendingSlotSwitch[gstream] >= 0)
                            ? (uint8_t)pendingSlotSwitch[gstream]
                            : activeSlot[gstream];
      if (armedCopySource == (int8_t)gs) {
        setLed(i, pulse ? LED_FULL : LED_COPY_SOURCE_DIM);
      } else if (gs == shownSlot) {
        setLed(i, slotDip ? 0 : LED_SLOT_ACTIVE);
      } else if (state.slots[gs].hasContent) {
        setLed(i, LED_SLOT_HAS_CONTENT);
      } else {
        setLed(i, 0);
      }
    }
    setBankLed(0);
    return;
  }

  // v25.1: Mute held — an overview of what there is to mute, in BOTH modes.
  //   lit   = this voice has something programmed in the current pattern
  //   flash = this voice is muted (shown whether or not it has content, so a
  //           mute set by accident is still visible and therefore undoable)
  //   dark  = empty and unmuted
  // Shows the same 16 voices the Note grid shows, following noteBankWindow[currentStream],
  // so Bank switches which half is visible and mutable. In Pattern mode this
  // temporarily repurposes the grid from steps to voices, the same way the
  // Pattern-held and Shift+Play overlays already do.
  // v26: Shift+Mute — whole-stream mutes on the first three pads. This is
  // also the only cross-stream status view there is: it answers "does that
  // stream have anything in it, and is it audible" at a glance, which is
  // most of what you need mid-performance even though it cannot show you
  // WHAT the other streams are playing.
  if (shiftHeld && muteHeld) {
    bool streamFlashOn = (now % MUTE_FLASH_PERIOD_MS) < (MUTE_FLASH_PERIOD_MS / 2);
    for (uint8_t i = 0; i < 16; i++) {
      // 0.3.2: pad 2 = Yummy MIDI, pad 3 = Yummy audio; lit, flashing when muted.
      if (i == 1 || i == 2) {
        bool m = i == 1 ? ui_yummy_midi_muted() : ui_yummy_audio_muted();
        setLed(i, m ? (streamFlashOn ? LED_VOICE_OVERVIEW : 0) : LED_VOICE_OVERVIEW);
        continue;
      }
      if (i >= NUM_STREAMS) { setLed(i, 0); continue; }
      if (streamMuted[i]) {
        setLed(i, streamFlashOn ? LED_VOICE_OVERVIEW : 0);
      } else {
        setLed(i, state.slots[activeSlot[i]].hasContent ? LED_VOICE_OVERVIEW : 0);
      }
    }
    setBankLed(0);
    return;
  }

  if (muteHeld) {
    bool muteFlashOn = (now % MUTE_FLASH_PERIOD_MS) < (MUTE_FLASH_PERIOD_MS / 2);
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t voice = noteBankWindow[currentStream] * 16 + i;
      if (mutedVoice[activeSlot[currentStream]][voice]) {
        setLed(i, muteFlashOn ? LED_VOICE_OVERVIEW : 0);
      } else {
        setLed(i, voiceHasContent(voice) ? LED_VOICE_OVERVIEW : 0);
      }
    }
    setBankLed(noteBankWindow[currentStream] ? LED_MODE_ON : 0);
    return;
  }

  // v26.10: chain mode: the stream's slots. Bright = in the new chain, dim =
  // has content, the one playing dips; the pad just added flashes.
  if (chainEditing) {
    bool slotDip = (now % SLOT_BLINK_PERIOD_MS) < SLOT_BLINK_DIP_MS;
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t gs = slotForPad(i);
      if (streamOfSlot(gs) != chainEditStream) { setLed(i, 0); continue; }
      bool inChain = false;
      for (uint8_t k = 0; k < chainEditLen; k++) if (chainEditList[k] == gs) inChain = true;
      uint8_t level = inChain ? LED_SLOT_ACTIVE : state.slots[gs].hasContent ? LED_SLOT_HAS_CONTENT : 0;
      if (gs == activeSlot[chainEditStream] && slotDip) level = 0;
      if ((int8_t)i == chainFlashPad && now < chainFlashUntil) level = LED_FULL;
      setLed(i, level);
    }
    setBankLed(0);
    return;
  }

  if (gridMode == MODE_NOTE) {
    if (noteHeld) {
      // Overview: which of the 16 visible sounds (current noteBankWindow[currentStream]
      // half) have anything programmed in Pattern mode at all, across
      // their full 32-step pattern. Lets you glance which sounds are
      // worth checking, then release and pick one — either arm record (double-tap Note) to
      // live-record into it, or switch to Pattern mode to edit it directly.
      for (uint8_t i = 0; i < 16; i++) {
        uint8_t voice = noteBankWindow[currentStream] * 16 + i;
        setLed(i, voiceHasContent(voice) ? LED_VOICE_OVERVIEW : 0);
      }
      setBankLed(noteBankWindow[currentStream] ? LED_MODE_ON : 0);
      return;
    }
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t voice = noteBankWindow[currentStream] * 16 + i;
      // Brightness reflects this voice's set velocity — dim for soft, bright
      // for hard — same convention as step brightness in Pattern mode, so
      // turning the knob while holding a pad gives immediate visual feedback.
      uint8_t brightness = (voice == lastSelectedVoice[currentStream])
        ? map(voiceVelocity[voice], 1, 127, LED_VEL_MIN, LED_VEL_MAX)
        : 0;
      if (mutedVoice[activeSlot[currentStream]][voice]) brightness = LED_VOICE_MUTED;
      // v26.5: a pad held in note repeat stays lit and goes full on each hit,
      // so the repeat is visible even on a voice that is not the selected one.
      if (repeatVoiceForPad[i] >= 0) brightness = LED_VEL_MAX;
      if (voiceFlashUntil[currentStream][voice] > now &&
          (voice == lastSelectedVoice[currentStream] || repeatVoiceForPad[i] >= 0)) {
        brightness = LED_FULL;
      }
      // 1.2.0: record armed and playing: the playhead runs through the grid
      // (pad 1 brighter at the start of each bar) so you can see where the bar
      // starts. Only on pads that are otherwise dark: sounds stay readable.
      if (brightness == 0 && recordArmed && isPlayingNow() && lastTriggeredStep % 16 == i)
        brightness = (i == 0) ? LED_PLAYHEAD_BAR : LED_PLAYHEAD;
      setLed(i, brightness);
    }
    setBankLed(noteBankWindow[currentStream] ? LED_MODE_ON : 0);
  } else {
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t globalStep = currentStepBank[currentStream] * 16 + i;
      uint8_t vel = state.slots[activeSlot[currentStream]].steps[lastSelectedVoice[currentStream]][globalStep];
      // LED intensity reflects velocity: dim for a soft hit, bright for a
      // hard one, off entirely for an empty step. Headroom is left below
      // 255 so the momentary playback flash is still visibly brighter even
      // for an already-max-velocity step. (No longer shows a dim "ghost"
      // indicator for other voices' steps here — reverted per request,
      // too much visual noise; see the Note-held overview below instead.)
      uint8_t brightness = (vel > 0) ? map(vel, 1, 127, LED_VEL_MIN, LED_VEL_MAX) : 0;
      if (stepFlashUntil[currentStream][globalStep] > now) brightness = LED_FULL;
      setLed(i, brightness);
    }

    // Bank LED: solid while viewing the upper half (you're already looking
    // at it, so no need to flag it further); off while viewing the lower
    // half if the upper half is empty; a gentle low-level flash while
    // viewing the lower half if the upper half actually has something in
    // it, as a "there's more over here" nudge to go check Bank.
    if (currentStepBank[currentStream] == 1) {
      setBankLed(LED_MODE_ON);
    } else if (bankHasContent(1)) {
      setBankLed(pulse ? LED_BANK_NUDGE_HI : LED_BANK_NUDGE_LO);
    } else {
      setBankLed(0);
    }
  }
}

// True if any voice has a programmed step anywhere in the given 16-step
// bank (0 = steps 1-16, 1 = steps 17-32) of the active pattern slot.
bool bankHasContent(uint8_t bank) {
  uint8_t start = bank * 16;
  for (uint8_t v = 0; v < NUM_VOICES; v++) {
    for (uint8_t s = start; s < start + 16; s++) {
      if (state.slots[activeSlot[currentStream]].steps[v][s] > 0) return true;
    }
  }
  return false;
}

// True if this voice has any programmed step anywhere across its full
// 32-step pattern in the active slot (both banks) — used by the Note-held
// overview to show which sounds have something programmed at all.
bool voiceHasContent(uint8_t voice) {
  for (uint8_t s = 0; s < MAX_STEPS; s++) {
    if (state.slots[activeSlot[currentStream]].steps[voice][s] > 0) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// SAVE / LOAD — LittleFS in the Teensy 4.1's own flash (LittleFS_Program),
// reached through boardFsBegin/OpenWrite/OpenRead in board_config.h.
//
// Whole-state save: simple and correct; not the most flash-wear-optimal
// approach, but the data is tiny (~16KB) so this is a reasonable trade-off
// for a hobbyist build. The filesystem's size is fixed at
// compile time by Tools > Flash Size, not by anything in this file.
// ---------------------------------------------------------------------------
const char *SAVE_PATH = "/seq.dat";

void markDirty() {
  persist_activity();
  dirty = true;
  lastEditMillis = millis();
}

void maybeAutosave() {

}

void forceSave() {
  dirty=false; // The combined journal detects and saves live state when stopped.
}

// v26.6: true if `state` holds a plausible SEQ4 save. Used when moving the
// save file from the old file system, whose blocks may have been partly
// overwritten: LittleFS checks its metadata but not file contents, so a
// damaged file still reads back at the right size.
bool stateLooksValid() {
  if (state.magic != SAVE_MAGIC || state.version != SAVE_VERSION) return false;
  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    const PatternSlot &p = state.slots[i];
    if (p.shuffle > 75 || p.gatePercent > GATE_PERCENT_MAX || p.noteBankWindow > 1)
      return false;
    for (uint8_t v = 0; v < NUM_VOICES; v++)
      for (uint8_t st = 0; st < MAX_STEPS; st++)
        if (p.steps[v][st] > 127) return false;
  }
  for (uint8_t s = 0; s < NUM_STREAMS; s++) {
    if (state.lastActiveSlot[s] < STREAM_BASE[s] ||
        state.lastActiveSlot[s] >= STREAM_BASE[s] + STREAM_SLOTS[s]) return false;
    if (state.streamChannel[s] < 1 || state.streamChannel[s] > 16) return false;
  }
  return true;
}

// v26.8: pull every per-pattern value back into range after a load. Saves
// written by 26.7 on the Step Mother can hold out-of-range swing, gate length
// and velocities (the knob bug above readKnob()); a velocity over 127 is not
// even a valid MIDI data byte. Clamping fixes those patterns in place instead
// of discarding them.
void sanitizeLoadedState() {
  uint16_t fixed = 0;
  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    PatternSlot &p = state.slots[i];
    if (p.shuffle < 50 || p.shuffle > 75) { p.shuffle = p.shuffle > 75 ? 75 : 50; fixed++; }
    if (p.gatePercent < GATE_PERCENT_MIN || p.gatePercent > GATE_PERCENT_MAX) {
      p.gatePercent = p.gatePercent > GATE_PERCENT_MAX ? GATE_PERCENT_MAX : GATE_PERCENT_DEFAULT;
      fixed++;
    }
    if (p.noteBankWindow > 1) { p.noteBankWindow = 0; fixed++; }
    for (uint8_t v = 0; v < NUM_VOICES; v++)
      for (uint8_t st = 0; st < MAX_STEPS; st++)
        if (p.steps[v][st] > 127) { p.steps[v][st] = 127; fixed++; }
    recalcHasContent(i);
  }
  for (uint8_t s = 0; s < NUM_STREAMS; s++) {
    if (state.lastActiveSlot[s] < STREAM_BASE[s] ||
        state.lastActiveSlot[s] >= STREAM_BASE[s] + STREAM_SLOTS[s]) {
      state.lastActiveSlot[s] = STREAM_BASE[s];
      fixed++;
    }
    if (state.streamChannel[s] < 1 || state.streamChannel[s] > 16) {
      state.streamChannel[s] = STREAM_DEFAULT_CHANNEL[s];
      fixed++;
    }
  }
  if (fixed) {
    Serial.print("SAVE FILE: "); Serial.print(fixed);
    Serial.println(" out-of-range value(s) clamped");
    markDirty();
  }
}

void loadStateFromFlash() {
  initDefaultState();
}

void initDefaultState() {
  memset(&state, 0, sizeof(SavedState));
  state.magic = SAVE_MAGIC;
  state.version = SAVE_VERSION;
  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    state.slots[i].shuffle = 50; // straight, no swing, by default
    state.slots[i].gatePercent = GATE_PERCENT_DEFAULT;
    state.slots[i].noteBankWindow = 0;
  }
  // v26: each stream starts on the first slot it owns, on its own channel.
  for (uint8_t s = 0; s < NUM_STREAMS; s++) {
    state.lastActiveSlot[s] = STREAM_BASE[s];
    state.streamChannel[s] = STREAM_DEFAULT_CHANNEL[s];
  }
  forceSave();
}

} // namespace bro
extern "C" {
void bro_init(void) { bro::setup(); }
void bro_task(void) { bro::loop(); }
void bro_key(int key, bool down) { bro::onKeyEvent((int8_t)key,down); }
void bro_system(uint8_t status) {
 switch(status) {
 case 0xF8: bro::onMidiClock();break;
 case 0xFA: bro::onMidiStart();break;
 case 0xFB: bro::onMidiContinue();break;
 case 0xFC: bro::onMidiStop();break;
 }
}
void bro_suspend_panel(void) {
 // Discard physical holds without firing edit-on-release or bank actions.
 bro::densityRearm=true; // 0.3.3: the knob was used on the other level meanwhile
 for (int p=0;p<16;p++) {
  if (bro::repeatVoiceForPad[p]>=0) bro::handleNoteRepeatPad(p,false);
  bro::padHeld[p]=false;bro::padKnobMoved[p]=false;
 }
 for (int v=0;v<32;v++) {
  if (bro::voiceHeldLive[0][v]) bro::sendVoiceNoteOff(0,v);
  bro::voiceHeldLive[0][v]=false;
 }
 bro::heldNoteVoice=-1;
 bro::shiftHeld=bro::muteHeld=bro::clearHeld=bro::patternHeld=bro::playHeld=false;
 bro::bankHeld=bro::noteHeld=false;bro::bankConsumed=false;
 bro::recordArmed=false;bro::lastNoteTapMillis=bro::lastPatternTapMillis=0;
 bro::patternSlotPicked=bro::patternUsedInCombo=false;
 bro::patternEngagedDuringClearHold=false;bro::armedCopySource=-1;
 if (bro::chainEditing) bro::finishChainEdit();
}
void bro_show(void) { bro::refreshLeds(); }
}

extern "C" {
static_assert(sizeof(bro::SavedState)==16456,"Update combined save layout for changed StepBro state");
uint32_t bro_store_size(void){return sizeof(bro::SavedState);}
void bro_store_read(uint32_t offset,void *dst,uint32_t len){
 if(offset+len<=sizeof(bro::state))memcpy(dst,(const uint8_t *)&bro::state+offset,len);
}
bool bro_store_validate(const void *src,uint32_t len){
 if(len!=sizeof(bro::SavedState))return false;
 const bro::SavedState *d=(const bro::SavedState *)src;
 if(d->magic!=bro::SAVE_MAGIC||d->version!=bro::SAVE_VERSION||d->lastActiveSlot[0]>=16||d->streamChannel[0]<1||d->streamChannel[0]>16)return false;
 for(int i=0;i<16;i++){
  const bro::PatternSlot *p=&d->slots[i];
  if(p->shuffle<50||p->shuffle>75||p->gatePercent<5||p->gatePercent>100||p->noteBankWindow>1)return false;
  if(*((const uint8_t *)p+offsetof(bro::PatternSlot,hasContent))>1)return false;
  for(int v=0;v<32;v++)for(int st=0;st<32;st++)if(p->steps[v][st]>127)return false;
 }
 return true;
}
bool bro_store_apply(const void *src,uint32_t len){
 if(!bro_store_validate(src,len))return false;
 memcpy(&bro::state,src,len);
 bro::activeSlot[0]=bro::state.lastActiveSlot[0];
 bro::noteBankWindow[0]=bro::state.slots[bro::activeSlot[0]].noteBankWindow;
 bro::currentStepBank[0]=0;bro::recalcLoopLengthFor(0);return true;
}
bool bro_save_ready(void){
 if(bro::dawPlaying)return false;
 for(int v=0;v<32;v++)if(bro::voiceHeldLive[0][v]||bro::voiceNoteOffAt[0][v])return false;
 return true;
}
// Diagnostics: bit 1 = the DAW transport runs, bit 2 = a note is still held / sounding.
uint8_t bro_save_blockers(void){
 uint8_t b=bro::dawPlaying?2:0;
 for(int v=0;v<32;v++)if(bro::voiceHeldLive[0][v]||bro::voiceNoteOffAt[0][v])b|=4;
 return b;
}
uint8_t bro_lead_get(void){return bro::midiLeadMs;}
void bro_lead_set(uint8_t n){if(n<=16)bro::midiLeadMs=n;}
}
