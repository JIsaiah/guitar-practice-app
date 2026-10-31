#include "MetalLogoTuner.h"


#include "../MetalLogo/MetalLogoGenerator.h"
#include "../../Text.h"

namespace
{
    // 生成した Path を、枠に収めて描くだけの部品
    class LogoPreview final : public juce::Component
    {
    public:
        void setLogo (juce::Path newLogo, juce::Rectangle<float> newArea, juce::Colour newColour)
        {
            logo = std::move (newLogo);
            area = newArea;
            colour = newColour;
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (Palette::deathMetal().background);
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.drawRect (getLocalBounds());

            if (logo.isEmpty() || area.isEmpty())
                return;

            g.setColour (colour);
            g.fillPath (logo, juce::RectanglePlacement (juce::RectanglePlacement::centred)
                                  .getTransformToFit (area, getLocalBounds().reduced (4).toFloat()));
        }

    private:
        juce::Path logo;
        juce::Rectangle<float> area;
        juce::Colour colour;
    };

    // パラメータ1つ分のスライダー
    struct ParamControl
    {
        const char* name;
        std::function<double (const MetalLogoParams&)> get;
        std::function<void (MetalLogoParams&, double)> set;
        double min, max, step;
        bool isInteger;
        juce::Label label;
        juce::Slider slider;
    };

    template <typename Value>
    std::unique_ptr<ParamControl> makeControl (const char* name, Value MetalLogoParams::* member,
                                               double min, double max, double step)
    {
        auto c = std::make_unique<ParamControl>();
        c->name = name;
        c->get = [member] (const MetalLogoParams& p) { return (double) (p.*member); };
        c->set = [member] (MetalLogoParams& p, double v) { p.*member = (Value) v; };
        c->min = min;
        c->max = max;
        c->step = step;
        c->isInteger = std::is_integral_v<Value>;
        return c;
    }

    juce::String formatValue (const ParamControl& c, const MetalLogoParams& p)
    {
        const auto v = c.get (p);
        if (c.isInteger)
            return juce::String ((int) v);

        auto s = juce::String (v, 3);
        while (s.endsWithChar ('0') && ! s.endsWith (".0"))
            s = s.dropLastCharacters (1);
        return s + "f";
    }

    class TunerContent final : public juce::Component
    {
    public:
        explicit TunerContent (ThemeManager& t)
            : theme (t), params (t.getLogoCache().getParams())   // 保存済みの値 (無ければ初期値) から始める
        {
            textEditor.setText ("デスメタルモード"_jp, false);
            textEditor.onTextChange = [this] { regenerate(); };
            textLabel.setText ("文字列"_jp, juce::dontSendNotification);

            seedSlider.setRange (0.0, 1000.0, 1.0);
            seedSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
            seedSlider.onValueChange = [this] { regenerate(); };
            seedLabel.setText ("seed (0=文字列から)"_jp, juce::dontSendNotification);

            for (auto* c : { &textLabel, &seedLabel })
                addAndMakeVisible (c);
            addAndMakeVisible (textEditor);
            addAndMakeVisible (seedSlider);

            controls.push_back (makeControl ("fontHeight", &MetalLogoParams::fontHeight, 32.0, 200.0, 1.0));
            controls.push_back (makeControl ("tracking", &MetalLogoParams::tracking, 0.0, 1.0, 0.01));
            controls.push_back (makeControl ("margin", &MetalLogoParams::margin, 0.0, 1.0, 0.01));
            controls.push_back (makeControl ("flattenTolerance", &MetalLogoParams::flattenTolerance, 0.1, 3.0, 0.05));
            controls.push_back (makeControl ("simplifyAngleDeg", &MetalLogoParams::simplifyAngleDeg, 0.0, 20.0, 0.5));
            controls.push_back (makeControl ("tipAngleDeg", &MetalLogoParams::tipAngleDeg, 30.0, 179.0, 1.0));
            controls.push_back (makeControl ("tipExtend", &MetalLogoParams::tipExtend, 0.0, 1.0, 0.01));
            controls.push_back (makeControl ("tipFalloffVerts", &MetalLogoParams::tipFalloffVerts, 0.0, 10.0, 1.0));
            controls.push_back (makeControl ("tipFalloffRadius", &MetalLogoParams::tipFalloffRadius, 1.0, 5.0, 0.1));
            controls.push_back (makeControl ("tipJitter", &MetalLogoParams::tipJitter, 0.0, 1.0, 0.01));
            controls.push_back (makeControl ("tipMaxStretch", &MetalLogoParams::tipMaxStretch, 0.0, 10.0, 0.1));
            controls.push_back (makeControl ("spikeBand", &MetalLogoParams::spikeBand, 0.0, 0.5, 0.01));
            controls.push_back (makeControl ("spikeLenMin", &MetalLogoParams::spikeLenMin, 0.0, 1.0, 0.01));
            controls.push_back (makeControl ("spikeLenMax", &MetalLogoParams::spikeLenMax, 0.0, 1.0, 0.01));
            controls.push_back (makeControl ("spikeBaseWidth", &MetalLogoParams::spikeBaseWidth, 0.005, 0.2, 0.005));
            controls.push_back (makeControl ("spikeMinGap", &MetalLogoParams::spikeMinGap, 0.0, 0.5, 0.01));
            controls.push_back (makeControl ("spikeProbability", &MetalLogoParams::spikeProbability, 0.0, 1.0, 0.01));
            controls.push_back (makeControl ("collisionSamples", &MetalLogoParams::collisionSamples, 1.0, 20.0, 1.0));
            controls.push_back (makeControl ("spikeStep", &MetalLogoParams::spikeStep, 0.005, 0.2, 0.005));
            controls.push_back (makeControl ("spikeMinFlatness", &MetalLogoParams::spikeMinFlatness, 0.0, 1.0, 0.01));

            for (auto& c : controls)
            {
                c->label.setText (c->name, juce::dontSendNotification);
                c->slider.setRange (c->min, c->max, c->step);
                c->slider.setValue (c->get (params), juce::dontSendNotification);
                c->slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
                c->slider.onValueChange = [this, control = c.get()]
                {
                    control->set (params, control->slider.getValue());
                    regenerate();
                };
                addAndMakeVisible (c->label);
                addAndMakeVisible (c->slider);
            }

            saveButton.setButtonText ("保存して反映"_jp);
            saveButton.setTooltip ("この値を保存し、画面のロゴを作り直します (次に起動したときも使われます)"_jp);
            saveButton.onClick = [this]
            {
                theme.setLogoParams (params);
                timeLabel.setText ("保存しました。デスメタルモードのロゴに反映しました"_jp, juce::dontSendNotification);
            };
            addAndMakeVisible (saveButton);

            copyButton.setButtonText ("パラメータをクリップボードにコピー"_jp);
            copyButton.onClick = [this] { copyToClipboard(); };
            resetButton.setButtonText ("初期値に戻す (保存するまで反映しない)"_jp);
            resetButton.onClick = [this]
            {
                params = {};
                for (auto& c : controls)
                    c->slider.setValue (c->get (params), juce::dontSendNotification);
                regenerate();
            };
            addAndMakeVisible (copyButton);
            addAndMakeVisible (resetButton);
            addAndMakeVisible (timeLabel);
            addAndMakeVisible (largePreview);
            addAndMakeVisible (smallPreview);

            setSize (1100, 800);
            regenerate();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (12);
            auto left = area.removeFromLeft (420);
            area.removeFromLeft (12);

            const auto row = [&left] (juce::Component& label, juce::Component& control)
            {
                auto r = left.removeFromTop (26);
                label.setBounds (r.removeFromLeft (150));
                control.setBounds (r);
                left.removeFromTop (3);
            };

            row (textLabel, textEditor);
            row (seedLabel, seedSlider);
            left.removeFromTop (6);
            for (auto& c : controls)
                row (c->label, c->slider);

            left.removeFromTop (8);
            saveButton.setBounds (left.removeFromTop (30));
            left.removeFromTop (6);
            copyButton.setBounds (left.removeFromTop (30));
            left.removeFromTop (6);
            resetButton.setBounds (left.removeFromTop (30));

            timeLabel.setBounds (area.removeFromBottom (24));
            area.removeFromBottom (8);
            smallPreview.setBounds (area.removeFromBottom (48));   // ボタンの中の大きさ (40px 前後) での見え方
            area.removeFromBottom (12);
            largePreview.setBounds (area);
        }

    private:
        void regenerate()
        {
            const auto text = textEditor.getText();
            const auto seed = seedSlider.getValue() > 0.0 ? (juce::uint32) seedSlider.getValue()
                                                          : MetalLogoGenerator::seedFor (text);

            const auto start = juce::Time::getMillisecondCounterHiRes();
            auto logo = MetalLogoGenerator::generate (text, juce::Font (juce::FontOptions (theme.getMetalTypeface())), params, seed);
            const auto elapsed = juce::Time::getMillisecondCounterHiRes() - start;

            const auto area = MetalLogoGenerator::getLogoArea (logo, params);
            const auto colour = Palette::deathMetal().logoText;
            largePreview.setLogo (logo, area, colour);
            smallPreview.setLogo (logo, area, colour);
            timeLabel.setText ("生成時間: "_jp + juce::String (elapsed, 2) + " ms", juce::dontSendNotification);
        }

        void copyToClipboard()
        {
            juce::String s;
            s << "// MetalLogoTuner で調整した値 (MetalLogoParams.h の初期値に反映する)\n";
            s << "MetalLogoParams {\n";
            for (auto& c : controls)
                s << "    ." << c->name << " = " << formatValue (*c, params) << ",\n";
            s << "}\n";
            juce::SystemClipboard::copyTextToClipboard (s);
            timeLabel.setText ("クリップボードにコピーしました"_jp, juce::dontSendNotification);
        }

        ThemeManager& theme;
        MetalLogoParams params;

        juce::Label textLabel, seedLabel, timeLabel;
        juce::TextEditor textEditor;
        juce::Slider seedSlider;
        std::vector<std::unique_ptr<ParamControl>> controls;
        juce::TextButton saveButton, copyButton, resetButton;
        LogoPreview largePreview, smallPreview;
    };
}

MetalLogoTunerWindow::MetalLogoTunerWindow (ThemeManager& theme)
    : DocumentWindow ("ロゴ調整 (開発用)"_jp,
                      juce::LookAndFeel::getDefaultLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new TunerContent (theme), true);
    setResizable (true, false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}
