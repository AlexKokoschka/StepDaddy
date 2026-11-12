# Combined save compatibility

## Saving and compatibility

The previous combined record layout is retained: StepBro 16,456 bytes, Yummy
4,280 bytes, four metadata bytes; total 20,740 bytes. The runtime voice parameter
struct gains Drive, but a dedicated stable disk struct keeps all old field offsets.
No original pattern, automation or MIDI-lead fields are moved.

Metadata is now: MIDI lead, extension tag 0xD3, low/high bytes of normalized
16-bit Drive. Older records have three zero reserved bytes and load Drive at
EXACTLY 50%. New Drive saves use 65,536 levels (finer than the panel knob).
A 50% saved value reloads within 0.000008 of that setting due to quantization.
There is no manual migration step. The next stopped-only save writes the extension.

Actual old v0.2.3 adapters/journal were used to generate a legacy fixture and
then tested reading a new Drive-tagged record. Both directions passed. Old
firmware ignores Drive and can still load patterns/settings. If the old firmware
saves again, it drops the Drive extension; the new build then defaults Drive to
50%. Unknown extension tags are rejected before changing either engine.
Compatibility applies to the combined merged save format, not standalone saves.

