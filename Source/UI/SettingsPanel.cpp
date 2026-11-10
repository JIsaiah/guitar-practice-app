#include "SettingsPanel.h"
#include "../Text.h"

namespace
{
    // 文字の大きさの選択肢 (倍率)
    constexpr float textScales[] = { 0.85f, 0.9f, 1.0f, 1.1f, 1.2f, 1.3f };

    // フィードバックのフォーム (Google フォーム)
    constexpr const char* feedbackFormUrl =
        "https://docs.google.com/forms/d/e/1FAIpQLScpCp7HUFtvVAEUsn4N_fgcNOKd3g68-fjdz4vUnbe5O-h9fQ/viewform?usp=header";
}

void SettingsPanel::Swatch::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (findColour (ThemeColourIds::border));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

SettingsPanel::SettingsPanel (AppSettings& s, ThemeManager& t, juce::AudioDeviceManager& deviceManager)
    : settings (s), theme (t),
      audioSelector (deviceManager,
                     0, 2,     // 入力チャンネル数 (最小, 最大)
                     2, 2,     // 出力チャンネル数
                     false, false,
                     true, false)
{
    //==== いちばん上: フィードバック ====
    feedbackButton.setButtonText ("フィードバックを送る (ご意見・不具合の報告)"_jp);
    feedbackButton.setTooltip ("ブラウザでフィードバックのフォームを開きます"_jp);
    feedbackButton.onClick = [] { juce::URL (feedbackFormUrl).launchInDefaultBrowser(); };
    addAndMakeVisible (feedbackButton);
    feedbackNote.setText ("(外部のブラウザで開きます)"_jp, juce::dontSendNotification);
    feedbackNote.setFont (juce::FontOptions (13.0f));
    feedbackNote.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (feedbackNote);

    for (auto* heading : { &generalHeading, &deathMetalHeading, &advancedHeading })
    {
        heading->setFont (juce::FontOptions (20.0f, juce::Font::bold));
        addAndMakeVisible (heading);
    }
    generalHeading.setText ("一般"_jp, juce::dontSendNotification);
    deathMetalHeading.setText ("デスメタルモード"_jp, juce::dontSendNotification);
    advancedHeading.setText ("詳細設定"_jp, juce::dontSendNotification);

    wheelHeading.setText ("マウスホイール1目盛りで変わる量"_jp, juce::dontSendNotification);
    colourHeading.setText ("色 (アクセント)"_jp, juce::dontSendNotification);
    colourNote.setText ("「#RRGGBB」の形で入力し、Enter で反映します。背景と文字の色は、読みやすさを保つため変えられません。"_jp,
                        juce::dontSendNotification);
    colourNote.setFont (juce::FontOptions (13.0f));
    colourNote.setMinimumHorizontalScale (1.0f);
    colourError.setFont (juce::FontOptions (13.0f));
    audioHeading.setText ("オーディオの入出力"_jp, juce::dontSendNotification);
    for (auto* label : { &wheelHeading, &colourHeading, &colourNote, &colourError, &audioHeading })
        addAndMakeVisible (label);

    //==== 一般 ====
    themeLabel.setText ("テーマ"_jp, juce::dontSendNotification);
    addAndMakeVisible (themeLabel);
    for (auto mode : { ColourTheme::System, ColourTheme::Light, ColourTheme::Dark })
        themeBox.addItem (ThemeManager::getColourThemeName (mode), (int) mode + 1);
    themeBox.onChange = [this] { theme.setColourTheme ((ColourTheme) (themeBox.getSelectedId() - 1)); };
    addAndMakeVisible (themeBox);

    textScaleLabel.setText ("文字の大きさ"_jp, juce::dontSendNotification);
    addAndMakeVisible (textScaleLabel);
    for (size_t i = 0; i < std::size (textScales); ++i)
        textScaleBox.addItem (juce::String (juce::roundToInt (textScales[i] * 100.0f)) + "%"
                                  + (juce::approximatelyEqual (textScales[i], 1.0f) ? " (標準)"_jp : juce::String()),
                              (int) i + 1);
    textScaleBox.onChange = [this]
    {
        const auto index = textScaleBox.getSelectedId() - 1;
        if (juce::isPositiveAndBelow (index, (int) std::size (textScales)))
            theme.setTextScale (textScales[index]);
    };
    addAndMakeVisible (textScaleBox);

    setUpRow (skipRow, "戻る・進むの幅"_jp, "秒"_jp, AppSettings::minSkipSeconds, AppSettings::maxSkipSeconds);
    setUpRow (speedRow, "速度"_jp, "%", 1, 50);
    setUpRow (volumeRow, "音量"_jp, "%", 1, 50);
    setUpRow (offsetRow, "ずらし"_jp, "ms", 1, 500);

    setUpRow (preStartDelayRow, "再生ボタンの待ち時間"_jp, "秒"_jp, 0, AppSettings::maxPreStartDelaySeconds);
    preStartDelayRow.value.setTooltip ("「▶ 再生」を押してから始まるまでの時間。0 ならすぐに始まります"_jp);
    setUpRow (songGapRow, "セットリストの待ち時間"_jp, "秒"_jp, 0, AppSettings::maxSongGapSeconds);
    songGapRow.value.setTooltip ("セットリストで次の曲を始めるまでの時間 (曲が終わったときと、「次の曲」「前の曲」を押したとき)"_jp);

    // ギターの総音量: 曲ごとのミキサーの「ギター」に、さらに掛ける。動かすとすぐに音が変わる
    setUpRow (guitarMasterRow, "ギターの総音量"_jp, "%", 0, AppSettings::maxGuitarMasterPercent);
    guitarMasterRow.value.setSliderStyle (juce::Slider::LinearHorizontal);
    guitarMasterRow.value.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 24);
    guitarMasterRow.value.setDoubleClickReturnValue (true, 100.0);
    guitarMasterRow.value.setTooltip ("すべての曲のギターの音量に掛けます。ギターが全体的に大きいときに下げます (ダブルクリックで 100%)"_jp);
    guitarMasterRow.value.onValueChange = [this] { settings.setGuitarMasterPercent ((int) guitarMasterRow.value.getValue()); };

    skipRow.value.onValueChange = [this] { settings.setSkipSeconds ((int) skipRow.value.getValue()); };
    preStartDelayRow.value.onValueChange = [this] { settings.setPreStartDelaySeconds ((int) preStartDelayRow.value.getValue()); };
    songGapRow.value.onValueChange = [this] { settings.setSongGapSeconds ((int) songGapRow.value.getValue()); };
    for (auto* row : { &speedRow, &volumeRow, &offsetRow })
        row->value.onValueChange = [this] { storeWheelSteps(); };

    //==== デスメタルモード ====
    deathMetalToggle.setButtonText ("デスメタルモードにする"_jp);
    deathMetalToggle.onClick = [this]
    {
        theme.setMode (deathMetalToggle.getToggleState() ? ThemeMode::DeathMetal : ThemeMode::Normal);
    };
    logoTunerButton.setButtonText ("ロゴ調整…"_jp);
    logoTunerButton.setTooltip ("ロゴのトゲや伸ばし方を、スライダーで調整するウィンドウを開きます"_jp);
    logoTunerButton.onClick = [this] { if (onOpenLogoTuner != nullptr) onOpenLogoTuner(); };
    addAndMakeVisible (deathMetalToggle);
   #if JUCE_DEBUG
    addAndMakeVisible (logoTunerButton);   // ロゴ調整は開発用。配布版 (Release) では出さない
   #endif

    //==== 詳細設定: 色 ====
    setUpColourRow (accentRow, "アクセント"_jp);
    accentRow.getCustom  = [this] { return theme.getCustomAccent(); };
    accentRow.getCurrent = [this] { return theme.getPalette().accent; };
    accentRow.setCustom  = [this] (std::optional<juce::Colour> c) { theme.setCustomAccent (c); };

    setUpColourRow (goodRow, "チューナーの「合っている」"_jp);
    goodRow.getCustom  = [this] { return theme.getCustomGood(); };
    goodRow.getCurrent = [this] { return theme.getPalette().good; };
    goodRow.setCustom  = [this] (std::optional<juce::Colour> c) { theme.setCustomGood (c); };

    //==== 詳細設定: デバッグ ====
    debugLogButton.setButtonText ("動作ログ・クラッシュの記録を見る…"_jp);
    debugLogButton.setTooltip ("アプリの動きの記録と、落ちたときの記録を表示します"_jp);
    debugLogButton.onClick = [this] { if (onOpenDebugLog != nullptr) onOpenDebugLog(); };
    addAndMakeVisible (debugLogButton);

    addAndMakeVisible (audioSelector);

    updateFromSettings();
    theme.addChangeListener (this);
    settings.addChangeListener (this);

    setSize (600, 1194 + 70);
}

SettingsPanel::~SettingsPanel()
{
    theme.removeChangeListener (this);
    settings.removeChangeListener (this);
}

void SettingsPanel::setUpRow (Row& row, const juce::String& name, const juce::String& unit, int min, int max)
{
    row.label.setText (name, juce::dontSendNotification);
    row.unit.setText (unit, juce::dontSendNotification);
    row.value.setSliderStyle (juce::Slider::IncDecButtons);
    row.value.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 26);
    row.value.setRange (min, max, 1.0);
    addAndMakeVisible (row.label);
    addAndMakeVisible (row.value);
    addAndMakeVisible (row.unit);
}

void SettingsPanel::setUpColourRow (ColourRow& row, const juce::String& name)
{
    row.label.setText (name, juce::dontSendNotification);
    row.hex.setInputRestrictions (7, "#0123456789abcdefABCDEF");
    row.hex.setJustification (juce::Justification::centredLeft);
    row.hex.setIndents (8, 0);
    row.hex.onReturnKey = [this, &row] { applyHex (row); };
    row.hex.onFocusLost = [this, &row] { applyHex (row); };
    row.hex.onEscapeKey = [this, &row] { updateColourRow (row); };
    row.reset.setButtonText ("既定に戻す"_jp);
    row.reset.onClick = [this, &row]
    {
        colourError.setText ({}, juce::dontSendNotification);
        row.setCustom (std::nullopt);
    };

    for (auto* c : std::initializer_list<juce::Component*> { &row.label, &row.hex, &row.swatch, &row.reset })
        addAndMakeVisible (c);
}

void SettingsPanel::applyHex (ColourRow& row)
{
    const auto text = row.hex.getText().trim();
    const auto current = row.getCustom();

    // 何も変えていない (今の色の HEX のまま) なら、何もしない
    if (text.equalsIgnoreCase (ThemeManager::toHex (row.getCurrent())))
        return;

    if (const auto colour = ThemeManager::parseHex (text))
    {
        colourError.setText ({}, juce::dontSendNotification);
        if (current != colour)
            row.setCustom (colour);
        return;
    }

    colourError.setText ("「"_jp + text + "」は色として読めません。「#E0A030」のように 6 桁で入力してください"_jp,
                         juce::dontSendNotification);
    updateColourRow (row);   // 入力欄を今の色に戻す
}

void SettingsPanel::updateColourRow (ColourRow& row)
{
    const auto current = row.getCurrent();
    row.swatch.colour = current;
    row.swatch.repaint();

    if (! row.hex.hasKeyboardFocus (false))
        row.hex.setText (ThemeManager::toHex (current), false);

    // デスメタルモード中はデスメタルの配色を使うので、色は変えられない
    const auto editable = ! theme.isDeathMetal();
    row.hex.setEnabled (editable);
    row.reset.setEnabled (editable && row.getCustom().has_value());
}

void SettingsPanel::updateFromSettings()
{
    themeBox.setSelectedId ((int) theme.getColourTheme() + 1, juce::dontSendNotification);
    themeBox.setEnabled (! theme.isDeathMetal());

    // 今の倍率に一番近い選択肢を選ぶ
    int nearest = 0;
    for (size_t i = 0; i < std::size (textScales); ++i)
        if (std::abs (textScales[i] - theme.getTextScale()) < std::abs (textScales[(size_t) nearest] - theme.getTextScale()))
            nearest = (int) i;
    textScaleBox.setSelectedId (nearest + 1, juce::dontSendNotification);

    skipRow.value.setValue (settings.getSkipSeconds(), juce::dontSendNotification);
    guitarMasterRow.value.setValue (settings.getGuitarMasterPercent(), juce::dontSendNotification);
    preStartDelayRow.value.setValue (settings.getPreStartDelaySeconds(), juce::dontSendNotification);
    songGapRow.value.setValue (settings.getSongGapSeconds(), juce::dontSendNotification);

    const auto& steps = settings.getWheelSteps();
    speedRow.value.setValue (steps.speedPercent, juce::dontSendNotification);
    volumeRow.value.setValue (steps.volumePercent, juce::dontSendNotification);
    offsetRow.value.setValue (steps.offsetMs, juce::dontSendNotification);

    deathMetalToggle.setToggleState (theme.isDeathMetal(), juce::dontSendNotification);

    updateColourRow (accentRow);
    updateColourRow (goodRow);
    colourNote.setText (theme.isDeathMetal()
                            ? "デスメタルモード中は、デスメタルの配色を使います。色を変えるには、モードを切ってください。"_jp
                            : "「#RRGGBB」の形で入力し、Enter で反映します。背景と文字の色は、読みやすさを保つため変えられません。"_jp,
                        juce::dontSendNotification);

    const auto muted = findColour (ThemeColourIds::textMuted);
    for (auto* label : { &wheelHeading, &colourHeading, &colourNote, &audioHeading, &feedbackNote })
        label->setColour (juce::Label::textColourId, muted);
    colourError.setColour (juce::Label::textColourId, findColour (ThemeColourIds::accent));

    // フィードバックのボタンはアクセントで塗る (テーマ・アクセントの色を変えたら塗り直す)
    feedbackButton.setColour (juce::TextButton::buttonColourId, findColour (ThemeColourIds::accent));
    // 文字は白 (作者の指示。アクセントの色に関係なく白にする)
    feedbackButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    feedbackButton.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
}

void SettingsPanel::storeWheelSteps()
{
    AppSettings::WheelSteps steps;
    steps.speedPercent = (int) speedRow.value.getValue();
    steps.bpm = 1;
    steps.volumePercent = (int) volumeRow.value.getValue();
    steps.offsetMs = (int) offsetRow.value.getValue();
    settings.setWheelSteps (steps);
}

void SettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    updateFromSettings();
    repaint();
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::ResizableWindow::backgroundColourId));

    // 区切り線
    g.setColour (findColour (ThemeColourIds::border));
    for (auto y : sectionLines)
        g.fillRect (20, y, getWidth() - 40, 1);
}

void SettingsPanel::resized()
{
    auto area = getLocalBounds().reduced (20, 16);
    sectionLines.clear();

    const auto row = [&area] (juce::Component& label, juce::Component& control, juce::Component* unit, int controlWidth = 150)
    {
        auto r = area.removeFromTop (32);
        label.setBounds (r.removeFromLeft (200));
        control.setBounds (r.removeFromLeft (controlWidth));
        if (unit != nullptr)
            unit->setBounds (r.removeFromLeft (60));
        area.removeFromTop (6);
    };

    const auto section = [this, &area] (juce::Label& heading)
    {
        area.removeFromTop (12);
        sectionLines.push_back (area.getY());
        area.removeFromTop (12);
        heading.setBounds (area.removeFromTop (30));
        area.removeFromTop (6);
    };

    //==== いちばん上: フィードバック ====
    // ボタンと補足は画面の真ん中に揃える
    feedbackButton.setBounds (area.removeFromTop (36).withSizeKeepingCentre (360, 36));
    area.removeFromTop (4);
    feedbackNote.setBounds (area.removeFromTop (20));
    area.removeFromTop (10);

    //==== 一般 ====
    generalHeading.setBounds (area.removeFromTop (30));
    area.removeFromTop (6);
    row (themeLabel, themeBox, nullptr, 230);
    row (textScaleLabel, textScaleBox, nullptr, 150);
    row (skipRow.label, skipRow.value, &skipRow.unit);
    row (preStartDelayRow.label, preStartDelayRow.value, &preStartDelayRow.unit);
    row (songGapRow.label, songGapRow.value, &songGapRow.unit);
    row (guitarMasterRow.label, guitarMasterRow.value, &guitarMasterRow.unit, 230);

    area.removeFromTop (4);
    wheelHeading.setBounds (area.removeFromTop (24));
    for (auto* r : { &speedRow, &volumeRow, &offsetRow })
        row (r->label, r->value, &r->unit);

    //==== デスメタルモード ====
    section (deathMetalHeading);
    {
        auto r = area.removeFromTop (32);
        deathMetalToggle.setBounds (r.removeFromLeft (260));
        logoTunerButton.setBounds (r.removeFromLeft (130));
    }

    //==== 詳細設定 ====
    section (advancedHeading);
    colourHeading.setBounds (area.removeFromTop (24));
    colourNote.setBounds (area.removeFromTop (36));
    area.removeFromTop (4);

    for (auto* colourRow : { &accentRow, &goodRow })
    {
        auto r = area.removeFromTop (32);
        colourRow->label.setBounds (r.removeFromLeft (200));
        colourRow->hex.setBounds (r.removeFromLeft (110));
        r.removeFromLeft (8);
        colourRow->swatch.setBounds (r.removeFromLeft (32));
        r.removeFromLeft (8);
        colourRow->reset.setBounds (r.removeFromLeft (110));
        area.removeFromTop (6);
    }
    colourError.setBounds (area.removeFromTop (22));

    area.removeFromTop (6);
    debugLogButton.setBounds (area.removeFromTop (32).removeFromLeft (300));

    area.removeFromTop (10);
    audioHeading.setBounds (area.removeFromTop (24));
    audioSelector.setBounds (area);
}
