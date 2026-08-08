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

    void triggerStandaloneNote() noexcept;
    void setStandaloneGateHeld (bool shouldBeHeld) noexcept;
    [[nodiscard]] float getOutputPeak() const noexcept;

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

    enum NoteOwner
    {
        noNoteOwner,
        midiNoteOwner,
        standaloneNoteOwner
    };

    void advanceParameterHandles (int samplePosition) noexcept;
    void processStandaloneTriggerCommands() noexcept;
    void advanceStandaloneSourcesAfterSample() noexcept;
    void syncStandaloneNoteState() noexcept;
    [[nodiscard]] bool hasActiveStandaloneSource() const noexcept;
    void updateEngineParameters() noexcept;
    void updateEngineParametersIfNeeded() noexcept;
    void resetEngine() noexcept;

    std::array<yup::AudioParameter::Ptr, parameterCount> parameters;
    std::array<yup::AudioParameterHandle, parameterCount> parameterHandles;
    std::array<float, parameterCount> smoothedValues {};
    violent::ShiftFeedbackEngine engine;

    int lastNote = -1;
    int standaloneButtonPulseSamplesRemaining = 0;
    int standaloneSpaceTapSamplesRemaining = 0;
    int coefficientUpdateCountdown = 0;
    NoteOwner noteOwner = noNoteOwner;
    std::atomic<bool> resetPending { false };
    std::atomic<bool> standaloneGateRequested { false };
    std::atomic<unsigned int> standaloneTriggerRequests { 0u };
    std::atomic<unsigned int> standaloneGateOnRequests { 0u };
    std::atomic<unsigned int> standaloneGateOffRequests { 0u };
    std::atomic<unsigned int> outputPeakQuantized { 0u };
    std::atomic<int> currentPreset { 0 };
    bool standaloneSpaceGateActive = false;
    std::array<yup::String, 4> presetNames {
        "Salted Glass",
        "Arc Hive",
        "Reverse Halo",
        "Fault Choir"
    };
};

} // namespace violent::plugin
