#include "violent/ParameterGridEditor.h"

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
    , accentColor (newAccentColor)
    , standaloneControls (std::move (newStandaloneControls))
{
    setWantsKeyboardFocus (true);

    const auto processorParameters = processor.getParameters();
    parameters.assign (processorParameters.begin(), processorParameters.end());

    titleLabel = std::make_unique<yup::Label>();
    titleLabel->setText (title, yup::dontSendNotification);
    titleLabel->setJustification (yup::Justification::centerLeft);
    addAndMakeVisible (*titleLabel);

    warningLabel = std::make_unique<yup::Label>();
    warningLabel->setText (warning, yup::dontSendNotification);
    warningLabel->setJustification (yup::Justification::centerLeft);
    addAndMakeVisible (*warningLabel);

    if (standaloneControls.triggerNote || standaloneControls.setGateHeld || standaloneControls.getOutputPeak)
    {
        triggerButton = std::make_unique<yup::TextButton>();
        triggerButton->setButtonText ("Trigger");
        triggerButton->setClickingGrabFocus (false);
        triggerButton->setColor (yup::TextButton::Style::backgroundColorId, yup::Color (accentColor));
        triggerButton->setColor (yup::TextButton::Style::backgroundPressedColorId, yup::Color (0xffffffffu));
        triggerButton->setColor (yup::TextButton::Style::textColorId, yup::Color (0xff08100eu));
        triggerButton->setColor (yup::TextButton::Style::textPressedColorId, yup::Color (0xff08100eu));
        triggerButton->setColor (yup::TextButton::Style::outlineColorId, yup::Color (0xff25302du));
        triggerButton->onClick = [this]
        {
            if (standaloneControls.triggerNote)
                standaloneControls.triggerNote();
            takeKeyboardFocus();
        };
        addAndMakeVisible (*triggerButton);
    }

    labels.reserve (parameters.size());
    sliders.reserve (parameters.size());
    valueLabels.reserve (parameters.size());

    for (const auto& parameter : parameters)
    {
        auto label = std::make_unique<yup::Label>();
        label->setText (parameter->getName(), yup::dontSendNotification);
        label->setJustification (yup::Justification::center);
        addAndMakeVisible (*label);
        labels.push_back (std::move (label));

        auto slider = std::make_unique<yup::Slider> (yup::Slider::RotaryVerticalDrag);
        slider->setClickingGrabFocus (false);
        slider->setRange (parameter->getMinimumValue(),
                          parameter->getMaximumValue(),
                          parameter->isStepped() ? 1.0 : 0.0);
        slider->setDefaultValue (parameter->getDefaultValue());
        slider->setValue (parameter->getValue(), yup::dontSendNotification);
        slider->setTextBoxStyle (yup::Slider::NoTextBox);
        slider->setPopupDisplayEnabled (false);
        slider->setMouseCursor (yup::MouseCursor::Hand);
        slider->onDragStart = [parameter] (const yup::MouseEvent&) { parameter->beginChangeGesture(); };
        slider->onValueChanged = [parameter] (double value)
        {
            parameter->setValueNotifyingHost (static_cast<float> (value));
        };
        slider->onDragEnd = [parameter] (const yup::MouseEvent&) { parameter->endChangeGesture(); };
        addAndMakeVisible (*slider);
        sliders.push_back (std::move (slider));

        auto valueLabel = std::make_unique<yup::Label>();
        valueLabel->setText (parameter->toString(), yup::dontSendNotification);
        valueLabel->setJustification (yup::Justification::center);
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
    return { 940, 520 };
}

void ParameterGridEditor::paint (yup::Graphics& graphics)
{
    graphics.setFillColor (0xff0a0b0du);
    graphics.fillAll();

    graphics.setFillColor (accentColor);
    graphics.fillRect (0.0f, 0.0f, getWidth(), 5.0f);

    if (triggerButton != nullptr)
    {
        const auto level = std::clamp (outputPeak, 0.0f, 1.0f);
        graphics.setFillColor (0xff151918u);
        graphics.fillRect (meterBounds);
        graphics.setFillColor (accentColor);
        graphics.fillRect (meterBounds.getX(),
                           meterBounds.getY(),
                           meterBounds.getWidth() * level,
                           meterBounds.getHeight());
        graphics.setStrokeColor (0xff25302du);
        graphics.strokeRect (meterBounds);
    }
}

void ParameterGridEditor::resized()
{
    constexpr int columns = 5;
    constexpr float margin = 20.0f;
    constexpr float top = 120.0f;
    constexpr float gap = 12.0f;
    constexpr float labelHeight = 24.0f;
    constexpr float valueHeight = 24.0f;
    constexpr float controlGap = 4.0f;

    const auto bounds = getLocalBounds();
    const auto cellWidth = (bounds.getWidth() - 2.0f * margin - gap * (columns - 1)) / columns;
    const auto rows = std::max (1, static_cast<int> ((sliders.size() + columns - 1) / columns));
    const auto availableHeight = bounds.getHeight() - top - margin;
    const auto cellHeight = (availableHeight - gap * (rows - 1)) / rows;

    titleLabel->setBounds (24.0f, 12.0f, bounds.getWidth() - 48.0f, 30.0f);
    warningLabel->setBounds (24.0f, 43.0f, bounds.getWidth() - 48.0f, 24.0f);
    if (triggerButton != nullptr)
    {
        triggerButton->setBounds (24.0f, 78.0f, 128.0f, 30.0f);
        meterBounds = yup::Rectangle<float> (168.0f, 84.0f, std::max (48.0f, bounds.getWidth() - 192.0f), 18.0f);
    }

    for (std::size_t i = 0; i < sliders.size(); ++i)
    {
        const auto column = static_cast<int> (i) % columns;
        const auto row = static_cast<int> (i) / columns;
        const auto x = margin + column * (cellWidth + gap);
        const auto y = top + row * (cellHeight + gap);
        const auto controlHeight = cellHeight - labelHeight - valueHeight - 2.0f * controlGap;
        const auto controlSize = std::max (20.0f, std::min (cellWidth - 8.0f, controlHeight));
        const auto controlX = x + 0.5f * (cellWidth - controlSize);
        const auto controlY = y + labelHeight + controlGap;

        labels[i]->setBounds (x, y, cellWidth, labelHeight);
        sliders[i]->setBounds (controlX, controlY, controlSize, controlSize);
        valueLabels[i]->setBounds (x, y + cellHeight - valueHeight, cellWidth, valueHeight);
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
        repaint (meterBounds);
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
