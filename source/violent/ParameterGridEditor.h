#pragma once

#include <yup_audio_processors/yup_audio_processors.h>
#include <yup_gui/yup_gui.h>

#include <functional>
#include <memory>
#include <vector>

namespace violent::plugin
{

/** Reusable parameter-grid shell; product DSP and parameter semantics stay processor-owned. */
class ParameterGridEditor final
    : public yup::AudioProcessorEditor
    , private yup::Timer
{
public:
    struct StandaloneControls
    {
        std::function<void()> triggerNote;
        std::function<void (bool)> setGateHeld;
        std::function<float()> getOutputPeak;
    };

    ParameterGridEditor (yup::AudioProcessor& processor,
                         yup::StringRef title,
                         yup::StringRef warning,
                         std::uint32_t accentColor,
                         StandaloneControls standaloneControls = {});
    ~ParameterGridEditor() override;

    bool isResizable() const override;
    bool shouldPreserveAspectRatio() const override;
    yup::Size<int> getPreferredSize() const override;
    void paint (yup::Graphics& graphics) override;
    void resized() override;
    void keyDown (const yup::KeyPress& key, const yup::Point<float>& position) override;
    void keyUp (const yup::KeyPress& key, const yup::Point<float>& position) override;
    void focusLost() override;

private:
    void timerCallback() override;
    void setSpaceGateHeld (bool shouldBeHeld);

    yup::String title;
    yup::String warning;
    std::uint32_t accentColor = 0xffff3300u;
    StandaloneControls standaloneControls;
    std::unique_ptr<yup::Label> titleLabel;
    std::unique_ptr<yup::Label> warningLabel;
    std::unique_ptr<yup::TextButton> triggerButton;
    yup::Rectangle<float> meterBounds;
    std::vector<yup::AudioParameter::Ptr> parameters;
    std::vector<std::unique_ptr<yup::Label>> labels;
    std::vector<std::unique_ptr<yup::Slider>> sliders;
    std::vector<std::unique_ptr<yup::Label>> valueLabels;
    float outputPeak = 0.0f;
    bool spaceGateHeld = false;
};

} // namespace violent::plugin
