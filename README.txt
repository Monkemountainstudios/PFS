PFS - Probabilistic Fractal Sequencer V1.13

PFS — a two-track, five-level probabilistic sample sequencer derived from PFD.

AUDIO
Place five samples in the audio folder: Samples included in package.
  sound1.ogg
  sound2.ogg
  sound3.ogg
  sound4.ogg
  sound5.ogg

For predictable note names, tune each source sample to C4 before exporting.
Both tracks share these five sounds and can select them independently.
If a sample is missing, PFS uses a simple synthesized fallback tone.

NODE CONTROLS
- Click a note node: ON/OFF.
- Mouse wheel over a node: move note up/down one semitone.
- Note range: C2 to C6.
- STATIC: always follows the left-most path.
- VARIATION: chooses a new left/right branch at each fork.
- Rate button above each tree cycles: 1 -> 1/2 -> 1/4.

RATCHET
- PROB.: chance that any active node on a routed track ratchets.
- REPEATS: 2, 3 or 4 subdivisions.
- FADE: successive ratchet hits fade in level.
- ROUTE 1/2: choose which tracks are eligible for ratchets.

MIXER
- FILTER: low-pass filter.
- GATE: note length as a percentage of the step.
- VOL / PAN / REV: channel level, stereo position and shared reverb send.
- MUTE: audio mute only; sequencing continues.

TIMING
Tempo and swing are global. Hidden tracks with active notes blink on the master beat.

V0.3 fixes:
- Tree roots and track buttons now share the same centres.
- Reverb return matches the stronger PFD-style send behaviour.
- Sample selectors have explicit button behaviour.
- Missing sample files now use five distinct fallback voices, so slots 1-5 are audibly testable.


V0.4 visual polish:
- Sequencer trees shifted left for clear separation from mixer.
- Full-height divider between sequencer and utility/mixer area.
- Colored Gate/Vol/Pan/Rev slider rails with hardware-style grey caps.
- Stronger indicator lines on Filter and Ratchet knobs.

V1.11 Bugfixes
- Auto resizing to fit window
- Added colour to sliders
- Oriented layout of buttons and graphics
- Minor value changes to Reverb


V1.13
- Includes the V1.12 output limiter/headroom panning fix.
- Added large red FUAP! global source override.
- Add audio/fuap.ogg. When active, both tracks use FUAP while retaining their own notes, filters, gates, pan, reverb and ratchets.
