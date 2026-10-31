#include "SessionScreen.h"
#include "../Model/TonePresets.h"
#include "../Model/YouTubeUrl.h"
#include "../Text.h"

namespace
{
    void setUpSlider (juce::Slider& slider, double min, double max, double interval, double value)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 24);
        slider.setRange (min, max, interval);
        slider.setValue (value, juce::dontSendNotification);
        slider.setScrollWheelEnabled (true);  // ホイールで値を増減 (設計書 4.3)
    }

    const char* const scoreFilePatterns = "*.gp;*.gpx;*.gp5;*.gp4;*.gp3;*.musicxml;*.xml";
    const char* const audioFilePatterns = "*.mp3;*.wav;*.flac;*.ogg;*.aif;*.aiff";
}

SessionScreen::SessionScreen (AudioEngine& e, SessionStore& s, ThemeManager& t, AppSettings& appSettings,
                              YouTubePlayer& yt, ScoreViewer& sv, Session sessionToOpen)
    : engine (e), store (s), theme (t), youtube (yt), scoreViewer (sv), settings (appSettings),
      session (std::move (sessionToOpen)),
      backButton (t), saveButton (t), settingsButton (t),
      playButton (t), tapButton (t), metronomeButton (t), loopButton (t), tunerButton (t)
{
    savedSnapshot = juce::JSON::toString (session.toVar());
    savedBpm = session.bpm;
    savedTempoOrigin = session.tempoOrigin;

    //==== 上部 ====
    backButton.setFixedText (FixedTextId::BackToList, "◀ 一覧"_jp);
    saveButton.setFixedText (FixedTextId::Save, "保存"_jp);
    settingsButton.setFixedText (FixedTextId::Details, "⚙ 詳細"_jp);
    backButton.onClick = [this] { perform (Command::backToList); };
    saveButton.onClick = [this] { perform (Command::save); };
    settingsButton.onClick = [this] { if (onOpenSettings != nullptr) onOpenSettings(); };

    titleLabel.setText (session.title.isEmpty() ? "(無題)"_jp : session.title, juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    statusLabel.setJustificationType (juce::Justification::centredRight);

    //==== 左: タブ譜 ====
    scoreNameLabel.setFont (juce::FontOptions (14.0f));
    positionLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    positionLabel.setJustificationType (juce::Justification::centredRight);
    scoreChooseButton.setButtonText ("タブ譜を選ぶ…"_jp);
    scoreRemoveButton.setButtonText ("外す"_jp);
    scoreChooseButton.onClick = [this] { chooseScoreFile(); };
    scoreRemoveButton.onClick = [this] { removeScore(); };
    timeline.onSeek = [this] (double seconds) { if (sourceReady) seekTo (seconds); };

    //==== 境目 (引っ張ると右のパネルの幅が変わる) と、隠す・出すタブ ====
    splitter.onDragStart = [this] (int) { sidePanelWidthAtDragStart = settings.getSidePanelWidth(); };
    splitter.onDrag = [this] (int deltaX)
    {
        settings.setSidePanelWidth (sidePanelWidthAtDragStart - deltaX);
        resized();
    };
    sidePanelButton.setTooltip ("右のパネルを隠す・出す"_jp);
    sidePanelButton.onClick = [this] { toggleSidePanel(); };

    //==== 右: 音源 ====
    sourceInfoLabel.setFont (juce::FontOptions (12.0f));
    sourceInfoLabel.onTextChange = [this] { changeYouTubeUrl (sourceInfoLabel.getText()); };
    sourceRemoveButton.setTooltip ("音源を外す (ファイルか YouTube を選び直す)"_jp);
    sourceRemoveButton.onClick = [this] { requestRemoveSource(); };
    chooseFileButton.setButtonText ("ファイルを選ぶ…"_jp);
    pasteUrlButton.setButtonText ("YouTube の URL を貼り付け"_jp);
    chooseFileButton.onClick = [this] { chooseSourceFile(); };
    pasteUrlButton.onClick = [this] { pasteYouTubeUrl(); };

    //==== 右: 音色 ====
    const auto& presets = getTonePresets();
    for (size_t i = 0; i < toneButtons.size(); ++i)
    {
        auto& button = toneButtons[i];
        button.setButtonText (juce::String::fromUTF8 (presets[i].name));
        button.setClickingTogglesState (true);
        button.setRadioGroupId (1);
        button.onClick = [this, i] { selectTone ((int) i); };
    }

    //==== 右: ミキサー ====
    sourceGainLabel.setText ("音源"_jp, juce::dontSendNotification);
    guitarGainLabel.setText ("ギター"_jp, juce::dontSendNotification);
    metronomeGainLabel.setText ("メトロノーム"_jp, juce::dontSendNotification);

    setUpSlider (sourceGainSlider, 0.0, 1.5, 0.01, session.mixSource);
    setUpSlider (guitarGainSlider, 0.0, 1.5, 0.01, session.mixGuitar);
    setUpSlider (metronomeGainSlider, 0.0, 1.5, 0.01, session.mixMetronome);

    sourceGainSlider.onValueChange = [this]
    {
        session.mixSource = sourceGainSlider.getValue();
        engine.setSourceGain ((float) session.mixSource);

        if (isYouTube())   // YouTube の音はアプリのミキサーを通らないので、プレイヤーの音量で合わせる
            youtube.setVolume (session.mixSource);
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

    //==== 右: ずらし (左右のボタン) ====
    offsetValueLabel.setJustificationType (juce::Justification::centred);
    offsetValueLabel.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    offsetDownButton.onClick = [this] { changeOffset (-settings.getWheelSteps().offsetMs); };
    offsetUpButton.onClick = [this] { changeOffset (settings.getWheelSteps().offsetMs); };
    offsetDownButton.setTooltip ("音源を早める (タブ譜とメトロノームを前へ)"_jp);
    offsetUpButton.setTooltip ("音源を遅らせる (タブ譜とメトロノームを後ろへ)"_jp);

    //==== 下部 ====
    playButton.setFixedText (FixedTextId::Play, "▶ 再生"_jp);
    tapButton.setFixedText (FixedTextId::Tap, "TAP");
    metronomeButton.setFixedText (FixedTextId::Metronome, "メトロノーム"_jp);
    metronomeButton.setTooltip ("オンにすると、再生中に曲の拍に合わせて鳴ります"_jp);
    metronomeSoloButton.setButtonText ("単体"_jp);
    metronomeSoloButton.setTooltip ("曲を再生せずに、メトロノームだけを鳴らします"_jp);
    loopStartButton.setButtonText ("A");
    loopEndButton.setButtonText ("B");
    loopButton.setFixedText (FixedTextId::Loop, "ループ"_jp);
    tunerButton.setFixedText (FixedTextId::Tuner, "チューナー"_jp);

    rewindButton.onClick = [this] { perform (Command::rewind); };
    playButton.onClick = [this] { perform (Command::togglePlay); };
    forwardButton.onClick = [this] { perform (Command::fastForward); };
    tapButton.onClick = [this] { perform (Command::tapTempo); };
    metronomeButton.onClick = [this] { perform (Command::toggleMetronome); };
    metronomeSoloButton.onClick = [this] { toggleMetronomeSolo(); };
    loopStartButton.onClick = [this] { perform (Command::setLoopStart); };
    loopEndButton.onClick = [this] { perform (Command::setLoopEnd); };
    loopButton.onClick = [this] { perform (Command::toggleLoop); };
    tunerButton.onClick = [this] { perform (Command::toggleTuner); };

    speedLabel.setText ("速度 %"_jp, juce::dontSendNotification);
    bpmLabel.setText ("BPM", juce::dontSendNotification);
    setUpSlider (speedSlider, 50.0, 200.0, 1.0, session.speedPercent);
    setUpSlider (bpmSlider, 40.0, 300.0, 1.0, session.bpm);
    speedSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 24);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 24);

    // BPM をダブルクリックすると、この曲に保存されている値に戻す
    bpmSlider.setDoubleClickReturnValue (true, savedBpm);
    bpmSlider.setTooltip ("ダブルクリックで、保存されている BPM に戻す"_jp);

    speedSlider.onValueChange = [this]
    {
        session.speedPercent = (int) speedSlider.getValue();
        engine.setSpeed (session.speedPercent / 100.0);

        if (isYouTube())
        {
            requestedRate = session.speedPercent / 100.0;
            rateRequestedAtMs = juce::Time::getMillisecondCounterHiRes();
            youtube.setRate (requestedRate);   // YouTube が対応する倍率に丸められる (handleYouTubeState で表示を合わせる)
        }
    };
    bpmSlider.onValueChange = [this]
    {
        session.bpm = bpmSlider.getValue();
        session.tempoOrigin = juce::exactlyEqual (session.bpm, savedBpm) ? savedTempoOrigin : TempoOrigin::manual;
        engine.setBpm (session.bpm);
    };

    for (auto* c : std::initializer_list<juce::Component*> {
             &backButton, &saveButton, &settingsButton, &titleLabel, &statusLabel,
             &scoreNameLabel, &positionLabel, &scoreChooseButton, &scoreRemoveButton, &scoreView, &timeline,
             &splitter, &sidePanelButton,
             &sourceHeading, &toneHeading, &mixerHeading, &offsetHeading,
             &videoView, &sourceInfoLabel, &sourceRemoveButton, &chooseFileButton, &pasteUrlButton,
             &toneButtons[0], &toneButtons[1], &toneButtons[2],
             &sourceGainLabel, &guitarGainLabel, &metronomeGainLabel,
             &sourceGainSlider, &guitarGainSlider, &metronomeGainSlider,
             &offsetDownButton, &offsetValueLabel, &offsetUpButton,
             &rewindButton, &playButton, &forwardButton, &speedLabel, &speedSlider, &bpmLabel, &bpmSlider,
             &tapButton, &metronomeButton, &metronomeSoloButton,
             &loopStartButton, &loopEndButton, &loopButton, &tunerButton })
    {
        addAndMakeVisible (c);

        // スペースキーや Enter キーを、押したボタンではなくこの画面で受け取るため
        c->setMouseClickGrabsKeyboardFocus (false);
        c->setWantsKeyboardFocus (false);
    }

    setWantsKeyboardFocus (true);

    // URL の欄は、ダブルクリックで文字を入力できるようにする (上でキーボードを受け取らない設定にしたため戻す)
    sourceInfoLabel.setWantsKeyboardFocus (true);

    applyAppSettings();
    settings.addChangeListener (this);
    lookAndFeelChanged();

    setTransportEnabled (false);  // 音源の読み込みが終わってから有効にする

    applySessionToEngine();
    updateSourceUi();
    loadSource();
    setUpYouTube();
    loadScore();
    startTimerHz (30);
}

SessionScreen::~SessionScreen()
{
    settings.removeChangeListener (this);

    if (isYouTube())
    {
        // プレイヤーは MainComponent のもの。止めて隠し、次の画面のために返す
        youtube.onStateChanged = nullptr;
        youtube.onError = nullptr;
        youtube.unload();
        engine.clearExternalClock();
    }
    youtube.hide();

    scoreViewer.onLoaded = nullptr;
    scoreViewer.onError = nullptr;
    scoreViewer.hide();

    engine.setPlaying (false);
    engine.setMetronomeEnabled (false);
    engine.setMetronomeSolo (false);
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
    changeOffset (0);
    updateLoop();
}

void SessionScreen::setTransportEnabled (bool enabled)
{
    for (auto* button : std::initializer_list<juce::Button*> { &rewindButton, &playButton, &forwardButton,
                                                               &loopStartButton, &loopEndButton, &loopButton })
        button->setEnabled (enabled);
}

//==============================================================================
// 音源: ローカルのファイル
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
        if (safe == nullptr || ! safe->isLocalSource())
            return;

        if (! succeeded)
        {
            safe->setStatus (error);
            return;
        }

        safe->sourceReady = true;
        safe->setStatus ({});
        safe->setTransportEnabled (true);
    });
}

//==============================================================================
// 音源: YouTube (設計書 v0.7 3.2、5.2)
void SessionScreen::setUpYouTube()
{
    if (! isYouTube())
        return;

    const auto videoId = YouTubeUrl::extractVideoId (session.youtubeUrl);
    if (videoId.isEmpty())
    {
        setStatus ("YouTube の URL から動画を特定できません"_jp);
        videoView.setText ("YouTube の URL から動画を特定できません\n"_jp + session.youtubeUrl);
        return;
    }

    youtube.onStateChanged = [this] { handleYouTubeState(); };
    youtube.onError = [this] (const juce::String& message) { setStatus (message); };

    setStatus ("YouTube を読み込み中…"_jp);
    requestedRate = session.speedPercent / 100.0;
    youtube.setVolume (session.mixSource);
    youtube.load (videoId, YouTubeUrl::extractStartSeconds (session.youtubeUrl));
    placeWebViews();
}

void SessionScreen::handleYouTubeState()
{
    const auto& state = youtube.getState();

    // メトロノームを YouTube の時計に合わせる
    engine.setExternalClock (state.time, state.rate, youtube.isPlaying());

    if (state.videoReady && ! sourceReady)
    {
        sourceReady = true;
        setStatus ({});
        setTransportEnabled (true);

        // 動画の準備ができる前に送った設定は捨てられるので、ここでもう一度送る
        rateRequestedAtMs = juce::Time::getMillisecondCounterHiRes();
        youtube.setRate (requestedRate);
        youtube.setVolume (session.mixSource);
        updateLoop();

        if (onVideoReady != nullptr)
            onVideoReady();
    }

    // YouTube 側 (プレイヤーの歯車) で速度を変えたら、アプリの表示も合わせる。
    // アプリから頼んだ直後は、まだ古い速度が届くことがあるので、少し待ってから比べる
    const auto sinceRequest = juce::Time::getMillisecondCounterHiRes() - rateRequestedAtMs;
    if (sourceReady && state.rate > 0.0 && std::abs (state.rate - requestedRate) > 0.001 && sinceRequest > 600.0)
    {
        requestedRate = state.rate;
        session.speedPercent = juce::roundToInt (state.rate * 100.0);
        speedSlider.setValue (session.speedPercent, juce::dontSendNotification);
    }
}

void SessionScreen::changeYouTubeUrl (const juce::String& newUrl)
{
    const auto url = newUrl.trim();

    if (url == session.youtubeUrl)
        return;

    if (YouTubeUrl::extractVideoId (url).isEmpty())
    {
        setStatus ("YouTube の URL ではありません。元の URL に戻しました"_jp);
        sourceInfoLabel.setText (session.youtubeUrl, juce::dontSendNotification);
        return;
    }

    // 新しい動画を読み込み直す。保存するまで、セッションには「未保存の変更」として残る
    session.youtubeUrl = url;
    sourceReady = false;
    setTransportEnabled (false);
    engine.clearExternalClock();
    setUpYouTube();
    grabKeyboardFocus();   // Space などのキー操作を、練習画面に戻す
}

//==============================================================================
// 音源を外す・選び直す
void SessionScreen::requestRemoveSource()
{
    const auto what = isYouTube() ? "YouTube のリンク"_jp : "音源ファイル"_jp;

    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("音源を外す"_jp)
                       .withMessage (what + "を外しますか？\n外したあと、ファイルか YouTube の URL を選び直せます。\n(保存するまで、セッションのファイルは変わりません)"_jp)
                       .withButton ("外す"_jp)
                       .withButton ("キャンセル"_jp)
                       .withAssociatedComponent (this);

    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer (this)] (int result)
    {
        if (safe != nullptr && result == 1)
            safe->removeSource();
    });
}

void SessionScreen::removeSource()
{
    if (isYouTube())
    {
        youtube.onStateChanged = nullptr;
        youtube.onError = nullptr;
        youtube.unload();
        youtube.hide();
        engine.clearExternalClock();
    }

    engine.setPlaying (false);
    engine.clearSource();

    session.sourceType = SourceType::none;
    session.sourcePath = {};
    session.youtubeUrl = {};
    sourceReady = false;
    setTransportEnabled (false);

    updateSourceUi();
    setStatus ("音源を選んでください (ファイルか YouTube の URL)"_jp);
}

void SessionScreen::chooseSourceFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("音源ファイルを選ぶ"_jp, juce::File(), audioFilePatterns);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer (this)] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (safe == nullptr || file == juce::File())
            return;

        if (safe->isYouTube())
            safe->removeSource();

        safe->session.sourceType = SourceType::local;
        safe->session.sourcePath = file.getFullPathName();
        safe->session.youtubeUrl = {};
        safe->sourceReady = false;
        safe->updateSourceUi();
        safe->loadSource();
    });
}

void SessionScreen::pasteYouTubeUrl()
{
    // クリップボードは、このボタンを押したときだけ読む (設計書 4.1 の 2)
    const auto text = juce::SystemClipboard::getTextFromClipboard().trim();

    if (YouTubeUrl::extractVideoId (text).isEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "YouTube の URL ではありません"_jp,
                                                "YouTube の動画ページの URL をコピーしてから、もう一度押してください。"_jp);
        return;
    }

    engine.setPlaying (false);
    engine.clearSource();

    session.sourceType = SourceType::youtube;
    session.youtubeUrl = text;
    session.sourcePath = {};
    sourceReady = false;
    updateSourceUi();
    setUpYouTube();
}

void SessionScreen::updateSourceUi()
{
    // 音源の種類によって、動画の枠・その下の欄・選び直しのボタンを切り替える
    sourceInfoLabel.setEditable (false, isYouTube(), false);

    if (isYouTube())
    {
        videoView.setText ("YouTube を準備しています…"_jp);
        sourceInfoLabel.setText (session.youtubeUrl, juce::dontSendNotification);
        sourceInfoLabel.setTooltip ("ダブルクリックで URL を変更。Enter で読み込み直します"_jp);
    }
    else if (isLocalSource())
    {
        videoView.setText ("ローカル音源\n"_jp + juce::File (session.sourcePath).getFileName());
        sourceInfoLabel.setText (juce::File (session.sourcePath).getFileName(), juce::dontSendNotification);
        sourceInfoLabel.setTooltip (session.sourcePath);
    }
    else
    {
        videoView.setText ("音源がありません\nファイルか YouTube の URL を選んでください"_jp);
        sourceInfoLabel.setText ({}, juce::dontSendNotification);
        sourceInfoLabel.setTooltip ({});
    }

    resized();
}

//==============================================================================
// タブ譜 (設計書 #5、PoC-3)
void SessionScreen::loadScore()
{
    scoreViewer.onLoaded = [this] (double tempo)
    {
        setStatus ({});
        juce::ignoreUnused (tempo);
    };
    scoreViewer.onError = [this] (const juce::String& message) { setStatus ("タブ譜: "_jp + message); };

    scoreViewer.setColours (theme.getPalette());
    scoreViewer.load (juce::File (session.scorePath), session.scoreTrackIndex);
    updateScoreUi();
}

void SessionScreen::chooseScoreFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("タブ譜ファイルを選ぶ"_jp, juce::File(), scoreFilePatterns);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer (this)] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (safe == nullptr || file == juce::File())
            return;

        safe->session.scorePath = file.getFullPathName();
        safe->session.scoreTrackIndex = 0;
        safe->loadScore();

        // 新しく選んだタブ譜のテンポを、BPM にする (設計書 #11: BPM はタブ譜を優先)
        safe->scoreViewer.onLoaded = [safe] (double tempo)
        {
            if (safe == nullptr || tempo <= 0.0)
                return;

            safe->bpmSlider.setValue (tempo, juce::sendNotificationSync);
            safe->session.tempoOrigin = TempoOrigin::score;
            safe->setStatus ("タブ譜のテンポ (BPM "_jp + juce::String (juce::roundToInt (tempo)) + ") にしました"_jp);
        };
    });
}

void SessionScreen::removeScore()
{
    session.scorePath = {};
    session.scoreTrackIndex = 0;
    scoreViewer.load ({}, 0);
    updateScoreUi();
}

void SessionScreen::updateScoreUi()
{
    const juce::File file (session.scorePath);
    scoreNameLabel.setText ("タブ譜: "_jp + (session.scorePath.isEmpty() ? "なし"_jp : file.getFileName()),
                            juce::dontSendNotification);
    scoreRemoveButton.setEnabled (session.scorePath.isNotEmpty());

    if (session.scorePath.isNotEmpty() && ! file.existsAsFile())
        setStatus ("タブ譜のファイルが見つかりません: "_jp + session.scorePath);

    placeWebViews();
}

//==============================================================================
double SessionScreen::getPosition() const
{
    return isYouTube() ? youtube.getEstimatedTime() : engine.getPositionSeconds();
}

double SessionScreen::getLength() const
{
    return isYouTube() ? youtube.getState().duration : engine.getSourceLengthSeconds();
}

bool SessionScreen::isTransportPlaying() const
{
    return isYouTube() ? youtube.isPlaying() : (isLocalSource() && engine.isPlaying());
}

void SessionScreen::seekTo (double seconds)
{
    if (isYouTube())
        youtube.seek (seconds);
    else
        engine.seekSeconds (seconds);
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
    savedBpm = session.bpm;
    savedTempoOrigin = session.tempoOrigin;
    bpmSlider.setDoubleClickReturnValue (true, savedBpm);
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
        case Command::rewind:          skip (-settings.getSkipSeconds()); break;
        case Command::fastForward:     skip (settings.getSkipSeconds()); break;
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

    if (isYouTube())
    {
        if (youtube.isPlaying())
            youtube.pause();
        else
            youtube.play();   // 最後まで再生していたら、YouTube が頭から再生する
        return;
    }

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
        seekTo (juce::jlimit (0.0, getLength(), getPosition() + seconds));
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

    const auto positionMs = (int) std::round (getPosition() * 1000.0);

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
    const auto position = getPosition() * 1000.0;
    if (session.loopEnabled && (position < session.loopStartMs || position > session.loopEndMs))
        seekTo (session.loopStartMs / 1000.0);
}

void SessionScreen::updateLoop()
{
    engine.setLoop (session.loopStartMs / 1000.0, session.loopEndMs / 1000.0, session.loopEnabled);

    if (isYouTube())   // YouTube ではページの中でループする (50ms ごとに確認するので、数十 ms の誤差がある)
        youtube.setLoop (session.loopStartMs / 1000.0, session.loopEndMs / 1000.0, session.loopEnabled);

    loopButton.setToggleState (session.loopEnabled, juce::dontSendNotification);
    loopStartButton.setButtonText ("A " + formatTime (session.loopStartMs / 1000.0));
    loopEndButton.setButtonText ("B " + formatTime (session.loopEndMs / 1000.0));
}

void SessionScreen::changeOffset (int deltaMs)
{
    session.syncOffsetMs = juce::jlimit (-5000, 5000, session.syncOffsetMs + deltaMs);
    engine.setSyncOffsetSeconds (session.syncOffsetMs / 1000.0);
    offsetValueLabel.setText ((session.syncOffsetMs > 0 ? "+" : "") + juce::String (session.syncOffsetMs) + " ms",
                              juce::dontSendNotification);
}

void SessionScreen::toggleMetronome()
{
    metronomeOn = ! metronomeOn;
    engine.setMetronomeEnabled (metronomeOn);
    metronomeButton.setToggleState (metronomeOn, juce::dontSendNotification);

    if (! metronomeOn && engine.isMetronomeSolo())
        toggleMetronomeSolo();   // オフにしたら、単体で鳴らすのも止める

    // 基本は、再生中に曲の拍に合わせて鳴る。止まっているときは「単体」で鳴らせる
    if (metronomeOn && ! isTransportPlaying())
        setStatus ("メトロノーム: 再生すると曲に合わせて鳴ります。「単体」でメトロノームだけ鳴らせます"_jp);
}

void SessionScreen::toggleMetronomeSolo()
{
    const auto solo = ! engine.isMetronomeSolo();

    if (solo && ! metronomeOn)
        toggleMetronome();   // 単体で鳴らすときは、メトロノーム自体もオンにする

    engine.setMetronomeSolo (solo);
    metronomeSoloButton.setToggleState (solo, juce::dontSendNotification);
    setStatus (solo ? "メトロノームだけを鳴らしています。再生すると曲に合わせます"_jp : juce::String());
}

void SessionScreen::toggleTuner()
{
    if (tunerPanel != nullptr)
    {
        tunerPanel = nullptr;
        tunerButton.setToggleState (false, juce::dontSendNotification);
        resized();   // 隠していた動画とタブ譜を戻す
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
    const auto position = getPosition();
    const auto length = getLength();
    const auto playing = isTransportPlaying();

    if (playing)
        playButton.setFixedText (FixedTextId::Stop, "■ 停止"_jp);
    else
        playButton.setFixedText (FixedTextId::Play, "▶ 再生"_jp);

    // 再生を始めたら、メトロノームは「単体」から曲に合わせる方へ切り替える (基本は音源に同期)
    if (playing && ! wasPlaying && engine.isMetronomeSolo())
        toggleMetronomeSolo();
    wasPlaying = playing;

    // 小節・拍 (一定テンポ + ずらし。設計書 3.1)
    const auto beats = (position - session.syncOffsetMs / 1000.0) * session.bpm / 60.0;
    const auto bar = beats >= 0.0 ? (int) std::floor (beats / 4.0) + 1 : 0;
    const auto beat = beats >= 0.0 ? (int) std::floor (beats) % 4 + 1 : 0;
    positionLabel.setText ("小節 "_jp + juce::String (bar) + "   拍 "_jp + juce::String (beat), juce::dontSendNotification);
    scoreView.setBarAndBeat (bar, beat);
    scoreViewer.setPosition (position, session.bpm, session.syncOffsetMs / 1000.0);

    timeline.setState (position, length, session.loopStartMs / 1000.0, session.loopEndMs / 1000.0, session.loopEnabled);

    const auto dirty = isDirty();
    saveButton.setFixedText (FixedTextId::Save, dirty ? "保存 *"_jp : "保存"_jp);
    saveButton.setMarked (dirty);   // デスメタルモードでは * の代わりに印を出す
}

void SessionScreen::changeListenerCallback (juce::ChangeBroadcaster*)
{
    applyAppSettings();
}

// 設定画面の値 (戻る・進むの秒数、ホイール1目盛りの量) を反映する
void SessionScreen::applyAppSettings()
{
    const auto seconds = juce::String (settings.getSkipSeconds());
    rewindButton.setButtonText (juce::String::fromUTF8 ("◀◀ ") + seconds + "秒"_jp);
    forwardButton.setButtonText (seconds + "秒 "_jp + juce::String::fromUTF8 ("▶▶"));
    rewindButton.setTooltip (seconds + " 秒戻る (←キー)。秒数は設定で変えられます"_jp);
    forwardButton.setTooltip (seconds + " 秒進む (→キー)。秒数は設定で変えられます"_jp);

    const auto& steps = settings.getWheelSteps();
    speedSlider.wheelStep = steps.speedPercent;
    bpmSlider.wheelStep = steps.bpm;
    for (auto* slider : { &sourceGainSlider, &guitarGainSlider, &metronomeGainSlider })
        slider->wheelStep = steps.volumePercent / 100.0;

    const auto offsetStep = juce::String (steps.offsetMs);
    offsetDownButton.setButtonText (juce::String::fromUTF8 ("◀ -") + offsetStep + " ms");
    offsetUpButton.setButtonText ("+" + offsetStep + juce::String::fromUTF8 (" ms ▶"));

    sidePanelButton.setButtonText (juce::String::fromUTF8 (sidePanelVisible ? "»" : "«"));
}

void SessionScreen::lookAndFeelChanged()
{
    // テーマが変わったら、補助の文字の色を取り直し、タブ譜の色も合わせる
    for (auto* label : { &statusLabel, &sourceInfoLabel, &scoreNameLabel })
        label->setColour (juce::Label::textColourId, findColour (ThemeColourIds::textMuted));

    scoreViewer.setColours (theme.getPalette());
    repaint();
}

void SessionScreen::toggleSidePanel()
{
    sidePanelVisible = ! sidePanelVisible;
    sidePanelButton.setButtonText (juce::String::fromUTF8 (sidePanelVisible ? "»" : "«"));
    resized();
}

//==============================================================================
void SessionScreen::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::ResizableWindow::backgroundColourId));

    // 右のパネルの区切りごとに面を描く (影は使わない)
    for (const auto& card : sideCards)
    {
        g.setColour (findColour (ThemeColourIds::panel));
        g.fillRoundedRectangle (card.toFloat(), 8.0f);
        g.setColour (findColour (ThemeColourIds::border));
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }

    // 下の再生操作の帯
    auto bottom = getLocalBounds().reduced (12).removeFromBottom (64).toFloat();
    g.setColour (findColour (ThemeColourIds::panel));
    g.fillRoundedRectangle (bottom, 10.0f);
    g.setColour (findColour (ThemeColourIds::border));
    g.drawRoundedRectangle (bottom.reduced (0.5f), 10.0f, 1.0f);
}

void SessionScreen::placeWebViews()
{
    // WebView (動画・タブ譜) は、ほかの部品より手前に描かれる。
    // チューナーを開いている間と、右のパネルを隠しているとき (動画) は隠す
    const bool canShow = tunerPanel == nullptr && getParentComponent() != nullptr && ! getBounds().isEmpty();

    if (isYouTube() && canShow && sidePanelVisible)
        youtube.placeOver (videoView);
    else
        youtube.hide();

    if (session.scorePath.isNotEmpty() && juce::File (session.scorePath).existsAsFile() && canShow)
        scoreViewer.placeOver (scoreView);
    else
        scoreViewer.hide();
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

    //==== 下部 (帯の中に並べる) ====
    auto bottom = area.removeFromBottom (64).reduced (8);
    area.removeFromBottom (12);

    tunerButton.setBounds (bottom.removeFromRight (110));
    bottom.removeFromRight (12);

    // 速度と BPM のスライダーは、残りの幅を分け合う (ウィンドウが狭くても、ほかのボタンを潰さない)
    constexpr int gap = 6;
    constexpr int fixedWidth = 76 + 112 + 76 + 10 + 52 + 36 + 70 + 104 + 52 + 10 + 78 + 78 + 70 + gap * 14;
    const auto sliderWidth = juce::jlimit (90, 240, (bottom.getWidth() - fixedWidth) / 2);

    const auto place = [&bottom] (juce::Component& c, int width)
    {
        c.setBounds (bottom.removeFromLeft (width));
        bottom.removeFromLeft (gap);
    };

    place (rewindButton, 76);
    place (playButton, 112);
    place (forwardButton, 76);
    bottom.removeFromLeft (10);
    place (speedLabel, 52);
    place (speedSlider, sliderWidth);
    place (bpmLabel, 36);
    place (bpmSlider, sliderWidth);
    place (tapButton, 70);
    place (metronomeButton, 104);
    place (metronomeSoloButton, 52);
    bottom.removeFromLeft (10);
    place (loopStartButton, 78);
    place (loopEndButton, 78);
    place (loopButton, 70);

    //==== 右のパネルと境目 ====
    sideCards.clear();
    const auto panelWidth = juce::jmin (settings.getSidePanelWidth(), area.getWidth() / 2);

    for (auto* c : std::initializer_list<juce::Component*> {
             &sourceHeading, &toneHeading, &mixerHeading, &offsetHeading, &splitter,
             &videoView, &sourceInfoLabel, &sourceRemoveButton, &chooseFileButton, &pasteUrlButton,
             &toneButtons[0], &toneButtons[1], &toneButtons[2],
             &sourceGainLabel, &guitarGainLabel, &metronomeGainLabel,
             &sourceGainSlider, &guitarGainSlider, &metronomeGainSlider,
             &offsetDownButton, &offsetValueLabel, &offsetUpButton })
        c->setVisible (sidePanelVisible);

    if (sidePanelVisible)
    {
        auto right = area.removeFromRight (panelWidth);
        auto bar = area.removeFromRight (16);
        splitter.setBounds (bar);
        sidePanelButton.setBounds (bar.withSizeKeepingCentre (16, 64));

        const auto card = [this, &right] (int height)
        {
            auto c = right.removeFromTop (height);
            sideCards.push_back (c);
            right.removeFromTop (10);
            return c.reduced (12, 10);
        };

        // 音源: 見出し、動画 (16:9)、その下に URL かファイル名と、外すボタン
        const auto videoHeight = juce::jmin (juce::roundToInt ((float) (panelWidth - 24) * 9.0f / 16.0f), 260);
        auto source = card (24 + videoHeight + 34 + (hasSource() ? 0 : 40));
        sourceHeading.setBounds (source.removeFromTop (20));
        source.removeFromTop (4);
        videoView.setBounds (source.removeFromTop (videoHeight));

        chooseFileButton.setVisible (! hasSource());
        pasteUrlButton.setVisible (! hasSource());
        sourceInfoLabel.setVisible (hasSource());
        sourceRemoveButton.setVisible (hasSource());

        if (hasSource())
        {
            auto row = source.removeFromTop (30).withTrimmedTop (4);
            sourceRemoveButton.setBounds (row.removeFromRight (26));
            sourceInfoLabel.setBounds (row);
        }
        else
        {
            source.removeFromTop (8);
            auto row = source.removeFromTop (34);
            chooseFileButton.setBounds (row.removeFromLeft ((row.getWidth() - 8) / 2));
            row.removeFromLeft (8);
            pasteUrlButton.setBounds (row);
        }

        // 音色
        auto tone = card (24 + 40);
        toneHeading.setBounds (tone.removeFromTop (20));
        tone.removeFromTop (4);
        const auto toneWidth = (tone.getWidth() - 12) / 3;
        for (auto& button : toneButtons)
        {
            button.setBounds (tone.removeFromLeft (toneWidth));
            tone.removeFromLeft (6);
        }

        // ミキサー
        auto mixer = card (24 + 3 * 34);
        mixerHeading.setBounds (mixer.removeFromTop (20));
        mixer.removeFromTop (4);
        for (auto [label, slider] : { std::pair<juce::Label*, juce::Slider*> { &sourceGainLabel, &sourceGainSlider },
                                      { &guitarGainLabel, &guitarGainSlider },
                                      { &metronomeGainLabel, &metronomeGainSlider } })
        {
            auto row = mixer.removeFromTop (34);
            label->setBounds (row.removeFromLeft (96));
            slider->setBounds (row);
        }

        // ずらし (左右のボタン)
        auto offset = card (24 + 40);
        offsetHeading.setBounds (offset.removeFromTop (20));
        offset.removeFromTop (4);
        offsetDownButton.setBounds (offset.removeFromLeft (100));
        offsetUpButton.setBounds (offset.removeFromRight (100));
        offsetValueLabel.setBounds (offset);
    }
    else
    {
        sidePanelButton.setBounds (area.removeFromRight (16).withSizeKeepingCentre (16, 64));
        area.removeFromRight (6);
    }

    //==== 左: タブ譜の見出し、タブ譜、再生位置バー ====
    auto scoreHeader = area.removeFromTop (34);
    scoreRemoveButton.setBounds (scoreHeader.removeFromRight (60).reduced (0, 2));
    scoreHeader.removeFromRight (6);
    scoreChooseButton.setBounds (scoreHeader.removeFromRight (130).reduced (0, 2));
    scoreHeader.removeFromRight (12);
    positionLabel.setBounds (scoreHeader.removeFromRight (180));
    scoreNameLabel.setBounds (scoreHeader);
    area.removeFromTop (6);

    timeline.setBounds (area.removeFromBottom (44));
    area.removeFromBottom (8);
    scoreView.setBounds (area);

    if (tunerPanel != nullptr)
        tunerPanel->setBounds (getLocalBounds().withSizeKeepingCentre (TunerPanel::preferredWidth, TunerPanel::preferredHeight));

    placeWebViews();
    repaint();
}
