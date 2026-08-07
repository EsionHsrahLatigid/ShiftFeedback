#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>

namespace violent
{

/** A stereo sample produced by one of the violent DSP engines. */
struct StereoFrame
{
    float left = 0.0f;
    float right = 0.0f;
};

/** Returns a finite value constrained to the supplied interval. */
inline float clampFinite (float value, float low, float high, float fallback) noexcept
{
    if (! std::isfinite (value))
        value = fallback;
    return std::clamp (value, low, high);
}

/** Converts a finite decibel value to a linear gain. */
inline float decibelsToGain (float decibels) noexcept
{
    return std::pow (10.0f, clampFinite (decibels, -120.0f, 24.0f, -120.0f) / 20.0f);
}

/** Cheap bounded nonlinearity used only as a final realtime safety stage. */
inline float boundedDrive (float input, float drive = 1.0f) noexcept
{
    const auto safeInput = std::isfinite (input) ? input : 0.0f;
    const auto safeDrive = clampFinite (drive, 0.0f, 32.0f, 1.0f);
    return std::tanh (safeInput * safeDrive);
}

/** Small deterministic generator for repeatable realtime-safe testable noise. */
class DeterministicNoise
{
public:
    void reset (std::uint32_t seed) noexcept
    {
        state = seed != 0u ? seed : 0x6d2b79f5u;
    }

    [[nodiscard]] std::uint32_t nextWord() noexcept
    {
        auto value = state;
        value ^= value << 13;
        value ^= value >> 17;
        value ^= value << 5;
        state = value != 0u ? value : 0x6d2b79f5u;
        return state;
    }

    [[nodiscard]] float nextFloat() noexcept
    {
        constexpr auto scale = 1.0 / 2147483648.0;
        return static_cast<float> (static_cast<double> (nextWord()) * scale - 1.0);
    }

    [[nodiscard]] float nextBinary() noexcept
    {
        return (nextWord() & 1u) != 0u ? 1.0f : -1.0f;
    }

private:
    std::uint32_t state = 0x6d2b79f5u;
};

/** Monophonic attack/decay gate with sample-continuous parameter changes. */
class TriggerEnvelope
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = std::isfinite (newSampleRate) && newSampleRate > 1.0 ? newSampleRate : 44100.0;
        updateRates();
        reset();
    }

    void reset() noexcept
    {
        value = 0.0f;
        velocity = 0.0f;
        stage = Stage::idle;
    }

    void setTimes (float attackSeconds, float decaySeconds) noexcept
    {
        attack = clampFinite (attackSeconds, 0.0001f, 1.0f, 0.002f);
        decay = clampFinite (decaySeconds, 0.001f, 30.0f, 1.0f);
        updateRates();
    }

    void trigger (float newVelocity) noexcept
    {
        velocity = clampFinite (newVelocity, 0.0f, 1.0f, 1.0f);
        stage = velocity > 0.0f ? Stage::attack : Stage::idle;
        if (stage == Stage::idle)
            value = 0.0f;
    }

    void release() noexcept
    {
        if (stage != Stage::idle)
            stage = Stage::decay;
    }

    [[nodiscard]] float process() noexcept
    {
        if (stage == Stage::attack)
        {
            value += attackStep;
            if (value >= 1.0f)
            {
                value = 1.0f;
                stage = Stage::decay;
            }
        }
        else if (stage == Stage::decay)
        {
            value *= decayCoefficient;
            if (value < 1.0e-6f)
            {
                value = 0.0f;
                stage = Stage::idle;
            }
        }

        return value * velocity;
    }

    [[nodiscard]] bool isActive() const noexcept { return stage != Stage::idle; }

private:
    enum class Stage
    {
        idle,
        attack,
        decay
    };

    void updateRates() noexcept
    {
        attackStep = 1.0f / static_cast<float> (std::max (1.0, sampleRate * attack));
        decayCoefficient = std::exp (-6.90775527898f / static_cast<float> (std::max (1.0, sampleRate * decay)));
    }

    double sampleRate = 44100.0;
    float attack = 0.002f;
    float decay = 1.0f;
    float attackStep = 1.0f;
    float decayCoefficient = 0.999f;
    float value = 0.0f;
    float velocity = 0.0f;
    Stage stage = Stage::idle;
};

/** Direct-form biquad with RBJ low-pass, high-pass, and band-pass designs. */
class Biquad
{
public:
    void reset() noexcept
    {
        z1 = 0.0f;
        z2 = 0.0f;
    }

    void setLowPass (double sampleRate, float frequency, float quality = 0.70710678f) noexcept
    {
        setCoefficients (sampleRate, frequency, quality, Mode::lowPass);
    }

    void setHighPass (double sampleRate, float frequency, float quality = 0.70710678f) noexcept
    {
        setCoefficients (sampleRate, frequency, quality, Mode::highPass);
    }

    void setBandPass (double sampleRate, float frequency, float quality = 1.0f) noexcept
    {
        setCoefficients (sampleRate, frequency, quality, Mode::bandPass);
    }

    [[nodiscard]] float process (float input) noexcept
    {
        const auto safeInput = std::isfinite (input) ? input : 0.0f;
        const auto output = b0 * safeInput + z1;
        z1 = b1 * safeInput - a1 * output + z2;
        z2 = b2 * safeInput - a2 * output;
        return std::isfinite (output) ? output : 0.0f;
    }

private:
    enum class Mode
    {
        lowPass,
        highPass,
        bandPass
    };

    void setCoefficients (double newSampleRate, float frequency, float quality, Mode mode) noexcept
    {
        const auto safeRate = std::isfinite (newSampleRate) && newSampleRate >= 8000.0 ? newSampleRate : 44100.0;
        const auto nyquist = static_cast<float> (safeRate * 0.5);
        const auto safeFrequency = clampFinite (frequency, 1.0f, nyquist * 0.98f, 1000.0f);
        const auto safeQuality = clampFinite (quality, 0.1f, 24.0f, 0.70710678f);
        const auto omega = 2.0f * std::numbers::pi_v<float> * safeFrequency / static_cast<float> (safeRate);
        const auto sine = std::sin (omega);
        const auto cosine = std::cos (omega);
        const auto alpha = sine / (2.0f * safeQuality);
        const auto inverseA0 = 1.0f / (1.0f + alpha);

        if (mode == Mode::lowPass)
        {
            b0 = 0.5f * (1.0f - cosine) * inverseA0;
            b1 = (1.0f - cosine) * inverseA0;
            b2 = b0;
        }
        else if (mode == Mode::highPass)
        {
            b0 = 0.5f * (1.0f + cosine) * inverseA0;
            b1 = -(1.0f + cosine) * inverseA0;
            b2 = b0;
        }
        else
        {
            b0 = alpha * inverseA0;
            b1 = 0.0f;
            b2 = -b0;
        }

        a1 = -2.0f * cosine * inverseA0;
        a2 = (1.0f - alpha) * inverseA0;
    }

    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float z1 = 0.0f;
    float z2 = 0.0f;
};

/** One-pole DC blocker for feedback and asymmetric-noise paths. */
class DcBlocker
{
public:
    void prepare (double sampleRate, float cutoffHz = 5.0f) noexcept
    {
        const auto safeRate = std::isfinite (sampleRate) && sampleRate > 1.0 ? sampleRate : 44100.0;
        const auto safeCutoff = clampFinite (cutoffHz, 1.0f, 40.0f, 5.0f);
        coefficient = std::exp (-2.0f * std::numbers::pi_v<float> * safeCutoff / static_cast<float> (safeRate));
        reset();
    }

    void reset() noexcept
    {
        previousInput = 0.0f;
        previousOutput = 0.0f;
    }

    [[nodiscard]] float process (float input) noexcept
    {
        const auto safeInput = std::isfinite (input) ? input : 0.0f;
        const auto output = safeInput - previousInput + coefficient * previousOutput;
        previousInput = safeInput;
        previousOutput = std::isfinite (output) ? output : 0.0f;
        return previousOutput;
    }

private:
    float coefficient = 0.999f;
    float previousInput = 0.0f;
    float previousOutput = 0.0f;
};

} // namespace violent
