#include "ToneDetailPanel.h"
#include "../Model/TonePresets.h"
#include "../Theme/DeathMetalLookAndFeel.h"
#include "../Text.h"

namespace
{
    constexpr float meterFloorDb = -60.0f;

    juce::FontOptions font (float size, int style = juce::Font::plain)
    {
        return AppLookAndFeel::scaled (juce::FontOptions (size, style));
    }
}

ToneDetailPanel::ToneDetailPanel (AudioEngine& e, Session& s)
    : engine (e), session (s)
{
    ampLabel.setText ("アンプ (.nam)"_jp, juce::dontSendNotification);
    cabinetLabel.setText ("キャビネット (IR .wav)"_jp, juce::dontSendNotification);
    inputLabel.setText ("入力の大きさ"_jp, juce::dontSendNotification);
    gateLabel.setText ("ノイズゲート"_jp, juce::dontSendNotification);

    ampChooseButton.setButtonText ("選ぶ…"_jp);
    cabinetChooseButton.setButtonText ("選ぶ…"_jp);
    ampClearButton.setButtonText ("外す"_jp);
    cabinetClearButton.setButtonText ("外す"_jp);
    ampChooseButton.onClick = [this] { chooseFile (true); };
    cabinetChooseButton.onClick = [this] { chooseFile (false); };
    ampClearButton.onClick = [this] { clearFile (true); };
    cabinetClearButton.onClick = [this] { clearFile (false); };

    // 入力の大きさ: NAM のモデルは、入る音の大きさで歪み方が変わる。メーターを見ながら合わせる
    inputSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    inputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 24);
    inputSlider.setRange (-24.0, 24.0, 0.5);
    inputSlider.setTextValueSuffix (" dB");
    inputSlider.setValue (session.inputGainDb, juce::dontSendNotification);
    inputSlider.setDoubleClickReturnValue (true, 0.0);
    inputSlider.setTooltip ("ギターの入力を大きく・小さくします。ダブルクリックで 0 dB に戻します"_jp);
    inputSlider.onValueChange = [this]
    {
        session.inputGainDb = inputSlider.getValue();
        engine.setInputGainDb ((float) session.inputGainDb);
    };

    // ノイズゲート (作者の指示): この大きさより小さい音 (弾いていないときの雑音) を消す。
    // いちばん左 (-90 dB) がオフ。どの音色 (クリーン・歪み・NAM・VST) にも、音色の前にかかる
    gateSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    gateSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 24);
    gateSlider.setRange (NoiseGate::offDb, -20.0, 1.0);
    gateSlider.textFromValueFunction = [this] (double value)
    {
        if (isPluginTone())
            return "オフ (VST)"_jp;
        return value <= NoiseGate::offDb ? "オフ"_jp : juce::String (juce::roundToInt (value)) + " dB";
    };
    gateSlider.valueFromTextFunction = [] (const juce::String& text)
    {
        const auto digits = text.retainCharacters ("-0123456789.");
        return digits.isEmpty() ? (double) NoiseGate::offDb : -std::abs (digits.getDoubleValue());   // 「50」と入れても -50 dB にする
    };
    gateSlider.setValue (session.noiseGateDb, juce::dontSendNotification);
    gateSlider.updateText();
    gateSlider.setDoubleClickReturnValue (true, NoiseGate::offDb);
    gateSlider.setTooltip ("この大きさより小さい音 (弾いていないときの雑音) を消します。右ほど強く消します。"
                           "弾いた音の消え際が切れるときは、左に戻してください。ダブルクリックでオフ"_jp);
    gateSlider.onValueChange = [this]
    {
        session.noiseGateDb = gateSlider.getValue();
        engine.setNoiseGateDb ((float) session.noiseGateDb);
    };
    updateGateEnabled();

    audioSettingsButton.setButtonText ("オーディオ設定…"_jp);
    audioSettingsButton.setTooltip ("ASIO の選択、入力チャンネル、サンプルレート、バッファの大きさ"_jp);
    audioSettingsButton.onClick = [this] { if (onOpenAudioSettings != nullptr) onOpenAudioSettings(); };
    closeButton.setButtonText ("閉じる"_jp);
    closeButton.onClick = [this] { if (onClose != nullptr) onClose(); };

    statusLabel.setMinimumHorizontalScale (1.0f);
    deviceLabel.setMinimumHorizontalScale (1.0f);
    statusLabel.setJustificationType (juce::Justification::topLeft);
    deviceLabel.setJustificationType (juce::Justification::topLeft);

    for (auto* c : std::initializer_list<juce::Component*> {
             &ampLabel, &ampName, &ampChooseButton, &ampClearButton,
             &cabinetLabel, &cabinetName, &cabinetChooseButton, &cabinetClearButton,
             &inputLabel, &inputSlider, &gateLabel, &gateSlider, &statusLabel, &deviceLabel, &audioSettingsButton, &closeButton })
        addAndMakeVisible (c);

    updateFileLabels();
    timerCallback();   // 開いた直後から、状態とデバイスの情報を出しておく
    startTimerHz (30);
}

//==============================================================================
void ToneDetailPanel::chooseFile (bool isAmp)
{
    const auto current = juce::File (isAmp ? session.namModelPath : session.irPath);
    const auto start = current.existsAsFile() ? current.getParentDirectory()
                                              : juce::File::getSpecialLocation (juce::File::userHomeDirectory);

    fileChooser = std::make_unique<juce::FileChooser> (isAmp ? "アンプのモデル (.nam) を選ぶ"_jp : "キャビネットの IR (.wav) を選ぶ"_jp,
                                                       start, isAmp ? "*.nam" : "*.wav");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer (this), isAmp] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (safe == nullptr || file == juce::File())
            return;

        (isAmp ? safe->session.namModelPath : safe->session.irPath) = file.getFullPathName();
        safe->updateFileLabels();
        if (safe->onToneFilesChanged != nullptr)
            safe->onToneFilesChanged();
    });
}

void ToneDetailPanel::clearFile (bool isAmp)
{
    (isAmp ? session.namModelPath : session.irPath) = {};
    updateFileLabels();
    if (onToneFilesChanged != nullptr)
        onToneFilesChanged();
}

void ToneDetailPanel::updateFileLabels()
{
    const auto describe = [] (const juce::String& path)
    {
        return path.isEmpty() ? "(なし)"_jp : juce::File (path).getFileNameWithoutExtension();
    };

    ampName.setText (describe (session.namModelPath), juce::dontSendNotification);
    cabinetName.setText (describe (session.irPath), juce::dontSendNotification);
    ampName.setTooltip (session.namModelPath);
    cabinetName.setTooltip (session.irPath);
    ampClearButton.setEnabled (session.namModelPath.isNotEmpty());
    cabinetClearButton.setEnabled (session.irPath.isNotEmpty());
}

//==============================================================================
juce::String ToneDetailPanel::describeStatus() const
{
    const auto info = engine.getNamToneInfo();
    const auto device = engine.getDeviceInfo();

    juce::StringArray lines;

    if (session.tonePresetId != namTonePresetId)
        lines.add ("※ 今は簡易な音色です。音色の「NAM」を選ぶと、ここで選んだモデルで鳴ります"_jp);

    if (info.loading)
        lines.add ("読み込み中…"_jp);
    else if (info.error.isNotEmpty())
        lines.add (info.error);
    else if (session.namModelPath.isEmpty() && session.irPath.isEmpty())
        lines.add ("アンプのモデル (.nam) を選んでください。IR (.wav) は、モデルにキャビネットが含まれていなければ選びます"_jp);
    else if (info.active)
        lines.add ("準備できました"_jp);
    else
        lines.add ("オーディオデバイスの準備を待っています (デバイスが動き出すと、その設定で読み込みます)"_jp);

    // モデルを作ったときのサンプルレートとデバイスが違うと、音が変わる (NAM は基本的に 48 kHz)
    if (! info.loading && info.modelSampleRate > 0.0 && device.sampleRate > 0.0
        && ! juce::approximatelyEqual (info.modelSampleRate, device.sampleRate))
        lines.add ("注意: このモデルは "_jp + juce::String (juce::roundToInt (info.modelSampleRate))
                   + " Hz 用です。オーディオ設定でサンプルレートを合わせてください (今: "_jp
                   + juce::String (juce::roundToInt (device.sampleRate)) + " Hz)");

    return lines.joinIntoString ("\n");
}

juce::String ToneDetailPanel::describeDevice() const
{
    const auto device = engine.getDeviceInfo();

    if (device.deviceName.isEmpty())
        return "オーディオデバイスが開かれていません。「オーディオ設定…」で選んでください"_jp;

    juce::String text;
    text << device.typeName << " / " << device.deviceName << "\n"
         << juce::roundToInt (device.sampleRate) << " Hz / " << device.bufferSize << " サンプル"_jp
         << "   遅延 (ドライバ報告値): "_jp << juce::String (device.reportedLatencyMs, 1) << " ms"
         << "   CPU: " << juce::roundToInt (device.cpuUsage * 100.0) << "%";

    if (! device.hasInput)
        text << "\n" << "入力チャンネルが選ばれていません。オーディオ設定で、ギターをつないだ入力を選んでください"_jp;
    else if (device.typeName != "ASIO")
        text << "\n" << "ASIO を選ぶと、遅延が小さくなります (オーディオ設定の「Audio device type」)"_jp;

    return text;
}

bool ToneDetailPanel::isPluginTone() const
{
    return session.tonePresetId == pluginTonePresetId;
}

// VST プラグインのときは、ノイズゲートをオフにして、スライダーを使えなくする (作者の指示)。
// 保存してある値は変えないので、ほかの音色に戻すと、前のゲートの設定に戻る
void ToneDetailPanel::updateGateEnabled()
{
    const auto plugin = isPluginTone();
    if (gateSlider.isEnabled() == ! plugin && gateLabel.getTooltip().isNotEmpty() == plugin)
        return;

    gateSlider.setEnabled (! plugin);
    // つまみも「オフ」の位置 (左端) に置く。知らせを出さないので、保存してある値 (session.noiseGateDb) は変わらない
    gateSlider.setValue (plugin ? (double) NoiseGate::offDb : session.noiseGateDb, juce::dontSendNotification);
    gateSlider.updateText();
    const auto tip = plugin ? "VST プラグインを使っているときは、ノイズゲートはオフです (プラグインの中のゲートを使ってください)"_jp
                            : juce::String();
    gateLabel.setTooltip (tip);
    gateSlider.setTooltip (plugin ? tip
                                  : "この大きさより小さい音 (弾いていないときの雑音) を消します。右ほど強く消します。"
                                    "弾いた音の消え際が切れるときは、左に戻してください。ダブルクリックでオフ"_jp);
}

void ToneDetailPanel::timerCallback()
{
    updateGateEnabled();   // 開いている間に音色が変わっても合わせる

    // メーター: 上がるときはすぐ、下がるときは 1 フレームに 1.5 dB ずつ
    const auto update = [] (float& shownDb, bool& clipped, float peak)
    {
        const auto db = juce::Decibels::gainToDecibels (peak, meterFloorDb);
        shownDb = juce::jmax (db, shownDb - 1.5f);
        if (peak >= 0.99f)
            clipped = true;
    };

    update (inputLevelDb, inputClipped, engine.takeInputPeak());
    update (outputLevelDb, outputClipped, engine.takeOutputPeak());

    // 赤い表示 (音割れ) は 1.5 秒ほど残す
    if (inputClipped || outputClipped)
    {
        if (++clipHoldFrames > 45)
        {
            inputClipped = outputClipped = false;
            clipHoldFrames = 0;
        }
    }

    statusLabel.setText (describeStatus(), juce::dontSendNotification);
    deviceLabel.setText (describeDevice(), juce::dontSendNotification);
    repaint (inputMeterArea.getUnion (outputMeterArea).expanded (4));
}

//==============================================================================
void ToneDetailPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (findColour (ThemeColourIds::panel));
    g.fillRoundedRectangle (bounds, 12.0f);
    g.setColour (findColour (ThemeColourIds::border));
    g.drawRoundedRectangle (bounds, 12.0f, 1.0f);

    g.setColour (findColour (juce::Label::textColourId));
    g.setFont (font (18.0f, juce::Font::bold));
    g.drawText ("音色の設定"_jp, getLocalBounds().reduced (24, 16).removeFromTop (28), juce::Justification::centredLeft);

    // メーター
    const auto drawMeter = [&] (juce::Rectangle<int> area, const juce::String& name, float levelDb, bool clipped)
    {
        auto r = area;
        g.setColour (findColour (ThemeColourIds::textMuted));
        g.setFont (font (13.0f));
        g.drawText (name, r.removeFromLeft (60), juce::Justification::centredLeft);

        auto bar = r.toFloat().reduced (0.0f, 5.0f);
        g.setColour (findColour (ThemeColourIds::border));
        g.fillRoundedRectangle (bar, 3.0f);

        const auto proportion = juce::jlimit (0.0f, 1.0f, (levelDb - meterFloorDb) / -meterFloorDb);
        const auto colour = clipped ? juce::Colours::red
                          : levelDb > -6.0f ? findColour (ThemeColourIds::accent)
                                            : findColour (ThemeColourIds::good);
        g.setColour (colour);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * proportion), 3.0f);

        // -18 dB (NAM のモデルが想定する、ふつうの大きさ) の目印
        const auto markX = bar.getX() + bar.getWidth() * (1.0f - 18.0f / -meterFloorDb);
        g.setColour (findColour (juce::Label::textColourId).withAlpha (0.5f));
        g.fillRect (markX, bar.getY() - 3.0f, 1.0f, bar.getHeight() + 6.0f);
    };

    drawMeter (inputMeterArea, "入力"_jp, inputLevelDb, inputClipped);
    drawMeter (outputMeterArea, "出力"_jp, outputLevelDb, outputClipped);
}

void ToneDetailPanel::resized()
{
    auto area = getLocalBounds().reduced (24, 16);
    area.removeFromTop (36);   // 見出し

    constexpr int labelWidth = 170, buttonWidth = 76, rowHeight = 32;

    const auto fileRow = [&] (juce::Label& label, juce::Label& name, juce::Button& choose, juce::Button& clear)
    {
        auto row = area.removeFromTop (rowHeight);
        label.setBounds (row.removeFromLeft (labelWidth));
        clear.setBounds (row.removeFromRight (buttonWidth).reduced (0, 2));
        row.removeFromRight (6);
        choose.setBounds (row.removeFromRight (buttonWidth).reduced (0, 2));
        row.removeFromRight (8);
        name.setBounds (row);
        area.removeFromTop (6);
    };

    fileRow (ampLabel, ampName, ampChooseButton, ampClearButton);
    fileRow (cabinetLabel, cabinetName, cabinetChooseButton, cabinetClearButton);

    area.removeFromTop (6);
    auto inputRow = area.removeFromTop (rowHeight);
    inputLabel.setBounds (inputRow.removeFromLeft (labelWidth));
    inputSlider.setBounds (inputRow);

    area.removeFromTop (6);
    auto gateRow = area.removeFromTop (rowHeight);
    gateLabel.setBounds (gateRow.removeFromLeft (labelWidth));
    gateSlider.setBounds (gateRow);

    area.removeFromTop (8);
    inputMeterArea = area.removeFromTop (24);
    area.removeFromTop (4);
    outputMeterArea = area.removeFromTop (24);

    auto buttons = area.removeFromBottom (36);
    closeButton.setBounds (buttons.removeFromRight (110));
    audioSettingsButton.setBounds (buttons.removeFromLeft (160));

    area.removeFromBottom (8);
    area.removeFromTop (10);
    deviceLabel.setBounds (area.removeFromBottom (64));
    statusLabel.setBounds (area);
}
