#include "violent/ShiftFeedbackEngine.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace violent
{

namespace
{
constexpr float finalCeiling = 0.95f;
constexpr float loopCeiling = 0.78f;
}

ShiftFeedbackEngine::ShiftFeedbackEngine()
{
    prepare (44100.0);
}

void ShiftFeedbackEngine::prepare (double newSampleRate) noexcept
{
    sampleRate = std::isfinite (newSampleRate) && newSampleRate > 1.0 ? newSampleRate : 44100.0;
    envelope.prepare (sampleRate);
    leftShifter.prepare (sampleRate);
    rightShifter.prepare (sampleRate);
    leftDcBlocker.prepare (sampleRate, 8.0f);
    rightDcBlocker.prepare (sampleRate, 8.0f);
    envelope.setTimes (0.0015f, params.decaySeconds);
    leftShifter.setShiftHz (params.shiftHz);
    rightShifter.setShiftHz (-params.shiftHz);
    updateFilters();
    reset (baseSeed);
}

void ShiftFeedbackEngine::reset (std::uint32_t seed) noexcept
{
    baseSeed = seed != 0u ? seed : 1u;
    activeNote = -1;
    writeIndex = 0;
    noise.reset (mixSeed (baseSeed ^ 0x4f1bbcdcu));
    envelope.reset();
    leftShifter.reset (0.0f);
    rightShifter.reset (0.5f * std::numbers::pi_v<float>);
    leftBandPass.reset();
    rightBandPass.reset();
    excitationLowPass.reset();
    leftDcBlocker.reset();
    rightDcBlocker.reset();
    clearDelay();
}

void ShiftFeedbackEngine::setParameters (const ShiftFeedbackParameters& parameters) noexcept
{
    params.shiftHz = sanitize (parameters.shiftHz, -4000.0f, 4000.0f, 73.0f);
    params.feedback = sanitize (parameters.feedback, 0.0f, 0.985f, 0.72f);
    params.decaySeconds = sanitize (parameters.decaySeconds, 0.01f, 12.0f, 1.25f);
    params.bandCenterHz = sanitize (parameters.bandCenterHz, 40.0f, 16000.0f, 1200.0f);
    params.bandWidth = sanitize (parameters.bandWidth, 0.2f, 8.0f, 1.2f);
    params.excitation = sanitize (parameters.excitation, 0.0f, 1.5f, 0.85f);
    params.stereo = sanitize (parameters.stereo, 0.0f, 1.0f, 0.75f);
    params.outputGain = sanitize (parameters.outputGain, 0.0f, 2.0f, 0.55f);

    envelope.setTimes (0.0015f, params.decaySeconds);
    leftShifter.setShiftHz (params.shiftHz);
    rightShifter.setShiftHz (-params.shiftHz);
    updateFilters();
}

void ShiftFeedbackEngine::noteOn (int midiNoteNumber, float velocity) noexcept
{
    activeNote = std::clamp (midiNoteNumber, 0, 127);
    configureStructureForNote (activeNote);
    noise.reset (mixSeed (baseSeed ^ static_cast<std::uint32_t> (activeNote * 0x45d9f3bu)));
    envelope.trigger (sanitize (velocity, 0.0f, 1.0f, 1.0f));
}

void ShiftFeedbackEngine::noteOff (int midiNoteNumber) noexcept
{
    if (activeNote == std::clamp (midiNoteNumber, 0, 127))
    {
        envelope.release();
        activeNote = -1;
    }
}

StereoFrame ShiftFeedbackEngine::processSample() noexcept
{
    const auto leftRead = (writeIndex - leftDelaySamples) & delayMask;
    const auto rightRead = (writeIndex - rightDelaySamples) & delayMask;
    const auto delayedLeft = leftDelay[static_cast<std::size_t> (leftRead)];
    const auto delayedRight = rightDelay[static_cast<std::size_t> (rightRead)];

    const auto gate = envelope.process();
    const auto excitation = nextExcitation (gate, 1.0f);
    const auto stereoFault = 0.15f + 0.35f * params.stereo;
    const auto leftExcitation = excitation;
    const auto rightExcitation = nextExcitation (gate, -1.0f) * stereoFault + excitation * (1.0f - stereoFault);

    const auto leftLoop = processLoopSample (leftExcitation, delayedRight * params.feedback, true, 0);
    const auto rightLoop = processLoopSample (rightExcitation, delayedLeft * params.feedback, false, 1);

    leftDelay[static_cast<std::size_t> (writeIndex)] = leftLoop;
    rightDelay[static_cast<std::size_t> (writeIndex)] = rightLoop;
    writeIndex = (writeIndex + 1) & delayMask;

    const auto leftOut = boundedDrive ((leftLoop + 0.18f * leftExcitation) * params.outputGain, 1.6f) * finalCeiling;
    const auto rightOut = boundedDrive ((rightLoop + 0.18f * rightExcitation) * params.outputGain, 1.6f) * finalCeiling;
    return { std::clamp (leftOut, -finalCeiling, finalCeiling),
             std::clamp (rightOut, -finalCeiling, finalCeiling) };
}

void ShiftFeedbackEngine::process (float* left, float* right, int numSamples) noexcept
{
    if (left == nullptr || right == nullptr || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto frame = processSample();
        left[i] = frame.left;
        right[i] = frame.right;
    }
}

void ShiftFeedbackEngine::AllPass::setCoefficient (float newCoefficient) noexcept
{
    coefficient = clampFinite (newCoefficient, -0.98f, 0.98f, 0.0f);
}

void ShiftFeedbackEngine::AllPass::reset() noexcept
{
    state = 0.0f;
}

float ShiftFeedbackEngine::AllPass::process (float input) noexcept
{
    const auto safeInput = std::isfinite (input) ? input : 0.0f;
    const auto output = -coefficient * safeInput + state;
    state = safeInput + coefficient * output;
    return std::isfinite (output) ? output : 0.0f;
}

void ShiftFeedbackEngine::QuadratureShifter::prepare (double newSampleRate) noexcept
{
    sampleRate = std::isfinite (newSampleRate) && newSampleRate > 1.0 ? newSampleRate : 44100.0;

    const std::array<float, 4> inPhaseCoefficients { 0.0417f, 0.1989f, 0.4784f, 0.7937f };
    const std::array<float, 4> quadratureCoefficients { 0.0711f, 0.2941f, 0.6236f, 0.9109f };
    for (std::size_t i = 0; i < inPhase.size(); ++i)
    {
        inPhase[i].setCoefficient (inPhaseCoefficients[i]);
        quadrature[i].setCoefficient (quadratureCoefficients[i]);
    }
}

void ShiftFeedbackEngine::QuadratureShifter::reset (float phaseOffset) noexcept
{
    phase = std::isfinite (phaseOffset) ? phaseOffset : 0.0f;
    for (auto& stage : inPhase)
        stage.reset();
    for (auto& stage : quadrature)
        stage.reset();
}

void ShiftFeedbackEngine::QuadratureShifter::setShiftHz (float shiftHz) noexcept
{
    const auto safeShift = clampFinite (shiftHz, -4000.0f, 4000.0f, 0.0f);
    phaseIncrement = 2.0f * std::numbers::pi_v<float> * safeShift / static_cast<float> (sampleRate);
}

float ShiftFeedbackEngine::QuadratureShifter::process (float input, bool upperSideband) noexcept
{
    auto inPhaseSample = input;
    auto quadratureSample = input;
    for (auto& stage : inPhase)
        inPhaseSample = stage.process (inPhaseSample);
    for (auto& stage : quadrature)
        quadratureSample = stage.process (quadratureSample);

    phase += phaseIncrement;
    if (phase >= 2.0f * std::numbers::pi_v<float>)
        phase -= 2.0f * std::numbers::pi_v<float>;
    else if (phase < 0.0f)
        phase += 2.0f * std::numbers::pi_v<float>;

    const auto sine = std::sin (phase);
    const auto cosine = std::cos (phase);
    const auto shifted = upperSideband
        ? (inPhaseSample * cosine - quadratureSample * sine)
        : (inPhaseSample * cosine + quadratureSample * sine);
    return std::isfinite (shifted) ? shifted : 0.0f;
}

std::uint32_t ShiftFeedbackEngine::mixSeed (std::uint32_t value) noexcept
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value != 0u ? value : 0x6d2b79f5u;
}

float ShiftFeedbackEngine::sanitize (float value, float low, float high, float fallback) noexcept
{
    return clampFinite (value, low, high, fallback);
}

void ShiftFeedbackEngine::updateFilters() noexcept
{
    const auto nyquist = static_cast<float> (sampleRate * 0.5);
    const auto center = std::min (params.bandCenterHz, nyquist * 0.86f);
    const auto quality = sanitize (params.bandWidth, 0.2f, 8.0f, 1.2f);
    leftBandPass.setBandPass (sampleRate, center, quality);
    rightBandPass.setBandPass (sampleRate, center * (1.0f + 0.035f * params.stereo), quality);
    excitationLowPass.setLowPass (sampleRate, std::min (nyquist * 0.42f, center * 3.5f + 1200.0f), 0.70710678f);
}

void ShiftFeedbackEngine::clearDelay() noexcept
{
    leftDelay.fill (0.0f);
    rightDelay.fill (0.0f);
}

void ShiftFeedbackEngine::configureStructureForNote (int midiNoteNumber) noexcept
{
    const auto note = static_cast<std::uint32_t> (std::clamp (midiNoteNumber, 0, 127));
    const auto state = mixSeed (baseSeed ^ (note * 0x9e3779b9u));
    const auto base = static_cast<int> (sampleRate * 0.018);
    const auto spread = static_cast<int> (sampleRate * (0.012 + 0.032 * params.stereo));
    leftDelaySamples = std::clamp (base + static_cast<int> (state % static_cast<std::uint32_t> (std::max (1, spread))),
                                   37,
                                   maxDelaySamples - 1);
    rightDelaySamples = std::clamp (base + static_cast<int> ((state >> 11) % static_cast<std::uint32_t> (std::max (1, spread + 173))),
                                    41,
                                    maxDelaySamples - 1);

    const auto phase = static_cast<float> ((state >> 20) & 0x0fffu) / 4096.0f;
    leftShifter.reset (phase * 2.0f * std::numbers::pi_v<float>);
    rightShifter.reset ((1.0f - phase) * 2.0f * std::numbers::pi_v<float>);
}

float ShiftFeedbackEngine::nextExcitation (float gate, float polarity) noexcept
{
    if (gate <= 0.0f || params.excitation <= 0.0f)
        return 0.0f;

    const auto noisy = 0.65f * noise.nextFloat() + 0.35f * noise.nextBinary() * polarity;
    const auto bandLimited = excitationLowPass.process (noisy);
    return bandLimited * gate * params.excitation;
}

float ShiftFeedbackEngine::processLoopSample (float input, float crossFeedback, bool upperSideband, int channel) noexcept
{
    const auto loopInput = std::clamp (input + crossFeedback, -1.8f, 1.8f);
    const auto shifted = channel == 0 ? leftShifter.process (loopInput, upperSideband)
                                      : rightShifter.process (loopInput, upperSideband);
    const auto bandLimited = channel == 0 ? leftBandPass.process (shifted)
                                          : rightBandPass.process (shifted);
    const auto withoutDc = channel == 0 ? leftDcBlocker.process (bandLimited)
                                        : rightDcBlocker.process (bandLimited);
    return boundedDrive (withoutDc, 1.35f) * loopCeiling;
}

} // namespace violent
