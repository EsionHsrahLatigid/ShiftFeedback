# ShiftFeedback Design

ShiftFeedback is a monophonic MIDI instrument. MIDI note numbers choose deterministic feedback structure and seed variation; they do not tune the sound to 12-TET pitch.

The audio engine uses:

- deterministic noise excitation
- short stereo delay feedback lines
- approximate all-pass quadrature frequency shifting
- fixed-Hz positive/negative sideband motion
- cross-feedback with band limiting
- DC blocking and bounded nonlinear limiting

The plugin wrapper owns parameter smoothing, preset names, state serialization, MIDI event handling, standalone UI trigger commands, output metering, and editor creation. External MIDI remains the normal note source.

Standalone trigger support is processor-owned: the editor writes only atomics for a visible trigger pulse and counted space-key gate edges, and the audio thread consumes those atomics to call the existing monophonic engine trigger path. Button pulse, Space tap, and Space held-gate ownership are tracked separately so one source release cannot cancel another. External MIDI owns the monophonic note while active; when MIDI releases, any still-requested standalone gate is restarted. The render loop does not allocate or lock. The output meter is telemetry only; the audio thread stores one scalar peak per block and the editor polls it from its UI timer.

The DSP engine remains allocation-free on the render path and is testable without YUP.
