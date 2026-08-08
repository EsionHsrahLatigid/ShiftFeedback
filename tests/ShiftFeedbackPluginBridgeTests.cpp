#include "violent/plugins/ShiftFeedbackPlugin.h"

#include <yup_audio_processors/yup_audio_processors.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>

namespace
{

struct ProcessorHarness
{
    ProcessorHarness()
    {
        processor.prepareToPlay (yup::AudioSpec (48000.0f, 512, 2));
        params.reserve (16);
    }

    float process()
    {
        audio.clear();
        yup::AudioProcessContext<float> context { audio, midi, params, nullptr, {}, {} };
        processor.processBlock (context);
        params.clear();
        return audio.getMagnitude (0, audio.getNumSamples());
    }

    violent::plugin::ShiftFeedbackPlugin processor;
    yup::AudioBuffer<float> audio { 2, 512 };
    yup::MidiBuffer midi;
    yup::ParameterChangeBuffer params;
};

void testStandaloneTriggerProducesWaveform()
{
    ProcessorHarness harness;
    harness.processor.triggerStandaloneNote();

    const auto peak = harness.process();

    assert (peak > 0.001f);
    assert (harness.processor.getOutputPeak() > 0.001f);
}

void testRapidStandaloneGateTapProducesWaveform()
{
    ProcessorHarness harness;
    harness.processor.setStandaloneGateHeld (true);
    harness.processor.setStandaloneGateHeld (false);

    const auto peak = harness.process();

    assert (peak > 0.001f);
    assert (harness.processor.getOutputPeak() > 0.001f);
}

void testMidiOverlapHandsBackToHeldStandaloneGate()
{
    ProcessorHarness harness;
    harness.processor.setStandaloneGateHeld (true);
    harness.midi.addEvent (yup::MidiMessage::noteOn (1, 72, 1.0f), 0);
    harness.midi.addEvent (yup::MidiMessage::noteOff (1, 72), 64);

    const auto overlapPeak = harness.process();
    const auto heldPeak = harness.process();

    assert (overlapPeak > 0.001f);
    assert (heldPeak > 0.001f);

    harness.processor.setStandaloneGateHeld (false);
    const auto releasePeak = harness.process();
    assert (releasePeak >= 0.0f);
}

void testSpaceReleaseDoesNotCancelButtonPulse()
{
    ProcessorHarness harness;
    harness.processor.triggerStandaloneNote();
    harness.processor.setStandaloneGateHeld (true);
    assert (harness.process() > 0.001f);

    harness.processor.setStandaloneGateHeld (false);
    assert (harness.process() > 0.001f);
}

void testFlushAndPrepareKeepHeldStandaloneGate()
{
    ProcessorHarness harness;
    harness.processor.setStandaloneGateHeld (true);
    assert (harness.process() > 0.001f);

    harness.processor.flush();
    assert (harness.process() > 0.001f);

    harness.processor.prepareToPlay (yup::AudioSpec (48000.0f, 512, 2));
    assert (harness.process() > 0.001f);

    harness.processor.setStandaloneGateHeld (false);
    for (int i = 0; i < 256; ++i)
        harness.process();

    assert (harness.processor.getOutputPeak() < 0.05f);
}

} // namespace

int main()
{
    testStandaloneTriggerProducesWaveform();
    testRapidStandaloneGateTapProducesWaveform();
    testMidiOverlapHandsBackToHeldStandaloneGate();
    testSpaceReleaseDoesNotCancelButtonPulse();
    testFlushAndPrepareKeepHeldStandaloneGate();
    std::cout << "ShiftFeedbackPluginBridgeTests passed\n";
    return 0;
}
