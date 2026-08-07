#pragma once

#include "violent/ShiftFeedbackEngine.h"

#include <yup_audio_processors/yup_audio_processors.h>

#include <array>
#include <atomic>

namespace violent::plugin
{

class ShiftFeedbackPlugin final : public yup::AudioProcessor
{
public:
    ShiftFeedbackPlugin();

    void prepareToPlay (const yup::AudioSpec& spec) override;
    void releaseResources() override;
    void processBlock (yup::AudioProcessContext<float>& context) override;
    void flush() override;

    bool acceptsMidi() const noexcept override;
    int getNumVoices() const override;

    int getCurrentPreset() const noexcept override;
    void setCurrentPreset (int index) noexcept override;
    int getNumPresets() const override;
    yup::String getPresetName (int index) const override;
    void setPresetName (int index, yup::StringRef newName) override;

    yup::Result loadStateFromMemory (const yup::MemoryBlock& data) override;
    yup::Result saveStateIntoMemory (yup::MemoryBlock& data) override;

    bool hasEditor() const override;
    yup::AudioProcessorEditor* createEditor() override;

private:
    enum ParameterIndex
    {
        shiftHz,
        feedback,
        decaySeconds,
        bandCenterHz,
        bandWidth,
        excitation,
        stereo,
        outputGain,
        parameterCount
    };

    void advanceParameterHandles (int samplePosition) noexcept;
    void updateEngineParameters() noexcept;
    void updateEngineParametersIfNeeded() noexcept;
    void resetEngine() noexcept;

    std::array<yup::AudioParameter::Ptr, parameterCount> parameters;
    std::array<yup::AudioParameterHandle, parameterCount> parameterHandles;
    std::array<float, parameterCount> smoothedValues {};
    violent::ShiftFeedbackEngine engine;

    int lastNote = -1;
    int coefficientUpdateCountdown = 0;
    std::atomic<bool> resetPending { false };
    std::atomic<int> currentPreset { 0 };
    std::array<yup::String, 4> presetNames {
        "Salted Glass",
        "Arc Hive",
        "Reverse Halo",
        "Fault Choir"
    };
};

} // namespace violent::plugin
