#include "violent/ParameterGridEditor.h"

#include <ehl/yup_plugin_ui/EhlPluginTheme.h>

#include <algorithm>
#include <utility>

namespace violent::plugin
{

ParameterGridEditor::ParameterGridEditor (yup::AudioProcessor& processor,
                                          yup::StringRef newTitle,
                                          yup::StringRef newWarning,
                                          std::uint32_t newAccentColor,
                                          StandaloneControls newStandaloneControls)
    : title (newTitle)
    , warning (newWarning)
    , standaloneControls (std::move (newStandaloneControls))
{
    (void) newAccentColor;
    setWantsKeyboardFocus (true);

    const auto processorParameters = processor.getParameters();
    parameters.assign (processorParameters.begin(), processorParameters.end());

    titleLabel = std::make_unique<yup::Label>();
    titleLabel->setText (title, yup::dontSendNotification);
    titleLabel->setJustification (yup::Justification::centerLeft);
    ehl::ui::styleLabel (*titleLabel, ehl::ui::TextRole::primary);
    addAndMakeVisible (*titleLabel);

    warningLabel = std::make_unique<yup::Label>();
    warningLabel->setText (warning, yup::dontSendNotification);
    warningLabel->setJustification (yup::Justification::centerLeft);
    ehl::ui::styleLabel (*warningLabel, ehl::ui::TextRole::secondary);
    addAndMakeVisible (*warningLabel);

    if (standaloneControls.triggerNote || standaloneControls.setGateHeld || standaloneControls.getOutputPeak)
    {
        triggerButton = std::make_unique<ehl::ui::CommandButton>();
        triggerButton->setButtonText ("Trigger");
        triggerButton->setClickingGrabFocus (false);
        triggerButton->setMouseCursor (yup::MouseCursor::Hand);
        triggerButton->onClick = [this]
        {
            if (standaloneControls.triggerNote)
                standaloneControls.triggerNote();
            takeKeyboardFocus();
        };
        addAndMakeVisible (*triggerButton);

        outputMeter = std::make_unique<ehl::ui::StripMeter> (ehl::ui::paper);
        addAndMakeVisible (*outputMeter);
    }

    labels.reserve (parameters.size());
    sliders.reserve (parameters.size());
    valueLabels.reserve (parameters.size());

    for (const auto& parameter : parameters)
    {
        auto label = std::make_unique<yup::Label>();
        label->setText (parameter->getName(), yup::dontSendNotification);
        label->setJustification (yup::Justification::center);
        ehl::ui::styleLabel (*label, ehl::ui::TextRole::secondary);
        addAndMakeVisible (*label);
        labels.push_back (std::move (label));

        auto slider = std::make_unique<ehl::ui::PixelSlider> (yup::Slider::RotaryVerticalDrag);
        slider->setClickingGrabFocus (false);
        slider->setRange (parameter->getMinimumValue(),
                          parameter->getMaximumValue(),
                          parameter->isStepped() ? 1.0 : 0.0);
        slider->setDefaultValue (parameter->getDefaultValue());
        slider->setValue (parameter->getValue(), yup::dontSendNotification);
        slider->setTextBoxStyle (yup::Slider::NoTextBox);
        slider->setPopupDisplayEnabled (false);
        slider->setMouseCursor (yup::MouseCursor::Hand);
        slider->onDragStart = [this, parameter] (const yup::MouseEvent&)
        {
            takeKeyboardFocus();
            parameter->beginChangeGesture();
        };
        slider->onValueChanged = [parameter] (double value)
        {
            parameter->setValueNotifyingHost (static_cast<float> (value));
        };
        slider->onDragEnd = [this, parameter] (const yup::MouseEvent&)
        {
            takeKeyboardFocus();
            parameter->endChangeGesture();
        };
        addAndMakeVisible (*slider);
        sliders.push_back (std::move (slider));

        auto valueLabel = std::make_unique<yup::Label>();
        valueLabel->setText (parameter->toString(), yup::dontSendNotification);
        valueLabel->setJustification (yup::Justification::center);
        ehl::ui::styleLabel (*valueLabel, ehl::ui::TextRole::primary);
        addAndMakeVisible (*valueLabel);
        valueLabels.push_back (std::move (valueLabel));
    }

    setSize (getPreferredSize().to<float>());
    startTimerHz (30);
}

ParameterGridEditor::~ParameterGridEditor()
{
    setSpaceGateHeld (false);
}

bool ParameterGridEditor::isResizable() const
{
    return true;
}

bool ParameterGridEditor::shouldPreserveAspectRatio() const
{
    return true;
}

yup::Size<int> ParameterGridEditor::getPreferredSize() const
{
    return ehl::ui::preferredSize;
}

void ParameterGridEditor::paint (yup::Graphics& graphics)
{
    ehl::ui::paintEditorBackground (graphics, getWidth(), getHeight());
}

void ParameterGridEditor::resized()
{
    constexpr int columns = 7;
    constexpr float margin = 16.0f;
    constexpr float top = 128.0f;
    constexpr float gap = 8.0f;
    constexpr float labelHeight = 24.0f;
    constexpr float valueHeight = 24.0f;
    constexpr float controlSize = 72.0f;

    const auto bounds = getLocalBounds();
    const auto cellWidth = (bounds.getWidth() - 2.0f * margin - gap * (columns - 1)) / columns;
    const auto rows = std::max (1, static_cast<int> ((sliders.size() + columns - 1) / columns));
    const auto availableHeight = bounds.getHeight() - top - margin;
    const auto cellHeight = (availableHeight - gap * (rows - 1)) / rows;
    const auto labelInset = rows > 1 ? 4.0f : 12.0f;
    const auto valueInset = rows > 1 ? 4.0f : 12.0f;

    titleLabel->setBounds (20.0f, 8.0f, bounds.getWidth() - 40.0f, 28.0f);
    warningLabel->setBounds (20.0f, 36.0f, bounds.getWidth() - 40.0f, 20.0f);
    if (triggerButton != nullptr && outputMeter != nullptr)
    {
        triggerButton->setBounds (margin, 72.0f, 104.0f, 28.0f);
        outputMeter->setBounds (margin + 112.0f,
                                76.0f,
                                std::max (90.0f, bounds.getWidth() - margin - 112.0f - margin),
                                12.0f);
    }

    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        const auto column = static_cast<int> (i) % columns;
        const auto row = static_cast<int> (i) / columns;
        const auto x = margin + column * (cellWidth + gap);
        const auto y = top + row * (cellHeight + gap);
        const auto labelY = y + labelInset;
        const auto valueY = y + cellHeight - valueHeight - valueInset;
        const auto controlTop = labelY + labelHeight;
        const auto controlBottom = valueY;
        const auto fittedControlSize = std::min ({ controlSize, cellWidth - 8.0f, std::max (20.0f, controlBottom - controlTop) });
        const auto controlX = x + 0.5f * (cellWidth - fittedControlSize);
        const auto controlY = controlTop + 0.5f * (controlBottom - controlTop - fittedControlSize);

        labels[i]->setBounds (x + 2.0f, labelY, cellWidth - 4.0f, labelHeight);
        sliders[i]->setBounds (controlX, controlY, fittedControlSize, fittedControlSize);
        valueLabels[i]->setBounds (x + 2.0f, valueY, cellWidth - 4.0f, valueHeight);
    }
}

void ParameterGridEditor::keyDown (const yup::KeyPress& key, const yup::Point<float>& position)
{
    if (key.getKey() == yup::KeyPress::spaceKey)
    {
        setSpaceGateHeld (true);
        return;
    }

    yup::AudioProcessorEditor::keyDown (key, position);
}

void ParameterGridEditor::keyUp (const yup::KeyPress& key, const yup::Point<float>& position)
{
    if (key.getKey() == yup::KeyPress::spaceKey)
    {
        setSpaceGateHeld (false);
        return;
    }

    yup::AudioProcessorEditor::keyUp (key, position);
}

void ParameterGridEditor::focusLost()
{
    setSpaceGateHeld (false);
}

void ParameterGridEditor::timerCallback()
{
    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        if (! sliders[i]->isCurrentlyBeingDragged())
            sliders[i]->setValue (parameters[i]->getValue(), yup::dontSendNotification);
        valueLabels[i]->setText (parameters[i]->toString(), yup::dontSendNotification);
    }

    if (standaloneControls.getOutputPeak)
    {
        outputPeak = std::max (standaloneControls.getOutputPeak(), outputPeak * 0.78f);
        if (outputMeter != nullptr)
            outputMeter->setLevel (outputPeak);
    }
}

void ParameterGridEditor::setSpaceGateHeld (bool shouldBeHeld)
{
    if (spaceGateHeld == shouldBeHeld)
        return;

    spaceGateHeld = shouldBeHeld;
    if (standaloneControls.setGateHeld)
        standaloneControls.setGateHeld (shouldBeHeld);
}

} // namespace violent::plugin
