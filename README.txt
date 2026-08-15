PFS V1.18 FINAL CONSOLIDATED

Merged/validated from:
- current working PFS files (2026-08-15): FUAP + fit-to-window behavior
- PFS V1.16: global transpose + Web MIDI clock IN
- PFS V1.17: NMIDI v3 MASTER/FOLLOW

Included features:
- 2 probabilistic fractal tracks
- FUAP mode
- Ratchet / fade / routing
- Global transpose: -12, -1, +1, +12 semitones
- Web MIDI clock input
- NMIDI v3: OFF/LOCAL, MASTER, FOLLOW
- NMIDI and Web MIDI input are mutually exclusive
- Current fit-to-window behavior retained

Audio files expected in ./audio/:
  sound1.ogg ... sound5.ogg
  fuap.ogg

Notes:
- V1.17 archive had lost the V1.16 transpose HTML/CSS/JS and contained stray closing markup in the clock panel. V1.18 restores/fixes those.
- This package contains code only; keep/use your existing audio folder alongside these files.
