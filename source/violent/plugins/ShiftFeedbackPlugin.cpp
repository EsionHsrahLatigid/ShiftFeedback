#include "violent/plugins/ShiftFeedbackPlugin.h"

#include "violent/ParameterGridEditor.h"
#include "violent/ProductState.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace violent::plugin
{

namespace
{
constexpr std::array<char, 4> stateMagic { 'S', 'F', 'B', '1' };
constexpr int stateVersion = 1;
constexpr int coefficientUpdateCadenceSamples = 16;
constexpr int numShiftFeedbackParameters = 8;

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

    for (int sample = 0; sample < numSamples; ++sample)
    {
        while (midi != midiEnd && (*midi).samplePosition <= sample)
        {
            const auto& message = (*midi).getMessage();
            if (message.isNoteOn())
            {
                lastNote = std::clamp (message.getNoteNumber(), 0, 127);
                engine.noteOn (lastNote, message.getVelocity() * (1.0f / 127.0f));
            }
            else if (message.isNoteOff())
            {
                const auto note = std::clamp (message.getNoteNumber(), 0, 127);
                if (note == lastNote)
                {
                    engine.noteOff (note);
                    lastNote = -1;
                }
            }

            ++midi;
        }

        advanceParameterHandles (sample);
        updateEngineParametersIfNeeded();
        const auto frame = engine.processSample();

        if (left != nullptr)
            left[sample] = frame.left;
        if (right != nullptr)
            right[sample] = frame.right;

        for (int channel = 2; channel < numChannels; ++channel)
            audio.getWritePointer (channel)[sample] = 0.0f;
    }

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
    return new ParameterGridEditor (*this,
                                    "ShiftFeedback",
                                    "Warning: self-oscillating feedback network. Keep monitoring level conservative.",
                                    0xff44d7b6u);
}

void ShiftFeedbackPlugin::advanceParameterHandles (int samplePosition) noexcept
{
    for (std::size_t i = 0; i < parameterHandles.size(); ++i)
    {
        parameterHandles[i].advanceToSample (samplePosition);
        smoothedValues[i] = parameterHandles[i].getNextValue();
    }
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
    coefficientUpdateCountdown = 0;
}

} // namespace violent::plugin

extern "C" yup::AudioProcessor* createPluginProcessor()
{
    return new violent::plugin::ShiftFeedbackPlugin();
}
