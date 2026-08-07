#pragma once

#include <yup_audio_processors/yup_audio_processors.h>
#include <yup_gui/yup_gui.h>

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
    ParameterGridEditor (yup::AudioProcessor& processor,
                         yup::StringRef title,
                         yup::StringRef warning,
                         std::uint32_t accentColor);

    bool isResizable() const override;
    bool shouldPreserveAspectRatio() const override;
    yup::Size<int> getPreferredSize() const override;
    void paint (yup::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;

    yup::String title;
    yup::String warning;
    std::uint32_t accentColor = 0xffff3300u;
    std::unique_ptr<yup::Label> titleLabel;
    std::unique_ptr<yup::Label> warningLabel;
    std::vector<yup::AudioParameter::Ptr> parameters;
    std::vector<std::unique_ptr<yup::Label>> labels;
    std::vector<std::unique_ptr<yup::Slider>> sliders;
    std::vector<std::unique_ptr<yup::Label>> valueLabels;
};

} // namespace violent::plugin
