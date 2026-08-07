# ShiftFeedback Design

ShiftFeedback is a monophonic MIDI instrument. MIDI note numbers choose deterministic feedback structure and seed variation; they do not tune the sound to 12-TET pitch.

The audio engine uses:

- deterministic noise excitation
- short stereo delay feedback lines
- approximate all-pass quadrature frequency shifting
- fixed-Hz positive/negative sideband motion
- cross-feedback with band limiting
- DC blocking and bounded nonlinear limiting

The plugin wrapper owns parameter smoothing, preset names, state serialization, MIDI event handling, and editor creation. The DSP engine remains allocation-free on the render path and is testable without YUP.
