#include "SessionScreen.h"
#include "../Model/TonePresets.h"
#include "../Text.h"

namespace
{
    void setUpSlider (juce::Slider& slider, double min, double max, double interval, double value)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 24);
        slider.setRange (min, max, interval);
        slider.setValue (value, juce::dontSendNotification);
        slider.setScrollWheelEnabled (true);  // ホイールで値を増減 (設計書 4.3)
    }
}

SessionScreen::SessionScreen (AudioEngine& e, SessionStore& s, Session sessionToOpen)
    : engine (e), store (s), session (std::move (sessionToOpen))
{
    savedSnapshot = juce::JSON::toString (session.toVar());

    //==== 上部 ====
    backButton.setButtonText ("◀ 一覧"_jp);
    saveButton.setButtonText ("保存"_jp);
    settingsButton.setButtonText ("⚙ 詳細"_jp);
    backButton.onClick = [this] { perform (Command::backToList); };
    saveButton.onClick = [this] { perform (Command::save); };
    settingsButton.onClick = [this] { if (onOpenSettings != nullptr) onOpenSettings(); };

    titleLabel.setText (session.title.isEmpty() ? "(無題)"_jp : session.title, juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));

    //==== 左 ====
    scoreView.setScoreName (juce::File (session.scorePath).getFileName());
    timeline.onSeek = [this] (double seconds) { if (sourceReady) engine.seekSeconds (seconds); };

    //==== 右 ====
    if (session.sourceType == SourceType::youtube)
        videoView.setText ("YouTube プレイヤー\n"_jp + session.youtubeUrl + "\n\n※ 埋め込み再生は PoC-4 で実装"_jp);
    else
        videoView.setText ("ローカル音源\n"_jp + juce::File (session.sourcePath).getFileName());

    const auto& presets = getTonePresets();
    for (size_t i = 0; i < toneButtons.size(); ++i)
    {
        auto& button = toneButtons[i];
        button.setButtonText (juce::String::fromUTF8 (presets[i].name));
        button.setClickingTogglesState (true);
        button.setRadioGroupId (1);
        button.onClick = [this, i] { selectTone ((int) i); };
    }

    sourceGainLabel.setText ("音源"_jp, juce::dontSendNotification);
    guitarGainLabel.setText ("ギター"_jp, juce::dontSendNotification);
    metronomeGainLabel.setText ("メトロノーム"_jp, juce::dontSendNotification);
    offsetLabel.setText ("ずらし (ms)"_jp, juce::dontSendNotification);

    setUpSlider (sourceGainSlider, 0.0, 1.5, 0.01, session.mixSource);
    setUpSlider (guitarGainSlider, 0.0, 1.5, 0.01, session.mixGuitar);
    setUpSlider (metronomeGainSlider, 0.0, 1.5, 0.01, session.mixMetronome);
    setUpSlider (offsetSlider, -2000.0, 2000.0, 10.0, session.syncOffsetMs);

    sourceGainSlider.onValueChange = [this]
    {
        session.mixSource = sourceGainSlider.getValue();
        engine.setSourceGain ((float) session.mixSource);
    };
    guitarGainSlider.onValueChange = [this]
    {
        session.mixGuitar = guitarGainSlider.getValue();
        engine.setGuitarGain ((float) session.mixGuitar);
    };
    metronomeGainSlider.onValueChange = [this]
    {
        session.mixMetronome = metronomeGainSlider.getValue();
        engine.setMetronomeGain ((float) session.mixMetronome);
    };
    offsetSlider.onValueChange = [this]
    {
        session.syncOffsetMs = (int) offsetSlider.getValue();
        engine.setSyncOffsetSeconds (session.syncOffsetMs / 1000.0);
    };

    //==== 下部 ====
    rewindButton.setButtonText (juce::String::fromUTF8 ("◀◀"));
    playButton.setButtonText ("▶ 再生"_jp);
    forwardButton.setButtonText (juce::String::fromUTF8 ("▶▶"));
    tapButton.setButtonText ("TAP");
    metronomeButton.setButtonText ("メトロノーム"_jp);
    metronomeButton.setClickingTogglesState (false);
    loopStartButton.setButtonText ("A");
    loopEndButton.setButtonText ("B");
    loopButton.setButtonText ("ループ"_jp);
    tunerButton.setButtonText ("チューナー"_jp);

    rewindButton.onClick = [this] { perform (Command::rewind); };
    playButton.onClick = [this] { perform (Command::togglePlay); };
    forwardButton.onClick = [this] { perform (Command::fastForward); };
    tapButton.onClick = [this] { perform (Command::tapTempo); };
    metronomeButton.onClick = [this] { perform (Command::toggleMetronome); };
    loopStartButton.onClick = [this] { perform (Command::setLoopStart); };
    loopEndButton.onClick = [this] { perform (Command::setLoopEnd); };
    loopButton.onClick = [this] { perform (Command::toggleLoop); };
    tunerButton.onClick = [this] { perform (Command::toggleTuner); };

    speedLabel.setText ("速度 %"_jp, juce::dontSendNotification);
    bpmLabel.setText ("BPM", juce::dontSendNotification);
    setUpSlider (speedSlider, 50.0, 150.0, 5.0, session.speedPercent);
    setUpSlider (bpmSlider, 40.0, 300.0, 1.0, session.bpm);

    speedSlider.onValueChange = [this]
    {
        session.speedPercent = (int) speedSlider.getValue();
        engine.setSpeed (session.speedPercent / 100.0);
    };
    bpmSlider.onValueChange = [this]
    {
        session.bpm = bpmSlider.getValue();
        session.tempoOrigin = TempoOrigin::manual;
        engine.setBpm (session.bpm);
    };

    for (auto* c : std::initializer_list<juce::Component*> {
             &backButton, &saveButton, &settingsButton, &titleLabel, &statusLabel,
             &scoreView, &timeline, &videoView,
             &toneButtons[0], &toneButtons[1], &toneButtons[2],
             &sourceGainLabel, &guitarGainLabel, &metronomeGainLabel, &offsetLabel,
             &sourceGainSlider, &guitarGainSlider, &metronomeGainSlider, &offsetSlider,
             &rewindButton, &playButton, &forwardButton, &speedLabel, &speedSlider, &bpmLabel, &bpmSlider,
             &tapButton, &metronomeButton, &loopStartButton, &loopEndButton, &loopButton, &tunerButton })
    {
        addAndMakeVisible (c);

        // スペースキーや Enter キーを、押したボタンではなくこの画面で受け取るため
        c->setMouseClickGrabsKeyboardFocus (false);
        c->setWantsKeyboardFocus (false);
    }

    setWantsKeyboardFocus (true);

    const bool canTransport = isLocalSource();
    for (auto* button : { &rewindButton, &playButton, &forwardButton, &loopStartButton, &loopEndButton, &loopButton })
        button->setEnabled (false);  // 音源の読み込みが終わってから有効にする

    if (! canTransport)
        setStatus ("YouTube の再生は未実装です (メトロノーム・チューナーは使えます)"_jp);

    applySessionToEngine();
    loadSource();
    startTimerHz (30);
}

SessionScreen::~SessionScreen()
{
    engine.setPlaying (false);
    engine.setMetronomeEnabled (false);
    engine.clearSource();
}

//==============================================================================
void SessionScreen::applySessionToEngine()
{
    selectTone (findTonePresetIndex (session.tonePresetId));
    engine.setSourceGain ((float) session.mixSource);
    engine.setGuitarGain ((float) session.mixGuitar);
    engine.setMetronomeGain ((float) session.mixMetronome);
    engine.setSpeed (session.speedPercent / 100.0);
    engine.setBpm (session.bpm);
    engine.setSyncOffsetSeconds (session.syncOffsetMs / 1000.0);
    engine.setMetronomeEnabled (false);
    updateLoop();
}

void SessionScreen::loadSource()
{
    if (! isLocalSource())
        return;

    const juce::File file (session.sourcePath);
    if (! file.existsAsFile())
    {
        setStatus ("音源ファイルが見つかりません: "_jp + session.sourcePath);
        return;
    }

    setStatus ("音源を読み込み中…"_jp);

    engine.loadSourceAsync (file, [safe = juce::Component::SafePointer (this)] (bool succeeded, const juce::String& error)
    {
        if (safe == nullptr)
            return;

        if (! succeeded)
        {
            safe->setStatus (error);
            return;
        }

        safe->sourceReady = true;
        safe->setStatus ({});
        for (auto* button : { &safe->rewindButton, &safe->playButton, &safe->forwardButton,
                              &safe->loopStartButton, &safe->loopEndButton, &safe->loopButton })
            button->setEnabled (true);
    });
}

bool SessionScreen::isDirty() const
{
    return juce::JSON::toString (session.toVar()) != savedSnapshot;
}

bool SessionScreen::save()
{
    if (! store.save (session))
    {
        setStatus ("保存できませんでした"_jp);
        return false;
    }

    savedSnapshot = juce::JSON::toString (session.toVar());
    setStatus ("保存しました"_jp);
    return true;
}

void SessionScreen::requestBackToList()
{
    if (! isDirty())
    {
        if (onBackToList != nullptr)
            onBackToList();
        return;
    }

    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("変更が保存されていません"_jp)
                       .withMessage ("一覧に戻る前に保存しますか？"_jp)
                       .withButton ("保存して戻る"_jp)
                       .withButton ("保存せずに戻る"_jp)
                       .withButton ("キャンセル"_jp)
                       .withAssociatedComponent (this);

    // 3ボタンの戻り値: 1番目 = 1、2番目 = 2、3番目 = 0
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer (this)] (int result)
    {
        if (safe == nullptr || result == 0)
            return;

        if (result == 1 && ! safe->save())
            return;

        if (safe->onBackToList != nullptr)
            safe->onBackToList();
    });
}

void SessionScreen::setStatus (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
}

//==============================================================================
void SessionScreen::perform (Command command)
{
    switch (command)
    {
        case Command::togglePlay:      togglePlay(); break;
        case Command::rewind:          skip (-5.0); break;
        case Command::fastForward:     skip (5.0); break;
        case Command::tapTempo:        tap(); break;
        case Command::setLoopStart:    setLoopPoint (true); break;
        case Command::setLoopEnd:      setLoopPoint (false); break;
        case Command::toggleLoop:      toggleLoop(); break;
        case Command::toggleMetronome: toggleMetronome(); break;
        case Command::toggleTuner:     toggleTuner(); break;
        case Command::save:            save(); break;
        case Command::backToList:      requestBackToList(); break;
    }
}

bool SessionScreen::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)  { perform (Command::togglePlay); return true; }
    if (key == juce::KeyPress::returnKey) { perform (Command::tapTempo); return true; }
    if (key == juce::KeyPress::leftKey)   { perform (Command::rewind); return true; }
    if (key == juce::KeyPress::rightKey)  { perform (Command::fastForward); return true; }
    if (key == juce::KeyPress ('s', juce::ModifierKeys::ctrlModifier, 0)) { perform (Command::save); return true; }
    return false;
}

void SessionScreen::togglePlay()
{
    if (! sourceReady)
        return;

    if (engine.isPlaying())
    {
        engine.setPlaying (false);
        return;
    }

    // 最後まで再生していたら、頭から
    if (engine.getPositionSeconds() >= engine.getSourceLengthSeconds() - 0.05)
        engine.seekSeconds (0.0);

    engine.setPlaying (true);
}

void SessionScreen::skip (double seconds)
{
    if (sourceReady)
        engine.seekSeconds (juce::jlimit (0.0, engine.getSourceLengthSeconds(), engine.getPositionSeconds() + seconds));
}

void SessionScreen::tap()
{
    if (const auto bpm = tapTempo.tap (juce::Time::getMillisecondCounterHiRes()))
    {
        bpmSlider.setValue (std::round (*bpm), juce::sendNotificationSync);
        session.tempoOrigin = TempoOrigin::tap;
    }
}

void SessionScreen::setLoopPoint (bool isStart)
{
    if (! sourceReady)
        return;

    const auto positionMs = (int) std::round (engine.getPositionSeconds() * 1000.0);

    if (isStart)
        session.loopStartMs = positionMs;
    else
        session.loopEndMs = positionMs;

    if (session.loopEndMs <= session.loopStartMs)
        session.loopEnabled = false;

    updateLoop();
}

void SessionScreen::toggleLoop()
{
    if (session.loopEndMs <= session.loopStartMs)
    {
        setStatus ("先に A と B を設定してください"_jp);
        return;
    }

    session.loopEnabled = ! session.loopEnabled;
    updateLoop();

    // ループ範囲の外にいたら A へ
    const auto position = engine.getPositionSeconds() * 1000.0;
    if (session.loopEnabled && (position < session.loopStartMs || position > session.loopEndMs))
        engine.seekSeconds (session.loopStartMs / 1000.0);
}

void SessionScreen::updateLoop()
{
    engine.setLoop (session.loopStartMs / 1000.0, session.loopEndMs / 1000.0, session.loopEnabled);
    loopButton.setToggleState (session.loopEnabled, juce::dontSendNotification);
    loopStartButton.setButtonText ("A " + formatTime (session.loopStartMs / 1000.0));
    loopEndButton.setButtonText ("B " + formatTime (session.loopEndMs / 1000.0));
}

void SessionScreen::toggleMetronome()
{
    metronomeOn = ! metronomeOn;
    engine.setMetronomeEnabled (metronomeOn);
    metronomeButton.setToggleState (metronomeOn, juce::dontSendNotification);
}

void SessionScreen::toggleTuner()
{
    if (tunerPanel != nullptr)
    {
        tunerPanel = nullptr;
        tunerButton.setToggleState (false, juce::dontSendNotification);
        return;
    }

    tunerPanel = std::make_unique<TunerPanel> (engine);
    tunerPanel->onClose = [this]
    {
        // 自分のボタンの onClick の中で破棄しないよう、後で閉じる
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
        {
            if (safe != nullptr && safe->tunerPanel != nullptr)
                safe->toggleTuner();
        });
    };
    addAndMakeVisible (*tunerPanel);
    tunerButton.setToggleState (true, juce::dontSendNotification);
    resized();
}

void SessionScreen::selectTone (int index)
{
    toneButtons[(size_t) index].setToggleState (true, juce::dontSendNotification);

    const auto& preset = getTonePresets()[(size_t) index];
    session.tonePresetId = preset.id;
    engine.setTone (preset.type);
}

//==============================================================================
void SessionScreen::timerCallback()
{
    const auto position = engine.getPositionSeconds();
    const auto length = engine.getSourceLengthSeconds();

    playButton.setButtonText (engine.isPlaying() ? "■ 停止"_jp : "▶ 再生"_jp);
    scoreView.setPosition (position, length, session.bpm, session.syncOffsetMs / 1000.0);
    timeline.setState (position, length, session.loopStartMs / 1000.0, session.loopEndMs / 1000.0, session.loopEnabled);
    saveButton.setButtonText (isDirty() ? "保存 *"_jp : "保存"_jp);
}

void SessionScreen::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void SessionScreen::resized()
{
    auto area = getLocalBounds().reduced (12);

    //==== 上部 ====
    auto top = area.removeFromTop (40);
    backButton.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (12);
    settingsButton.setBounds (top.removeFromRight (100));
    top.removeFromRight (8);
    saveButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (12);
    statusLabel.setBounds (top.removeFromRight (top.getWidth() / 2));
    titleLabel.setBounds (top);

    area.removeFromTop (12);

    //==== 下部 ====
    auto bottom = area.removeFromBottom (52);
    area.removeFromBottom (12);

    const auto place = [&bottom] (juce::Component& c, int width)
    {
        c.setBounds (bottom.removeFromLeft (width));
        bottom.removeFromLeft (6);
    };

    place (rewindButton, 52);
    place (playButton, 120);
    place (forwardButton, 52);
    bottom.removeFromLeft (10);
    place (speedLabel, 56);
    place (speedSlider, 170);
    place (bpmLabel, 36);
    place (bpmSlider, 170);
    place (tapButton, 76);
    place (metronomeButton, 110);
    bottom.removeFromLeft (10);
    place (loopStartButton, 84);
    place (loopEndButton, 84);
    place (loopButton, 70);
    tunerButton.setBounds (bottom.removeFromRight (110));

    //==== 右 ====
    auto right = area.removeFromRight (360);
    area.removeFromRight (12);

    videoView.setBounds (right.removeFromTop (200));
    right.removeFromTop (12);

    auto toneRow = right.removeFromTop (40);
    const auto toneWidth = (toneRow.getWidth() - 12) / 3;
    for (auto& button : toneButtons)
    {
        button.setBounds (toneRow.removeFromLeft (toneWidth));
        toneRow.removeFromLeft (6);
    }

    right.removeFromTop (12);
    const auto mixerRow = [&right] (juce::Label& label, juce::Slider& slider)
    {
        auto row = right.removeFromTop (34);
        label.setBounds (row.removeFromLeft (100));
        slider.setBounds (row);
        right.removeFromTop (6);
    };
    mixerRow (sourceGainLabel, sourceGainSlider);
    mixerRow (guitarGainLabel, guitarGainSlider);
    mixerRow (metronomeGainLabel, metronomeGainSlider);
    right.removeFromTop (12);
    mixerRow (offsetLabel, offsetSlider);

    //==== 左 ====
    timeline.setBounds (area.removeFromBottom (36));
    area.removeFromBottom (8);
    scoreView.setBounds (area);

    if (tunerPanel != nullptr)
        tunerPanel->setBounds (getLocalBounds().withSizeKeepingCentre (440, 260));
}
