#include "violent/ShiftFeedbackEngine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <vector>

using violent::ShiftFeedbackEngine;
using violent::ShiftFeedbackParameters;
using violent::StereoFrame;

namespace
{

std::vector<StereoFrame> render (std::uint32_t seed, ShiftFeedbackParameters params, int note, int samples)
{
    ShiftFeedbackEngine engine;
    engine.prepare (48000.0);
    engine.setParameters (params);
    engine.reset (seed);
    engine.noteOn (note, 0.9f);

    std::vector<StereoFrame> output;
    output.reserve (static_cast<std::size_t> (samples));
    for (int i = 0; i < samples; ++i)
        output.push_back (engine.processSample());

    return output;
}

float rmsLeft (const std::vector<StereoFrame>& frames, int begin, int end)
{
    double sum = 0.0;
    const auto safeBegin = std::clamp (begin, 0, static_cast<int> (frames.size()));
    const auto safeEnd = std::clamp (end, safeBegin, static_cast<int> (frames.size()));
    for (int i = safeBegin; i < safeEnd; ++i)
        sum += static_cast<double> (frames[static_cast<std::size_t> (i)].left) * frames[static_cast<std::size_t> (i)].left;

    return safeEnd > safeBegin ? static_cast<float> (std::sqrt (sum / static_cast<double> (safeEnd - safeBegin))) : 0.0f;
}

float meanMid (const std::vector<StereoFrame>& frames, int begin)
{
    double sum = 0.0;
    int count = 0;
    for (std::size_t i = static_cast<std::size_t> (std::max (0, begin)); i < frames.size(); ++i)
    {
        sum += 0.5 * (static_cast<double> (frames[i].left) + frames[i].right);
        ++count;
    }
    return count > 0 ? static_cast<float> (sum / static_cast<double> (count)) : 0.0f;
}

int signChangesLeft (const std::vector<StereoFrame>& frames)
{
    int changes = 0;
    auto previous = frames.front().left;
    for (std::size_t i = 1; i < frames.size(); ++i)
    {
        const auto current = frames[i].left;
        if ((previous < 0.0f && current >= 0.0f) || (previous >= 0.0f && current < 0.0f))
            ++changes;
        previous = current;
    }
    return changes;
}

bool framesExactlyEqual (const std::vector<StereoFrame>& first, const std::vector<StereoFrame>& second)
{
    if (first.size() != second.size())
        return false;

    for (std::size_t i = 0; i < first.size(); ++i)
    {
        if (first[i].left != second[i].left || first[i].right != second[i].right)
            return false;
    }
    return true;
}

float normalizedLeftCorrelation (const std::vector<StereoFrame>& first, const std::vector<StereoFrame>& second, int begin)
{
    double dot = 0.0;
    double firstEnergy = 0.0;
    double secondEnergy = 0.0;
    const auto start = static_cast<std::size_t> (std::max (0, begin));
    const auto count = std::min (first.size(), second.size());
    for (std::size_t i = start; i < count; ++i)
    {
        dot += static_cast<double> (first[i].left) * second[i].left;
        firstEnergy += static_cast<double> (first[i].left) * first[i].left;
        secondEnergy += static_cast<double> (second[i].left) * second[i].left;
    }

    if (firstEnergy <= 0.0 || secondEnergy <= 0.0)
        return 1.0f;
    return static_cast<float> (dot / std::sqrt (firstEnergy * secondEnergy));
}

void testDeterministicTrigger()
{
    ShiftFeedbackParameters params;
    params.shiftHz = 211.0f;
    params.feedback = 0.81f;

    assert (framesExactlyEqual (render (1234u, params, 48, 4096), render (1234u, params, 48, 4096)));
}

void testSilenceUntilTriggered()
{
    ShiftFeedbackEngine engine;
    engine.prepare (48000.0);
    engine.reset (77u);

    for (int i = 0; i < 2048; ++i)
    {
        const auto frame = engine.processSample();
        assert (frame.left == 0.0f);
        assert (frame.right == 0.0f);
    }

    engine.noteOn (60, 1.0f);
    float peak = 0.0f;
    for (int i = 0; i < 2048; ++i)
    {
        const auto frame = engine.processSample();
        peak = std::max ({ peak, std::fabs (frame.left), std::fabs (frame.right) });
    }
    assert (peak > 0.01f);
}

void testShiftParameterChangesOutputStructure()
{
    ShiftFeedbackParameters slow;
    slow.shiftHz = 0.0f;
    slow.feedback = 0.76f;

    auto fast = slow;
    fast.shiftHz = 517.0f;

    const auto a = render (0xabcdu, slow, 57, 8192);
    const auto b = render (0xabcdu, fast, 57, 8192);

    double absoluteDifference = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i)
        absoluteDifference += std::fabs (a[i].left - b[i].left);

    assert (absoluteDifference > 35.0);
    assert (std::fabs (normalizedLeftCorrelation (a, b, 1024)) < 0.25f);
}

void testShiftPolarityChangesSidebandDirection()
{
    ShiftFeedbackParameters positive;
    positive.shiftHz = 211.0f;
    positive.feedback = 0.82f;
    auto negative = positive;
    negative.shiftHz = -211.0f;

    const auto up = render (0x55aau, positive, 57, 8192);
    const auto down = render (0x55aau, negative, 57, 8192);
    assert (! framesExactlyEqual (up, down));
    assert (std::fabs (normalizedLeftCorrelation (up, down, 1024)) < 0.95f);
}

void testFeedbackDecaysBelowUnity()
{
    ShiftFeedbackParameters params;
    params.feedback = 0.68f;
    params.decaySeconds = 0.035f;
    params.excitation = 1.0f;
    params.outputGain = 0.7f;

    const auto frames = render (91u, params, 36, 48000);
    const auto early = rmsLeft (frames, 128, 4096);
    const auto late = rmsLeft (frames, 36000, 48000);

    assert (early > 0.01f);
    assert (late < early * 0.04f);
    assert (late < 0.0025f);
}

void testFiniteBoundedExtremeAndNonfiniteParameters()
{
    ShiftFeedbackParameters params;
    params.shiftHz = std::numeric_limits<float>::infinity();
    params.feedback = std::numeric_limits<float>::quiet_NaN();
    params.decaySeconds = -std::numeric_limits<float>::infinity();
    params.bandCenterHz = std::numeric_limits<float>::quiet_NaN();
    params.bandWidth = std::numeric_limits<float>::infinity();
    params.excitation = 99.0f;
    params.stereo = std::numeric_limits<float>::quiet_NaN();
    params.outputGain = 99.0f;

    ShiftFeedbackEngine engine;
    engine.prepare (std::numeric_limits<double>::infinity());
    engine.setParameters (params);
    engine.reset (0u);
    engine.noteOn (999, std::numeric_limits<float>::infinity());

    for (int i = 0; i < 24000; ++i)
    {
        const auto frame = engine.processSample();
        assert (std::isfinite (frame.left));
        assert (std::isfinite (frame.right));
        assert (frame.left >= -0.9501f && frame.left <= 0.9501f);
        assert (frame.right >= -0.9501f && frame.right <= 0.9501f);
    }
}

void testLowDcAfterFeedback()
{
    ShiftFeedbackParameters params;
    params.feedback = 0.9f;
    params.decaySeconds = 0.7f;
    params.shiftHz = 311.0f;
    params.bandCenterHz = 900.0f;

    const auto frames = render (20260808u, params, 64, 96000);
    assert (std::fabs (meanMid (frames, 24000)) < 0.0035f);
}

void testNoteSelectsStructureNotTetPitch()
{
    ShiftFeedbackParameters params;
    params.shiftHz = 173.0f;
    params.feedback = 0.78f;

    const auto low = render (44u, params, 48, 8192);
    const auto octave = render (44u, params, 60, 8192);

    double difference = 0.0;
    for (std::size_t i = 0; i < low.size(); ++i)
        difference += std::fabs (low[i].left - octave[i].left);

    assert (difference > 20.0);

    const auto lowChanges = signChangesLeft (low);
    const auto octaveChanges = signChangesLeft (octave);
    const auto ratio = static_cast<float> (octaveChanges) / static_cast<float> (std::max (1, lowChanges));
    assert (ratio > 0.55f && ratio < 1.55f);
}

} // namespace

int main()
{
    testDeterministicTrigger();
    testSilenceUntilTriggered();
    testShiftParameterChangesOutputStructure();
    testShiftPolarityChangesSidebandDirection();
    testFeedbackDecaysBelowUnity();
    testFiniteBoundedExtremeAndNonfiniteParameters();
    testLowDcAfterFeedback();
    testNoteSelectsStructureNotTetPitch();

    std::cout << "ShiftFeedbackEngineTests passed\n";
    return 0;
}
