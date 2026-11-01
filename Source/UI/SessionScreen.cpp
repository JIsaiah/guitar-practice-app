#include "SessionScreen.h"
#include "../Model/TonePresets.h"
#include "../Model/YouTubeUrl.h"
#include "../Text.h"

namespace
{
    void setUpSlider (juce::Slider& slider, double min, double max, double interval, double value)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 24);
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
      tapButton (t), tunerButton (t), playButton (t), metronomeButton (t), loopButton (t)
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

    // 曲名: ダブルクリックで変更できる (Enter で確定、Esc で取り消し)
    titleLabel.setText (session.title.isEmpty() ? "(無題)"_jp : session.title, juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    titleLabel.setEditable (false, true, false);
    titleLabel.setTooltip ("ダブルクリックで曲名を変更できます"_jp);
    titleLabel.onTextChange = [this] { renameSession (titleLabel.getText()); };
    titleLabel.onEditorHide = [this] { grabKeyboardFocus(); };
    statusLabel.setJustificationType (juce::Justification::centredRight);

    //==== 左: タブ譜 ====
    scoreNameLabel.setFont (juce::FontOptions (14.0f));
    positionLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    positionLabel.setJustificationType (juce::Justification::centredRight);
    scoreChooseButton.setButtonText ("タブ譜を選ぶ…"_jp);
    scoreRemoveButton.setButtonText ("外す"_jp);
    scoreChooseButton.onClick = [this] { chooseScoreFile(); };
    scoreRemoveButton.onClick = [this] { removeScore(); };
    swapButton.setButtonText (juce::String::fromUTF8 ("⇄ ") + "動画と入れ替え"_jp);
    swapButton.setTooltip ("タブ譜と動画の場所を入れ替えます (タブ譜つきの動画で練習するとき)"_jp);
    swapButton.onClick = [this] { toggleSwap(); };

    // 小節を同じ幅で並べる (アプリ全体の好みとして保存する)
    equalBarsButton.setButtonText ("小節を同じ幅に"_jp);
    equalBarsButton.setTooltip ("タブ譜の小節を、音符の数に関係なく同じ幅で並べます"_jp);
    equalBarsButton.setToggleState (settings.getScoreEqualBars(), juce::dontSendNotification);
    equalBarsButton.onClick = [this]
    {
        const auto equal = ! settings.getScoreEqualBars();
        settings.setScoreEqualBars (equal);
        equalBarsButton.setToggleState (equal, juce::dontSendNotification);
        scoreViewer.setEqualBarWidths (equal);
    };
    scoreViewer.setEqualBarWidths (settings.getScoreEqualBars());

    // タブ譜の小節をクリックしたら、その小節の頭から再生する
    scoreViewer.onBarClicked = [this] (double barStartBeats) { seekToBar (barStartBeats); };
    scoreViewer.onKey = [this] (const juce::String& key) { handlePageKey (key); };
    youtube.onKey = [this] (const juce::String& key) { handlePageKey (key); };
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

    //==== 右: 音色 (簡易な音色 3 つと、NAM) ====
    const auto& presets = getTonePresets();
    for (size_t i = 0; i < toneButtons.size(); ++i)
    {
        auto& button = toneButtons[i];
        button.setButtonText (i == namToneIndex ? juce::String ("NAM") : juce::String::fromUTF8 (presets[i].name));
        button.setClickingTogglesState (true);
        button.setRadioGroupId (1);
        button.onClick = [this, i] { selectTone ((int) i, true); };
    }
    toneButtons[namToneIndex].setTooltip ("NAM のアンプのモデル (.nam) とキャビネットの IR (.wav) で鳴らします"_jp);

    toneSummaryLabel.setFont (juce::FontOptions (12.0f));
    toneDetailButton.setButtonText ("音色の設定…"_jp);
    toneDetailButton.setTooltip ("NAM のモデル・IR・入力の大きさ・メーター・オーディオデバイスの状態"_jp);
    toneDetailButton.onClick = [this] { toggleTonePanel(); };

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
        applySourceGain();
    };

    // 音源だけのミュート。タブ譜のカーソルとメトロノームは動いたまま、音源の音だけを消す
    sourceMuteButton.setButtonText ("ミュート"_jp);
    sourceMuteButton.setClickingTogglesState (false);
    sourceMuteButton.setTooltip ("音源の音だけを消します (タブ譜とメトロノームはそのまま動きます)"_jp);
    sourceMuteButton.onClick = [this] { toggleSourceMute(); };
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

    // ダブルクリックで、この曲に保存されている音量に戻す (BPM と同じ)
    for (auto* slider : { &sourceGainSlider, &guitarGainSlider, &metronomeGainSlider })
        slider->setTooltip ("ダブルクリックで、保存されている音量に戻す"_jp);
    updateMixerDefaults();

    //==== 右: テンポ (BPM) とずらし ====
    bpmLabel.setText ("BPM", juce::dontSendNotification);
    scoreOffsetLabel.setText ("タブ譜だけ"_jp, juce::dontSendNotification);
    scoreOffsetLabel.setTooltip ("タブ譜のカーソルだけをずらします (メトロノームと音源は合っているのに、タブ譜がずれるとき)"_jp);
    offsetLabel.setText ("拍の位置"_jp, juce::dontSendNotification);
    offsetLabel.setTooltip ("メトロノームとタブ譜を、音源に対してずらします"_jp);
    tapButton.setFixedText (FixedTextId::Tap, "TAP");
    tapButton.setTooltip ("曲に合わせて数回押すと、BPM になります (Enter キーでも押せます)"_jp);
    tapButton.onClick = [this] { perform (Command::tapTempo); };

    //==== 右: ずらし (左右のボタン) ====
    // ボタンは押し続けると連続で変わる。値をクリックすると数値を入力できる (OffsetControl)
    offsetControl.onChange = [this] (int ms) { setSyncOffset (ms); };
    offsetControl.setTooltips ("メトロノームとタブ譜を早める"_jp, "メトロノームとタブ譜を遅らせる"_jp);
    scoreOffsetControl.onChange = [this] (int ms) { setScoreOffset (ms); };
    scoreOffsetControl.setTooltips ("タブ譜のカーソルを早める"_jp, "タブ譜のカーソルを遅らせる"_jp);
    for (auto* control : { &offsetControl, &scoreOffsetControl })
        control->onEditFinished = [this] { grabKeyboardFocus(); };

    //==== 下部 ====
    playButton.setFixedText (FixedTextId::Play, "▶ 再生"_jp);
    metronomeButton.setFixedText (FixedTextId::Metronome, "曲に合わせる"_jp);
    metronomeButton.setTooltip ("オンにすると、再生中に曲の拍に合わせて鳴ります"_jp);
    metronomeSoloButton.setButtonText ("単体で鳴らす"_jp);
    metronomeSoloButton.setTooltip ("曲を再生せずに、メトロノームだけを鳴らします。再生を始めると、曲に合わせる方に切り替わります"_jp);
    loopStartButton.setButtonText ("A");
    loopEndButton.setButtonText ("B");
    loopStartButton.setTooltip ("今の位置にいちばん近い小節の頭を、くり返しの始まり (A) にします"_jp);
    loopEndButton.setTooltip ("今弾いている小節を、くり返しの最後 (B) にします。その小節の終わりまで弾いてから A に戻ります"_jp);
    loopButton.setFixedText (FixedTextId::Loop, "くり返す"_jp);
    tunerButton.setFixedText (FixedTextId::Tuner, "チューナー"_jp);

    toStartButton.setButtonText (juce::String::fromUTF8 ("|◀ ") + "最初へ"_jp);
    toStartButton.setTooltip ("曲の頭に戻る (Home キー)"_jp);
    toStartButton.onClick = [this] { perform (Command::goToStart); };
    rewindButton.onClick = [this] { perform (Command::rewind); };
    playButton.onClick = [this] { perform (Command::togglePlay); };
    forwardButton.onClick = [this] { perform (Command::fastForward); };
    metronomeButton.onClick = [this] { perform (Command::toggleMetronome); };
    metronomeSoloButton.onClick = [this] { toggleMetronomeSolo(); };
    loopStartButton.onClick = [this] { perform (Command::setLoopStart); };
    loopEndButton.onClick = [this] { perform (Command::setLoopEnd); };
    loopButton.onClick = [this] { perform (Command::toggleLoop); };
    tunerButton.onClick = [this] { perform (Command::toggleTuner); };

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
             &scoreNameLabel, &positionLabel, &scoreChooseButton, &scoreRemoveButton, &swapButton, &scoreView, &timeline,
             &splitter, &sidePanelButton,
             &sourceHeading, &toneHeading, &mixerHeading, &offsetHeading,
             &videoView, &sourceInfoLabel, &sourceRemoveButton, &chooseFileButton, &pasteUrlButton,
             &toneButtons[0], &toneButtons[1], &toneButtons[2], &toneButtons[3], &toneSummaryLabel, &toneDetailButton,
             &sourceGainLabel, &guitarGainLabel, &metronomeGainLabel,
             &sourceGainSlider, &guitarGainSlider, &metronomeGainSlider, &sourceMuteButton,
             &bpmLabel, &bpmSlider, &tapButton, &scoreOffsetLabel, &scoreOffsetControl, &offsetLabel, &offsetControl,
             &tunerButton,
             &playHeading, &speedHeading, &metronomeHeading, &loopHeading,
             &toStartButton, &rewindButton, &playButton, &forwardButton, &speedSlider, &equalBarsButton,
             &metronomeButton, &metronomeSoloButton,
             &loopStartButton, &loopEndButton, &loopButton })
    {
        addAndMakeVisible (c);

        // スペースキーや Enter キーを、押したボタンではなくこの画面で受け取るため
        c->setMouseClickGrabsKeyboardFocus (false);
        c->setWantsKeyboardFocus (false);
    }

    setWantsKeyboardFocus (true);

    // URL の欄は、ダブルクリックで文字を入力できるようにする (上でキーボードを受け取らない設定にしたため戻す)
    sourceInfoLabel.setWantsKeyboardFocus (true);

    // チューナーの後ろを暗くする幕 (チューナーを開いたときだけ表示)。クリックするとチューナーを閉じる
    dimOverlay.onClick = [this]
    {
        if (tunerPanel != nullptr) perform (Command::toggleTuner);
        if (tonePanel != nullptr)  toggleTonePanel();
    };
    addChildComponent (dimOverlay);

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

    youtube.onKey = nullptr;
    scoreViewer.onLoaded = nullptr;
    scoreViewer.onError = nullptr;
    scoreViewer.onBarClicked = nullptr;
    scoreViewer.onKey = nullptr;
    scoreViewer.hide();

    engine.setPlaying (false);
    engine.setMetronomeEnabled (false);
    engine.setMetronomeSolo (false);
    engine.clearSource();
}

//==============================================================================
void SessionScreen::applySessionToEngine()
{
    selectTone (session.tonePresetId == namTonePresetId ? namToneIndex : findTonePresetIndex (session.tonePresetId), false);
    engine.setInputGainDb ((float) session.inputGainDb);
    loadNamTone();
    applySourceGain();
    engine.setGuitarGain ((float) session.mixGuitar);
    engine.setMetronomeGain ((float) session.mixMetronome);
    engine.setSpeed (session.speedPercent / 100.0);
    engine.setBpm (session.bpm);
    engine.setMetronomeEnabled (false);
    setSyncOffset (session.syncOffsetMs);
    setScoreOffset (session.scoreOffsetMs);
    updateLoop();
    updateSwapUi();
}

void SessionScreen::setTransportEnabled (bool enabled)
{
    for (auto* button : std::initializer_list<juce::Button*> { &toStartButton, &rewindButton, &playButton, &forwardButton,
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
    applySourceGain();
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
        applySourceGain();
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

    updateSwapUi();
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
        updateLoop();          // タブ譜の拍子が分かったので、A・B の小節の番号を出し直す
        updateSkipButtons();   // 小節が分かったので、戻る・進むを小節単位にする
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

        // 別の曲のタブ譜なので、前のループ (A・B) は使えない。0 に戻す
        safe->session.loopStartMs = safe->session.loopEndMs = 0;
        safe->session.loopEnabled = false;
        safe->updateLoop();

        safe->loadScore();

        // 新しく選んだタブ譜のテンポを、BPM にする (設計書 #11: BPM はタブ譜を優先)
        safe->scoreViewer.onLoaded = [safe] (double tempo)
        {
            if (safe == nullptr)
                return;

            safe->updateLoop();
            safe->updateSkipButtons();
            if (tempo <= 0.0)
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
    updateLoop();   // 4/4 の小節に戻るので、小節の番号を出し直す
    updateSkipButtons();
}

void SessionScreen::updateScoreUi()
{
    const juce::File file (session.scorePath);
    scoreNameLabel.setText ("タブ譜: "_jp + (session.scorePath.isEmpty() ? "なし"_jp : file.getFileName()),
                            juce::dontSendNotification);
    scoreRemoveButton.setEnabled (session.scorePath.isNotEmpty());

    if (session.scorePath.isNotEmpty() && ! file.existsAsFile())
        setStatus ("タブ譜のファイルが見つかりません: "_jp + session.scorePath);

    updateSwapUi();
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
    updateMixerDefaults();
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
        case Command::goToStart:       if (sourceReady) seekTo (0.0); break;
        // タブ譜モードでは小節単位、動画モード (とタブ譜がないとき) は秒単位
        case Command::rewind:          if (isBarMode()) skipBars (-1); else skip (-settings.getSkipSeconds()); break;
        case Command::fastForward:     if (isBarMode()) skipBars (1);  else skip (settings.getSkipSeconds()); break;
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
    if (key == juce::KeyPress::homeKey)   { perform (Command::goToStart); return true; }
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

// タブ譜モード: タブ譜を左の大きな枠に表示していて、タブ譜の小節が分かるとき
// (動画と入れ替えているとき、タブ譜がないときは、動画モードとして秒単位で動かす)
bool SessionScreen::isBarMode() const
{
    return usesScoreBars() && ! isSwapped();
}

void SessionScreen::skipBars (int direction)
{
    if (! sourceReady)
        return;

    const auto position = getPosition();
    const auto current = barIndexAt (position);
    auto target = current + direction;

    // 戻るとき、小節の途中 (頭から 0.5 秒より後) にいたら、まずその小節の頭へ戻る (音楽プレイヤーの「前へ」と同じ)
    if (direction < 0 && position - barStartSeconds (current) > 0.5)
        target = current;

    seekTo (juce::jlimit (0.0, juce::jmax (0.0, getLength()), barStartSeconds (juce::jmax (0, target))));
}

// 戻る・進むのボタンの文字を、今のモード (小節単位／秒単位) に合わせる
void SessionScreen::updateSkipButtons()
{
    if (isBarMode())
    {
        rewindButton.setButtonText (juce::String::fromUTF8 ("◀◀ ") + "1小節"_jp);
        forwardButton.setButtonText ("1小節 "_jp + juce::String::fromUTF8 ("▶▶"));
        rewindButton.setTooltip ("前の小節の頭へ (←キー)。小節の途中なら、その小節の頭へ"_jp);
        forwardButton.setTooltip ("次の小節の頭へ (→キー)"_jp);
        playHeading.setText ("再生 (タブ譜モード: 小節単位)"_jp, juce::dontSendNotification);
    }
    else
    {
        const auto seconds = juce::String (settings.getSkipSeconds());
        rewindButton.setButtonText (juce::String::fromUTF8 ("◀◀ ") + seconds + "秒"_jp);
        forwardButton.setButtonText (seconds + "秒 "_jp + juce::String::fromUTF8 ("▶▶"));
        rewindButton.setTooltip (seconds + " 秒戻る (←キー)。秒数は設定で変えられます"_jp);
        forwardButton.setTooltip (seconds + " 秒進む (→キー)。秒数は設定で変えられます"_jp);
        playHeading.setText (isSwapped() ? "再生 (動画モード: 秒単位)"_jp : "再生"_jp, juce::dontSendNotification);
    }
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

    // A・B は小節の区切りに合わせる。押したタイミングのまま (ミリ秒単位) だと、
    // くり返しのつなぎ目に中途半端な間ができて、リズムを保ちにくいため
    //   A: 押した位置にいちばん近い小節の頭 (少し早めに押しても、次の小節の頭になる)
    //   B: 押した位置を含む小節の「終わり」(= 次の小節の頭)。4小節目の途中で押すと、4小節目の最後まで弾いてから A に戻る
    const auto position = getPosition();
    auto index = isStart ? barIndexNear (position) : barIndexAt (position) + 1;

    // B が A と同じか前になったら、A の小節の終わりにする (最低1小節はくり返す)
    const auto loopStartSeconds = session.loopStartMs / 1000.0;
    if (! isStart && barStartSeconds (index) <= loopStartSeconds + 0.001)
        index = barIndexNear (loopStartSeconds) + 1;

    const auto positionMs = juce::roundToInt (barStartSeconds (index) * 1000.0);

    if (isStart)
        session.loopStartMs = positionMs;
    else
        session.loopEndMs = positionMs;

    if (session.loopEndMs <= session.loopStartMs)
        session.loopEnabled = false;

    updateLoop();
}

bool SessionScreen::usesScoreBars() const
{
    return session.scorePath.isNotEmpty() && ! scoreViewer.getBarStartBeats().empty();
}

// 小節の頭の位置 (秒) = 小節の頭の拍 × 60 ÷ BPM + ずらし
//   タブ譜の小節: タブ譜のカーソルと同じずらし (拍の位置 + タブ譜だけ) を使う。画面のタブ譜と合うように
//   4/4 とみなすとき: メトロノームと同じずらし (拍の位置) を使う
double SessionScreen::barStartSeconds (int index) const
{
    index = juce::jmax (0, index);
    const auto& bars = scoreViewer.getBarStartBeats();

    double beats = index * 4.0;
    double offset = session.syncOffsetMs / 1000.0;

    if (usesScoreBars())
    {
        // タブ譜の最後の小節より後は、4拍ずつ足す
        const auto last = (int) bars.size() - 1;
        beats = index <= last ? bars[(size_t) index] : bars.back() + (index - last) * 4.0;
        offset = getScoreOffsetSeconds();
    }

    return juce::jmax (0.0, beats * 60.0 / juce::jmax (1.0, session.bpm) + offset);
}

int SessionScreen::barIndexNear (double seconds) const
{
    // 前後の小節の頭のうち、近いほう。小節の頭は単調に増えるので、順に見ていけばよい
    int index = 0;
    while (barStartSeconds (index + 1) <= seconds && index < 100000)
        ++index;

    if (seconds - barStartSeconds (index) > barStartSeconds (index + 1) - seconds)
        ++index;

    return index;
}

int SessionScreen::barIndexAt (double seconds) const
{
    // 小節の頭がその位置以前にある、最後の小節 (わずかな誤差で前の小節にならないよう 1ms の余裕)
    int index = 0;
    while (barStartSeconds (index + 1) <= seconds + 0.001 && index < 100000)
        ++index;

    return index;
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
    // 小節の番号で表示する (まだ決めていなければ「A」「B」だけ)。
    //   A: 始まりの小節 (A の位置から始まる小節)
    //   B: 最後まで弾く小節 (保存している B の位置は、その小節の終わり = 次の小節の頭なので、1つ前の小節)
    const bool unset = session.loopStartMs == 0 && session.loopEndMs == 0;
    const auto firstBar = barIndexNear (session.loopStartMs / 1000.0) + 1;
    const auto lastBar = juce::jmax (1, barIndexNear (session.loopEndMs / 1000.0));

    loopStartButton.setButtonText (unset ? juce::String ("A") : "A " + juce::String (firstBar) + "小節"_jp);
    loopEndButton.setButtonText (unset ? juce::String ("B") : "B " + juce::String (lastBar) + "小節"_jp);

    if (! unset && session.loopEndMs > session.loopStartMs)
        loopButton.setTooltip (juce::String (firstBar) + "〜"_jp + juce::String (lastBar) + " 小節目をくり返します"_jp);
}

//==============================================================================
// ずらし
//   拍の位置 (syncOffsetMs): メトロノームとタブ譜を、音源に対してずらす (エンジンにも送る)
//   タブ譜だけ (scoreOffsetMs): タブ譜のカーソルだけを、さらにずらす (エンジンには送らない)
// タブ譜のカーソルは「再生位置 − (拍の位置 + タブ譜だけ)」で動く (timerCallback)
void SessionScreen::setSyncOffset (int ms)
{
    session.syncOffsetMs = juce::jlimit (OffsetControl::minMs, OffsetControl::maxMs, ms);
    engine.setSyncOffsetSeconds (session.syncOffsetMs / 1000.0);
    offsetControl.setValue (session.syncOffsetMs);
}

void SessionScreen::setScoreOffset (int ms)
{
    session.scoreOffsetMs = juce::jlimit (OffsetControl::minMs, OffsetControl::maxMs, ms);
    scoreOffsetControl.setValue (session.scoreOffsetMs);
}

// タブ譜の小節をクリックしたとき: その小節の頭から再生する
void SessionScreen::seekToBar (double barStartBeats)
{
    if (! sourceReady || session.bpm <= 0.0)
        return;

    // カーソルの式 (拍 = (位置 − ずらし) × BPM ÷ 60) を逆にして、小節の頭の位置 (秒) を求める
    const auto seconds = barStartBeats * 60.0 / session.bpm + getScoreOffsetSeconds();
    seekTo (juce::jlimit (0.0, juce::jmax (0.0, getLength()), seconds));
    grabKeyboardFocus();   // タブ譜 (WebView) に移ったキー操作を、練習画面に戻す
}

// WebView (タブ譜・動画のページ) の上で押されたキー。練習画面で押したのと同じにする
void SessionScreen::handlePageKey (const juce::String& key)
{
    if (key == "Space")           perform (Command::togglePlay);
    else if (key == "ArrowLeft")  perform (Command::rewind);
    else if (key == "Home")       perform (Command::goToStart);
    else if (key == "ArrowRight") perform (Command::fastForward);
    else if (key == "Enter")      perform (Command::tapTempo);

    grabKeyboardFocus();
}

//==============================================================================
// タブ譜と動画の入れ替え (YouTube のときだけ)
void SessionScreen::toggleSwap()
{
    if (! isYouTube())
        return;

    session.videoInMainArea = ! session.videoInMainArea;
    updateSourceUi();
}

void SessionScreen::updateSwapUi()
{
    swapButton.setEnabled (isYouTube());
    swapButton.setToggleState (isSwapped(), juce::dontSendNotification);
    swapButton.setTooltip (isYouTube() ? "タブ譜と動画の場所を入れ替えます (タブ譜つきの動画で練習するとき)"_jp
                                       : "YouTube の音源のときに使えます"_jp);
    sourceHeading.setText (isSwapped() ? "タブ譜 (動画と入れ替え中)"_jp : "音源"_jp, juce::dontSendNotification);
    updateSkipButtons();

    if (isSwapped())
        videoView.setText (session.scorePath.isEmpty() ? "タブ譜がありません"_jp : juce::String());
}

//==============================================================================
void SessionScreen::updateMixerDefaults()
{
    sourceGainSlider.setDoubleClickReturnValue (true, session.mixSource);
    guitarGainSlider.setDoubleClickReturnValue (true, session.mixGuitar);
    metronomeGainSlider.setDoubleClickReturnValue (true, session.mixMetronome);
}

void SessionScreen::renameSession (const juce::String& newTitle)
{
    const auto title = newTitle.trim();

    if (title.isEmpty())
    {
        // 空の名前にはしない。元に戻す
        titleLabel.setText (session.title.isEmpty() ? "(無題)"_jp : session.title, juce::dontSendNotification);
        return;
    }

    session.title = title;   // 保存するまでは「未保存の変更」
    titleLabel.setText (title, juce::dontSendNotification);
}

void SessionScreen::toggleSourceMute()
{
    sourceMuted = ! sourceMuted;
    sourceMuteButton.setToggleState (sourceMuted, juce::dontSendNotification);
    applySourceGain();
    setStatus (sourceMuted ? "音源をミュートしました (タブ譜とメトロノームはそのまま動きます)"_jp : juce::String());
}

void SessionScreen::applySourceGain()
{
    // ミュートしても、スライダーの値 (session.mixSource) は変えない。解除すると元の音量に戻る
    const auto gain = sourceMuted ? 0.0 : session.mixSource;
    engine.setSourceGain ((float) gain);

    if (isYouTube())   // YouTube の音はアプリのミキサーを通らないので、プレイヤーの音量で合わせる
        youtube.setVolume (gain);

    sourceGainSlider.setAlpha (sourceMuted ? 0.5f : 1.0f);
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
        showOverlay (nullptr);
        tunerButton.setToggleState (false, juce::dontSendNotification);
        resized();   // 隠していた動画とタブ譜を戻す
        return;
    }

    if (tonePanel != nullptr)
        toggleTonePanel();   // 重ねるパネルは1つだけ

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
    showOverlay (tunerPanel.get());
    tunerButton.setToggleState (true, juce::dontSendNotification);
    resized();
}

// 後ろの画面を少し暗くして、目が重ねたパネルに行くようにする。panel が nullptr なら幕を外す
void SessionScreen::showOverlay (juce::Component* panel)
{
    dimOverlay.setBounds (getLocalBounds());
    dimOverlay.setVisible (panel != nullptr);

    if (panel != nullptr)
    {
        dimOverlay.toFront (false);
        addAndMakeVisible (*panel);
        panel->toFront (false);
    }
}

//==============================================================================
// 音色
void SessionScreen::selectTone (int index, bool fromUser)
{
    index = juce::jlimit (0, namToneIndex, index);
    toneButtons[(size_t) index].setToggleState (true, juce::dontSendNotification);

    if (index == namToneIndex)
    {
        session.tonePresetId = namTonePresetId;
        engine.setTone (ToneType::nam);

        // まだモデルを選んでいなければ、音色の設定を開いて選んでもらう
        if (fromUser && session.namModelPath.isEmpty() && session.irPath.isEmpty() && tonePanel == nullptr)
            toggleTonePanel();
    }
    else
    {
        const auto& preset = getTonePresets()[(size_t) index];
        session.tonePresetId = preset.id;
        engine.setTone (preset.type);
    }

    updateToneSummary();
}

void SessionScreen::loadNamTone()
{
    const auto toFile = [] (const juce::String& path) { return path.isEmpty() ? juce::File() : juce::File (path); };
    const auto amp = toFile (session.namModelPath);
    const auto cabinet = toFile (session.irPath);

    for (const auto& file : { amp, cabinet })
        if (file != juce::File() && ! file.existsAsFile())
            setStatus ("音色のファイルが見つかりません: "_jp + file.getFullPathName());

    engine.loadNamToneAsync (amp.existsAsFile() ? amp : juce::File(),
                             cabinet.existsAsFile() ? cabinet : juce::File(),
                             [safe = juce::Component::SafePointer (this)] (bool succeeded, const juce::String& error)
    {
        if (safe == nullptr)
            return;

        if (! succeeded)
            safe->setStatus (error);

        safe->updateToneSummary();
    });

    updateToneSummary();
}

void SessionScreen::updateToneSummary()
{
    const auto name = [] (const juce::String& path)
    {
        return path.isEmpty() ? "なし"_jp : juce::File (path).getFileNameWithoutExtension();
    };

    const auto text = "NAM: "_jp + name (session.namModelPath) + "  /  IR: " + name (session.irPath);
    toneSummaryLabel.setText (text, juce::dontSendNotification);
    toneSummaryLabel.setTooltip (text);

    // NAM を選んでいるときだけ、はっきり表示する
    toneSummaryLabel.setAlpha (session.tonePresetId == namTonePresetId ? 1.0f : 0.5f);
}

void SessionScreen::toggleTonePanel()
{
    if (tonePanel != nullptr)
    {
        tonePanel = nullptr;
        showOverlay (nullptr);
        toneDetailButton.setToggleState (false, juce::dontSendNotification);
        resized();
        grabKeyboardFocus();
        return;
    }

    if (tunerPanel != nullptr)
        toggleTuner();   // 重ねるパネルは1つだけ

    tonePanel = std::make_unique<ToneDetailPanel> (engine, session);
    tonePanel->onToneFilesChanged = [this]
    {
        // モデルや IR を選んだら、NAM の音色に切り替えて読み込む
        selectTone (namToneIndex, false);
        loadNamTone();
    };
    tonePanel->onOpenAudioSettings = [this] { if (onOpenSettings != nullptr) onOpenSettings(); };
    tonePanel->onClose = [this]
    {
        // 自分のボタンの onClick の中で破棄しないよう、後で閉じる
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
        {
            if (safe != nullptr && safe->tonePanel != nullptr)
                safe->toggleTonePanel();
        });
    };

    showOverlay (tonePanel.get());
    toneDetailButton.setToggleState (true, juce::dontSendNotification);
    resized();
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
    scoreViewer.setPosition (position, session.bpm, getScoreOffsetSeconds());   // タブ譜は「拍の位置」+「タブ譜だけ」

    timeline.setState (position, length, session.loopStartMs / 1000.0, session.loopEndMs / 1000.0, session.loopEnabled);

    const auto dirty = isDirty();
    saveButton.setFixedText (FixedTextId::Save, dirty ? "保存 *"_jp : "保存"_jp);
    saveButton.setMarked (dirty);   // デスメタルモードでは * の代わりに印を出す

    // キー操作 (Space・←→) を受け取れるようにしておく。
    // ダイアログを閉じたあとなどで、アプリのウィンドウにはフォーカスがあるのに、どの部品も持っていないときに取り戻す。
    // (WebView や入力欄がキーを持っているときは、ウィンドウ自体のフォーカスではないので、取り上げない)
    if (juce::Component::getCurrentlyFocusedComponent() == nullptr && isShowing())
        if (auto* peer = getPeer(); peer != nullptr && peer->isFocused())
            grabKeyboardFocus();
}

void SessionScreen::changeListenerCallback (juce::ChangeBroadcaster*)
{
    applyAppSettings();
}

// 設定画面の値 (戻る・進むの秒数、ホイール1目盛りの量) を反映する
void SessionScreen::applyAppSettings()
{
    updateSkipButtons();

    const auto& steps = settings.getWheelSteps();
    speedSlider.wheelStep = steps.speedPercent;
    bpmSlider.wheelStep = steps.bpm;
    for (auto* slider : { &sourceGainSlider, &guitarGainSlider, &metronomeGainSlider })
        slider->wheelStep = steps.volumePercent / 100.0;

    offsetControl.setStep (steps.offsetMs);
    scoreOffsetControl.setStep (steps.offsetMs);

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

    // 下の再生操作の帯と、グループの区切り線
    auto bottom = getLocalBounds().reduced (12).removeFromBottom (bottomBarHeight).toFloat();
    g.setColour (findColour (ThemeColourIds::panel));
    g.fillRoundedRectangle (bottom, 10.0f);
    g.setColour (findColour (ThemeColourIds::border));
    g.drawRoundedRectangle (bottom.reduced (0.5f), 10.0f, 1.0f);

    for (auto x : bottomDividers)
        g.fillRect ((float) x, bottom.getY() + 12.0f, 1.0f, bottom.getHeight() - 24.0f);
}

void SessionScreen::placeWebViews()
{
    // WebView (動画・タブ譜) は、ほかの部品より手前に描かれる。
    // チューナーを開いている間と、右のパネルを隠しているとき (動画) は隠す
    // 入れ替え中は、動画を左の大きな枠に、タブ譜を右のパネルの枠に重ねる
    const bool canShow = tunerPanel == nullptr && tonePanel == nullptr
                      && getParentComponent() != nullptr && ! getBounds().isEmpty();
    const bool swapped = isSwapped();
    auto& videoTarget = swapped ? static_cast<juce::Component&> (scoreView) : static_cast<juce::Component&> (videoView);
    auto& scoreTarget = swapped ? static_cast<juce::Component&> (videoView) : static_cast<juce::Component&> (scoreView);
    const bool videoTargetVisible = swapped || sidePanelVisible;
    const bool scoreTargetVisible = ! swapped || sidePanelVisible;

    if (isYouTube() && canShow && videoTargetVisible)
        youtube.placeOver (videoTarget);
    else
        youtube.hide();

    if (session.scorePath.isNotEmpty() && juce::File (session.scorePath).existsAsFile() && canShow && scoreTargetVisible)
        scoreViewer.placeOver (scoreTarget);
    else
        scoreViewer.hide();
}

void SessionScreen::resized()
{
    auto area = getLocalBounds().reduced (12);

    //==== 上部: 戻る、曲名、状態、チューナー、保存、詳細 ====
    auto top = area.removeFromTop (40);
    backButton.setBounds (top.removeFromLeft (100));
    top.removeFromLeft (12);
    settingsButton.setBounds (top.removeFromRight (100));
    top.removeFromRight (8);
    saveButton.setBounds (top.removeFromRight (90));
    top.removeFromRight (8);
    tunerButton.setBounds (top.removeFromRight (120));
    top.removeFromRight (12);
    statusLabel.setBounds (top.removeFromRight (top.getWidth() / 2));
    titleLabel.setBounds (top);

    area.removeFromTop (12);

    //==== 下部: 「再生／速度／メトロノーム／ループ」のグループ。それぞれ上に見出し、間に区切り線 ====
    auto bottom = area.removeFromBottom (bottomBarHeight).reduced (14, 8);
    area.removeFromBottom (12);
    bottomDividers.clear();

    constexpr int gap = 6, groupGap = 28;
    constexpr int playWidth = 84 + 84 + 100 + 84 + gap * 3;
    constexpr int metronomeWidth = 120 + 120 + gap;
    constexpr int loopWidth = 96 + 96 + 96 + gap * 2;
    const auto speedWidth = juce::jlimit (160, 360, bottom.getWidth() - playWidth - metronomeWidth - loopWidth - groupGap * 3);

    // 見出し (20px) と、その下に部品の行 (残り)
    const auto group = [&] (SectionHeading& heading, int width, bool divider)
    {
        auto g = bottom.removeFromLeft (width);
        heading.setBounds (g.removeFromTop (20));
        g.removeFromTop (2);

        if (divider)
        {
            bottomDividers.push_back (bottom.getX() + groupGap / 2);
            bottom.removeFromLeft (groupGap);
        }
        return g;
    };

    const auto place = [] (juce::Rectangle<int>& row, juce::Component& c, int width)
    {
        c.setBounds (row.removeFromLeft (width));
        row.removeFromLeft (gap);
    };

    auto play = group (playHeading, playWidth, true);
    place (play, toStartButton, 84);
    place (play, rewindButton, 84);
    place (play, playButton, 100);
    place (play, forwardButton, 84);

    auto speed = group (speedHeading, speedWidth, true);
    speedSlider.setBounds (speed);

    auto metronome = group (metronomeHeading, metronomeWidth, true);
    place (metronome, metronomeButton, 120);
    place (metronome, metronomeSoloButton, 120);

    auto loop = group (loopHeading, loopWidth, false);
    place (loop, loopStartButton, 96);
    place (loop, loopEndButton, 96);
    place (loop, loopButton, 96);

    //==== 右のパネルと境目 ====
    sideCards.clear();
    const auto panelWidth = juce::jmin (settings.getSidePanelWidth(), area.getWidth() / 2);

    for (auto* c : std::initializer_list<juce::Component*> {
             &sourceHeading, &toneHeading, &mixerHeading, &offsetHeading, &splitter,
             &videoView, &sourceInfoLabel, &sourceRemoveButton, &chooseFileButton, &pasteUrlButton,
             &toneButtons[0], &toneButtons[1], &toneButtons[2], &toneButtons[3], &toneSummaryLabel, &toneDetailButton,
             &sourceGainLabel, &guitarGainLabel, &metronomeGainLabel,
             &sourceGainSlider, &guitarGainSlider, &metronomeGainSlider, &sourceMuteButton,
             &bpmLabel, &bpmSlider, &tapButton, &scoreOffsetLabel, &scoreOffsetControl, &offsetLabel, &offsetControl })
        c->setVisible (sidePanelVisible);

    if (sidePanelVisible)
    {
        auto right = area.removeFromRight (panelWidth);
        auto bar = area.removeFromRight (16);
        splitter.setBounds (bar);
        sidePanelButton.setBounds (bar.withSizeKeepingCentre (16, 64));

        // height は中身の高さ。カードの内側の余白 (上下 8 ずつ) を足して切り取る
        const auto card = [this, &right] (int height)
        {
            auto c = right.removeFromTop (height + 16);
            sideCards.push_back (c);
            right.removeFromTop (8);
            return c.reduced (12, 8);
        };

        // 中身の高さ (動画以外)
        constexpr int headingHeight = 24, rowHeight = 32;
        constexpr int toneHeight = headingHeight + 36 + 4 + 28;   // 音色のボタンと、NAM のモデル名・設定ボタン
        constexpr int mixerHeight = headingHeight + 3 * rowHeight;
        constexpr int tempoHeight = headingHeight + 3 * rowHeight + 2 * 4;
        const int sourceExtra = headingHeight + 30 + (hasSource() ? 0 : 40);
        constexpr int cardChrome = 16 + 8;   // カードの内側の余白と、カードの間
        constexpr int labelWidth = 84;       // 行の名前の列。狭くして、スライダーを長くする

        // 動画は 16:9 で最大 260px。縦が足りないときは、ほかのカードが入るように小さくする
        const auto otherHeights = toneHeight + mixerHeight + tempoHeight + sourceExtra + cardChrome * 4;
        const auto videoHeight = juce::jlimit (90, 260, juce::jmin (juce::roundToInt ((float) (panelWidth - 24) * 9.0f / 16.0f),
                                                                    right.getHeight() - otherHeights));

        // 音源: 見出し、動画 (16:9)、その下に URL かファイル名と、外すボタン
        auto source = card (videoHeight + sourceExtra);
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
        auto tone = card (toneHeight);
        toneHeading.setBounds (tone.removeFromTop (20));
        tone.removeFromTop (4);
        auto toneRow = tone.removeFromTop (36);
        const auto toneWidth = (toneRow.getWidth() - 6 * (int) (toneButtons.size() - 1)) / (int) toneButtons.size();
        for (auto& button : toneButtons)
        {
            button.setBounds (toneRow.removeFromLeft (toneWidth));
            toneRow.removeFromLeft (6);
        }

        // NAM のモデル名と、音色の設定を開くボタン
        tone.removeFromTop (4);
        auto summaryRow = tone.removeFromTop (28);
        toneDetailButton.setBounds (summaryRow.removeFromRight (110).reduced (0, 1));
        summaryRow.removeFromRight (6);
        toneSummaryLabel.setBounds (summaryRow);

        // ミキサー。「音源」の行の右端にミュート (ほかの行も同じ幅を空けて、スライダーの長さを揃える)
        auto mixer = card (mixerHeight);
        mixerHeading.setBounds (mixer.removeFromTop (20));
        mixer.removeFromTop (4);
        for (auto [label, slider] : { std::pair<juce::Label*, juce::Slider*> { &sourceGainLabel, &sourceGainSlider },
                                      { &guitarGainLabel, &guitarGainSlider },
                                      { &metronomeGainLabel, &metronomeGainSlider } })
        {
            auto row = mixer.removeFromTop (rowHeight);
            label->setBounds (row.removeFromLeft (labelWidth));
            auto muteArea = row.removeFromRight (64);
            if (slider == &sourceGainSlider)
                sourceMuteButton.setBounds (muteArea.reduced (0, 3).withTrimmedLeft (6));
            slider->setBounds (row);
        }

        // テンポ: BPM と TAP、タブ譜だけのずらし、拍の位置のずらし
        auto tempo = card (tempoHeight);
        offsetHeading.setBounds (tempo.removeFromTop (20));
        tempo.removeFromTop (4);

        auto bpmRow = tempo.removeFromTop (rowHeight);
        bpmLabel.setBounds (bpmRow.removeFromLeft (labelWidth));
        tapButton.setBounds (bpmRow.removeFromRight (64).reduced (0, 3).withTrimmedLeft (6));
        bpmSlider.setBounds (bpmRow);

        for (auto [label, control] : { std::pair<juce::Label*, OffsetControl*> { &scoreOffsetLabel, &scoreOffsetControl },
                                       { &offsetLabel, &offsetControl } })
        {
            tempo.removeFromTop (4);
            auto row = tempo.removeFromTop (rowHeight);
            label->setBounds (row.removeFromLeft (labelWidth));
            control->setBounds (row.reduced (0, 1));
        }
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
    scoreHeader.removeFromRight (6);
    swapButton.setBounds (scoreHeader.removeFromRight (150).reduced (0, 2));
    scoreHeader.removeFromRight (6);
    equalBarsButton.setBounds (scoreHeader.removeFromRight (124).reduced (0, 2));
    scoreHeader.removeFromRight (12);
    positionLabel.setBounds (scoreHeader.removeFromRight (160));
    scoreNameLabel.setBounds (scoreHeader);
    area.removeFromTop (6);

    timeline.setBounds (area.removeFromBottom (44));
    area.removeFromBottom (8);
    scoreView.setBounds (area);

    dimOverlay.setBounds (getLocalBounds());
    if (tunerPanel != nullptr)
        tunerPanel->setBounds (getLocalBounds().withSizeKeepingCentre (TunerPanel::preferredWidth, TunerPanel::preferredHeight));
    if (tonePanel != nullptr)
        tonePanel->setBounds (getLocalBounds().withSizeKeepingCentre (ToneDetailPanel::preferredWidth, ToneDetailPanel::preferredHeight));

    placeWebViews();
    repaint();
}
