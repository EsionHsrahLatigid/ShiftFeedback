#pragma once

#include "ViolentDspPrimitives.h"

#include <array>
#include <cstdint>

namespace violent
{

/** Parameters for the monophonic ShiftFeedback MIDI instrument.

    All values are sanitized by ShiftFeedbackEngine::setParameters().
    shiftHz is a fixed frequency offset in Hertz, not a pitch ratio. MIDI note
    numbers select deterministic seed/feedback structure only; they do not
    tune the cloud to 12-TET pitch.
*/
struct ShiftFeedbackParameters
{
    float shiftHz = 73.0f;       ///< Fixed frequency shift in Hz, clamped to [-4000, 4000].
    float feedback = 0.72f;      ///< Cross-feedback gain, clamped below unity.
    float decaySeconds = 1.25f;  ///< Excitation envelope decay time in seconds.
    float bandCenterHz = 1200.0f;///< Feedback band-pass center in Hz.
    float bandWidth = 1.2f;      ///< Approximate band-pass Q control.
    float excitation = 0.85f;    ///< Noise burst amount.
    float stereo = 0.75f;        ///< Stereo delay/phase divergence.
    float outputGain = 0.55f;    ///< Final post-ceiling drive gain.
};

/** Stable non-harmonic frequency-shifted cross-feedback cloud.

    The engine is a pure C++20, allocation-free render path for the
    ShiftFeedback product contract. It uses a short deterministic noise burst,
    an approximate all-pass quadrature pair, fixed-Hz ring/SSB-inspired
    frequency shifting, cross-feedback delays, feedback band limiting, DC
    blockers, sample-wise loop limiting, and a final ceiling.

    The quadrature stage is intentionally approximate rather than a full FFT or
    FIR Hilbert transformer. It is sufficient for deterministic non-harmonic
    sideband motion and stable feedback tests, but it is not a measurement-grade
    single-sideband shifter.
*/
class ShiftFeedbackEngine
{
public:
    ShiftFeedbackEngine();

    /** Sets sample rate, recomputes filters, and clears all state. */
    void prepare (double sampleRate) noexcept;

    /** Clears delay/filter/envelope state and selects the base deterministic seed. */
    void reset (std::uint32_t seed = 1u) noexcept;

    /** Applies sanitized parameters without allocating or changing current MIDI state. */
    void setParameters (const ShiftFeedbackParameters& parameters) noexcept;

    /** Retriggers the monophonic noise excitation; note chooses seed/structure, not pitch. */
    void noteOn (int midiNoteNumber, float velocity) noexcept;

    /** Releases the current monophonic excitation when the matching note ends. */
    void noteOff (int midiNoteNumber) noexcept;

    /** Renders one stereo sample. Silent until noteOn() supplies excitation. */
    [[nodiscard]] StereoFrame processSample() noexcept;

    /** Renders numSamples into non-null stereo buffers. */
    void process (float* left, float* right, int numSamples) noexcept;

private:
    static constexpr int maxDelaySamples = 8192;
    static constexpr int delayMask = maxDelaySamples - 1;

    struct AllPass
    {
        void setCoefficient (float newCoefficient) noexcept;
        void reset() noexcept;
        [[nodiscard]] float process (float input) noexcept;

        float coefficient = 0.0f;
        float state = 0.0f;
    };

    struct QuadratureShifter
    {
        void prepare (double sampleRate) noexcept;
        void reset (float phaseOffset) noexcept;
        void setShiftHz (float shiftHz) noexcept;
        [[nodiscard]] float process (float input, bool upperSideband) noexcept;

        std::array<AllPass, 4> inPhase {};
        std::array<AllPass, 4> quadrature {};
        double sampleRate = 44100.0;
        float phase = 0.0f;
        float phaseIncrement = 0.0f;
    };

    struct ClampedParameters
    {
        float shiftHz = 73.0f;
        float feedback = 0.72f;
        float decaySeconds = 1.25f;
        float bandCenterHz = 1200.0f;
        float bandWidth = 1.2f;
        float excitation = 0.85f;
        float stereo = 0.75f;
        float outputGain = 0.55f;
    };

    static std::uint32_t mixSeed (std::uint32_t value) noexcept;
    static float sanitize (float value, float low, float high, float fallback) noexcept;

    void updateFilters() noexcept;
    void clearDelay() noexcept;
    void configureStructureForNote (int midiNoteNumber) noexcept;
    [[nodiscard]] float nextExcitation (float gate, float polarity) noexcept;
    [[nodiscard]] float processLoopSample (float input, float crossFeedback, bool upperSideband, int channel) noexcept;

    ClampedParameters params;
    double sampleRate = 44100.0;
    std::uint32_t baseSeed = 1u;
    int activeNote = -1;
    int leftDelaySamples = 1579;
    int rightDelaySamples = 2137;
    int writeIndex = 0;

    DeterministicNoise noise;
    TriggerEnvelope envelope;
    QuadratureShifter leftShifter;
    QuadratureShifter rightShifter;
    Biquad leftBandPass;
    Biquad rightBandPass;
    Biquad excitationLowPass;
    DcBlocker leftDcBlocker;
    DcBlocker rightDcBlocker;

    std::array<float, maxDelaySamples> leftDelay {};
    std::array<float, maxDelaySamples> rightDelay {};
};

} // namespace violent
