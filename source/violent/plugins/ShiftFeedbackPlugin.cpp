#include "violent/plugins/ShiftFeedbackPlugin.h"

#if ! SHIFTFEEDBACK_HEADLESS_TEST
#include "violent/ParameterGridEditor.h"
#endif
#include "violent/ProductState.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <utility>

namespace violent::plugin
{

namespace
{
constexpr std::array<char, 4> stateMagic { 'S', 'F', 'B', '1' };
constexpr int stateVersion = 1;
constexpr int coefficientUpdateCadenceSamples = 16;
constexpr int numShiftFeedbackParameters = 8;
constexpr int standaloneTriggerNote = 60;
constexpr float standaloneTriggerVelocity = 1.0f;
constexpr float standalonePulseSeconds = 0.08f;
constexpr float outputPeakScale = 1000000.0f;

static_assert (std::atomic<bool>::is_always_lock_free);
static_assert (std::atomic<unsigned int>::is_always_lock_free);

using Unit = yup::AudioParameter::ParameterUnit;

yup::String valueWithSuffix (float value, int decimals, const char* suffix)
{
    return yup::String (value, decimals) + suffix;
}

yup::String percentString (float value)
{
    return yup::String (std::lround (value * 100.0f)) + "%";
}

yup::NormalisableRange<float> makeHzRange (float low, float high, float centre)
{
    auto range = yup::NormalisableRange<float> (low, high);
    range.setSkewForCentre (centre);
    return range;
}

constexpr std::array<std::array<float, numShiftFeedbackParameters>, 4> presetValues {{
    {{ 73.0f, 0.72f, 1.25f, 1200.0f, 1.2f, 0.85f, 0.75f, 0.55f }},
    {{ 212.0f, 0.83f, 2.4f, 2100.0f, 0.85f, 1.05f, 0.92f, 0.48f }},
    {{ -137.0f, 0.68f, 3.8f, 640.0f, 2.6f, 0.72f, 0.58f, 0.62f }},
    {{ 889.0f, 0.91f, 0.65f, 4200.0f, 3.7f, 1.22f, 0.84f, 0.38f }}
}};
} // namespace

ShiftFeedbackPlugin::ShiftFeedbackPlugin()
    : yup::AudioProcessor ("ShiftFeedback",
                           yup::AudioBusLayout ({
                                                     yup::AudioBus ("midi", yup::AudioBus::Midi, yup::AudioBus::Input, 0),
                                                 },
                                                 {
                                                     yup::AudioBus ("main", yup::AudioBus::Audio, yup::AudioBus::Output, 2),
                                                 }))
{
    parameters[shiftHz] = yup::AudioParameterBuilder()
                              .withID ("shift_hz")
                              .withName ("Shift")
                              .withHostID (shiftHz)
                              .withRange (-4000.0f, 4000.0f)
                              .withDefault (73.0f)
                              .withSmoothing (12.0f)
                              .withModulatable (true)
                              .withUnit (Unit::Hertz)
                              .withValueToString ([] (float value) { return valueWithSuffix (value, 1, " Hz"); })
                              .build();
    parameters[feedback] = yup::AudioParameterBuilder()
                               .withID ("feedback")
                               .withName ("Feedback")
                               .withHostID (feedback)
                               .withRange (0.0f, 0.985f)
                               .withDefault (0.72f)
                               .withSmoothing (18.0f)
                               .withModulatable (true)
                               .withUnit (Unit::Ratio)
                               .withValueToString ([] (float value) { return valueWithSuffix (value, 3, ""); })
                               .build();
    parameters[decaySeconds] = yup::AudioParameterBuilder()
                                   .withID ("decay_seconds")
                                   .withName ("Decay")
                                   .withHostID (decaySeconds)
                                   .withRange (0.01f, 12.0f)
                                   .withDefault (1.25f)
                                   .withSmoothing (35.0f)
                                   .withModulatable (true)
                                   .withUnit (Unit::Seconds)
                                   .withValueToString ([] (float value) { return valueWithSuffix (value, 2, " s"); })
                                   .build();
    parameters[bandCenterHz] = yup::AudioParameterBuilder()
                                   .withID ("band_center_hz")
                                   .withName ("Band Center")
                                   .withHostID (bandCenterHz)
                                   .withRange (makeHzRange (40.0f, 16000.0f, 1200.0f))
                                   .withDefault (1200.0f)
                                   .withSmoothing (20.0f)
                                   .withModulatable (true)
                                   .withUnit (Unit::Hertz)
                                   .withValueToString ([] (float value) { return valueWithSuffix (value, 0, " Hz"); })
                                   .build();
    parameters[bandWidth] = yup::AudioParameterBuilder()
                                .withID ("band_width")
                                .withName ("Band Width")
                                .withHostID (bandWidth)
                                .withRange (0.2f, 8.0f)
                                .withDefault (1.2f)
                                .withSmoothing (20.0f)
                                .withModulatable (true)
                                .withUnit (Unit::Custom, "Q")
                                .withValueToString ([] (float value) { return valueWithSuffix (value, 2, " Q"); })
                                .build();
    parameters[excitation] = yup::AudioParameterBuilder()
                                 .withID ("excitation")
                                 .withName ("Excitation")
                                 .withHostID (excitation)
                                 .withRange (0.0f, 1.5f)
                                 .withDefault (0.85f)
                                 .withSmoothing (10.0f)
                                 .withModulatable (true)
                                 .withUnit (Unit::LinearGain)
                                 .withValueToString ([] (float value) { return valueWithSuffix (value, 2, ""); })
                                 .build();
    parameters[stereo] = yup::AudioParameterBuilder()
                             .withID ("stereo")
                             .withName ("Stereo")
                             .withHostID (stereo)
                             .withRange (0.0f, 1.0f)
                             .withDefault (0.75f)
                             .withSmoothing (20.0f)
                             .withModulatable (true)
                             .withUnit (Unit::Percent)
                             .withValueToString ([] (float value) { return percentString (value); })
                             .build();
    parameters[outputGain] = yup::AudioParameterBuilder()
                                 .withID ("output_gain")
                                 .withName ("Output Gain")
                                 .withHostID (outputGain)
                                 .withRange (0.0f, 2.0f)
                                 .withDefault (0.55f)
                                 .withSmoothing (18.0f)
                                 .withModulatable (true)
                                 .withUnit (Unit::LinearGain)
                                 .withValueToString ([] (float value) { return valueWithSuffix (value, 2, ""); })
                                 .build();

    for (const auto& parameter : parameters)
        addParameter (parameter);
}

void ShiftFeedbackPlugin::prepareToPlay (const yup::AudioSpec& spec)
{
    engine.prepare (spec.sampleRate);

    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
    {
        parameterHandles[i] = yup::AudioParameterHandle (*parameters[i], spec.sampleRate);
        smoothedValues[i] = parameterHandles[i].getCurrentValue();
    }

    coefficientUpdateCountdown = 0;
    lastNote = -1;
    noteOwner = noNoteOwner;
    standaloneSpaceGateActive = standaloneGateRequested.load (std::memory_order_acquire);
    updateEngineParameters();
}

void ShiftFeedbackPlugin::releaseResources()
{
}

void ShiftFeedbackPlugin::processBlock (yup::AudioProcessContext<float>& context)
{
    auto& audio = context.audio;
    const auto numSamples = audio.getNumSamples();
    const auto numChannels = audio.getNumChannels();

    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
        parameterHandles[i].prepareBlock (context.params, parameters[i]->getIndexInContainer());

    if (resetPending.exchange (false, std::memory_order_acq_rel))
        resetEngine();

    auto midi = context.midi.begin();
    const auto midiEnd = context.midi.end();
    auto* left = numChannels > 0 ? audio.getWritePointer (0) : nullptr;
    auto* right = numChannels > 1 ? audio.getWritePointer (1) : nullptr;
    auto blockPeak = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        while (midi != midiEnd && (*midi).samplePosition <= sample)
        {
            const auto& message = (*midi).getMessage();
            if (message.isNoteOn())
            {
                lastNote = std::clamp (message.getNoteNumber(), 0, 127);
                noteOwner = midiNoteOwner;
                engine.noteOn (lastNote, message.getVelocity() * (1.0f / 127.0f));
            }
            else if (message.isNoteOff())
            {
                const auto note = std::clamp (message.getNoteNumber(), 0, 127);
                if (note == lastNote && noteOwner == midiNoteOwner)
                {
                    engine.noteOff (note);
                    lastNote = -1;
                    noteOwner = noNoteOwner;
                    syncStandaloneNoteState();
                }
            }

            ++midi;
        }

        processStandaloneTriggerCommands();
        advanceParameterHandles (sample);
        updateEngineParametersIfNeeded();
        const auto frame = engine.processSample();
        blockPeak = std::max ({ blockPeak, std::fabs (frame.left), std::fabs (frame.right) });

        if (left != nullptr)
            left[sample] = frame.left;
        if (right != nullptr)
            right[sample] = frame.right;

        for (int channel = 2; channel < numChannels; ++channel)
            audio.getWritePointer (channel)[sample] = 0.0f;

        advanceStandaloneSourcesAfterSample();
    }

    outputPeakQuantized.store (static_cast<unsigned int> (std::clamp (blockPeak, 0.0f, 1.0f) * outputPeakScale),
                               std::memory_order_release);
    context.midi.clear();
}

void ShiftFeedbackPlugin::flush()
{
    resetPending.store (true, std::memory_order_release);
}

bool ShiftFeedbackPlugin::acceptsMidi() const noexcept
{
    return true;
}

int ShiftFeedbackPlugin::getNumVoices() const
{
    return 1;
}

int ShiftFeedbackPlugin::getCurrentPreset() const noexcept
{
    return currentPreset.load (std::memory_order_relaxed);
}

void ShiftFeedbackPlugin::setCurrentPreset (int index) noexcept
{
    if (! yup::isPositiveAndBelow (index, static_cast<int> (presetValues.size())))
        return;

    currentPreset.store (index, std::memory_order_relaxed);
    for (std::size_t i = 0; i < parameters.size(); ++i)
        parameters[i]->setValue (presetValues[static_cast<std::size_t> (index)][i]);

    resetPending.store (true, std::memory_order_release);
}

int ShiftFeedbackPlugin::getNumPresets() const
{
    return static_cast<int> (presetNames.size());
}

yup::String ShiftFeedbackPlugin::getPresetName (int index) const
{
    if (yup::isPositiveAndBelow (index, static_cast<int> (presetNames.size())))
        return presetNames[static_cast<std::size_t> (index)];
    return "Invalid Preset";
}

void ShiftFeedbackPlugin::setPresetName (int index, yup::StringRef newName)
{
    if (yup::isPositiveAndBelow (index, static_cast<int> (presetNames.size())))
        presetNames[static_cast<std::size_t> (index)] = newName;
}

yup::Result ShiftFeedbackPlugin::loadStateFromMemory (const yup::MemoryBlock& data)
{
    auto loadedPreset = currentPreset.load (std::memory_order_relaxed);
    const auto result = loadProductState (*this, data, stateMagic, stateVersion, getNumPresets(), loadedPreset);
    if (result.wasOk())
    {
        currentPreset.store (loadedPreset, std::memory_order_relaxed);
        resetPending.store (true, std::memory_order_release);
    }
    return result;
}

yup::Result ShiftFeedbackPlugin::saveStateIntoMemory (yup::MemoryBlock& data)
{
    return saveProductState (*this, data, stateMagic, stateVersion, currentPreset.load (std::memory_order_relaxed));
}

bool ShiftFeedbackPlugin::hasEditor() const
{
    return true;
}

yup::AudioProcessorEditor* ShiftFeedbackPlugin::createEditor()
{
#if SHIFTFEEDBACK_HEADLESS_TEST
    return nullptr;
#else
    ParameterGridEditor::StandaloneControls controls;
    controls.triggerNote = [this] { triggerStandaloneNote(); };
    controls.setGateHeld = [this] (bool shouldBeHeld) { setStandaloneGateHeld (shouldBeHeld); };
    controls.getOutputPeak = [this] { return getOutputPeak(); };

    return new ParameterGridEditor (*this,
                                    "ShiftFeedback",
                                    "Warning: self-oscillating feedback network. Keep monitoring level conservative.",
                                    0xfff2f2f0u,
                                    std::move (controls));
#endif
}

void ShiftFeedbackPlugin::triggerStandaloneNote() noexcept
{
    standaloneTriggerRequests.fetch_add (1u, std::memory_order_release);
}

void ShiftFeedbackPlugin::setStandaloneGateHeld (bool shouldBeHeld) noexcept
{
    const auto wasHeld = standaloneGateRequested.exchange (shouldBeHeld, std::memory_order_acq_rel);
    if (wasHeld == shouldBeHeld)
        return;

    if (shouldBeHeld)
        standaloneGateOnRequests.fetch_add (1u, std::memory_order_release);
    else
        standaloneGateOffRequests.fetch_add (1u, std::memory_order_release);
}

float ShiftFeedbackPlugin::getOutputPeak() const noexcept
{
    return static_cast<float> (outputPeakQuantized.load (std::memory_order_acquire)) / outputPeakScale;
}

void ShiftFeedbackPlugin::advanceParameterHandles (int samplePosition) noexcept
{
    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
    {
        parameterHandles[i].advanceToSample (samplePosition);
        smoothedValues[i] = parameterHandles[i].getNextValue();
    }
}

void ShiftFeedbackPlugin::processStandaloneTriggerCommands() noexcept
{
    const auto requestedTriggers = standaloneTriggerRequests.exchange (0u, std::memory_order_acq_rel);
    if (requestedTriggers > 0u)
        standaloneButtonPulseSamplesRemaining = std::max (standaloneButtonPulseSamplesRemaining,
                                                          std::max (1, static_cast<int> (standalonePulseSeconds * static_cast<float> (getSampleRate()))));

    const auto gateOnRequests = standaloneGateOnRequests.exchange (0u, std::memory_order_acq_rel);
    const auto gateOffRequests = standaloneGateOffRequests.exchange (0u, std::memory_order_acq_rel);
    const auto gateRequested = standaloneGateRequested.load (std::memory_order_acquire);

    if (gateOnRequests > 0u && ! gateRequested)
        standaloneSpaceTapSamplesRemaining = std::max (standaloneSpaceTapSamplesRemaining,
                                                       std::max (1, static_cast<int> (standalonePulseSeconds * static_cast<float> (getSampleRate()))));

    if (gateRequested || gateOnRequests > gateOffRequests)
        standaloneSpaceGateActive = true;
    else if (gateOffRequests > 0u)
        standaloneSpaceGateActive = false;

    syncStandaloneNoteState();
}

void ShiftFeedbackPlugin::advanceStandaloneSourcesAfterSample() noexcept
{
    if (standaloneButtonPulseSamplesRemaining > 0)
        --standaloneButtonPulseSamplesRemaining;
    if (standaloneSpaceTapSamplesRemaining > 0)
        --standaloneSpaceTapSamplesRemaining;

    syncStandaloneNoteState();
}

void ShiftFeedbackPlugin::syncStandaloneNoteState() noexcept
{
    if (noteOwner == midiNoteOwner)
        return;

    if (hasActiveStandaloneSource())
    {
        if (noteOwner != standaloneNoteOwner)
        {
            engine.noteOn (standaloneTriggerNote, standaloneTriggerVelocity);
            noteOwner = standaloneNoteOwner;
        }
    }
    else if (noteOwner == standaloneNoteOwner)
    {
        engine.noteOff (standaloneTriggerNote);
        noteOwner = noNoteOwner;
    }
}

bool ShiftFeedbackPlugin::hasActiveStandaloneSource() const noexcept
{
    return standaloneButtonPulseSamplesRemaining > 0
        || standaloneSpaceTapSamplesRemaining > 0
        || standaloneSpaceGateActive;
}

void ShiftFeedbackPlugin::updateEngineParameters() noexcept
{
    violent::ShiftFeedbackParameters engineParameters;
    engineParameters.shiftHz = smoothedValues[shiftHz];
    engineParameters.feedback = smoothedValues[feedback];
    engineParameters.decaySeconds = smoothedValues[decaySeconds];
    engineParameters.bandCenterHz = smoothedValues[bandCenterHz];
    engineParameters.bandWidth = smoothedValues[bandWidth];
    engineParameters.excitation = smoothedValues[excitation];
    engineParameters.stereo = smoothedValues[stereo];
    engineParameters.outputGain = smoothedValues[outputGain];
    engine.setParameters (engineParameters);
}

void ShiftFeedbackPlugin::updateEngineParametersIfNeeded() noexcept
{
    if (coefficientUpdateCountdown > 0)
    {
        --coefficientUpdateCountdown;
        return;
    }

    updateEngineParameters();
    coefficientUpdateCountdown = coefficientUpdateCadenceSamples - 1;
}

void ShiftFeedbackPlugin::resetEngine() noexcept
{
    engine.reset (1u);
    lastNote = -1;
    standaloneButtonPulseSamplesRemaining = 0;
    standaloneSpaceTapSamplesRemaining = 0;
    coefficientUpdateCountdown = 0;
    noteOwner = noNoteOwner;
    standaloneSpaceGateActive = standaloneGateRequested.load (std::memory_order_acquire);
    outputPeakQuantized.store (0u, std::memory_order_release);
}

} // namespace violent::plugin

extern "C" yup::AudioProcessor* createPluginProcessor()
{
    return new violent::plugin::ShiftFeedbackPlugin();
}
