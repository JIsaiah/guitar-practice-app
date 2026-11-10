#include "SessionScreen.h"
#include "../Model/TonePresets.h"
#include "../Model/YouTubeUrl.h"
#include "../Audio/PluginLocator.h"
#include "../Platform/WindowOwner.h"
#include "../AppLog.h"
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

    // 値を「100%」の形で出す (作者のレイアウト)。scale: 値に掛けて % にする数 (ミキサーは 1.0 = 100%)
    void showAsPercent (juce::Slider& slider, double scale)
    {
        slider.textFromValueFunction = [scale] (double value) { return juce::String (juce::roundToInt (value * scale)) + "%"; };
        slider.valueFromTextFunction = [scale] (const juce::String& text) { return text.retainCharacters ("0123456789.").getDoubleValue() / scale; };
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 24);
        slider.updateText();
    }

    const char* const scoreFilePatterns = "*.gp;*.gpx;*.gp5;*.gp4;*.gp3;*.musicxml;*.xml";
    const char* const audioFilePatterns = "*.mp3;*.wav;*.flac;*.ogg;*.aif;*.aiff";
}

SessionScreen::SessionScreen (AudioEngine& e, SessionStore& s, ThemeManager& t, AppSettings& appSettings,
                              YouTubePlayer& yt, ScoreViewer& sv, Session sessionToOpen,
                              SessionOpenContext context, PlaybackSettingsResolver resolver)
    : engine (e), store (s), theme (t), youtube (yt), scoreViewer (sv), settings (appSettings),
      session (std::move (sessionToOpen)), openContext (std::move (context)), settingsResolver (std::move (resolver)),
      backButton (t), saveButton (t),
      tapButton (t), tunerButton (t), metronomeButton (t), loopButton (t)
{
    savedSnapshot = juce::JSON::toString (session.toVar());
    savedBpm = session.bpm;
    savedTempoOrigin = session.tempoOrigin;

    //==== 上部 ====
    backButton.setFixedText (FixedTextId::BackToList, "<");
    backButton.setTooltip ("一覧に戻る"_jp);
    saveButton.setFixedText (FixedTextId::Save, "保存"_jp);
    settingsButton.setTooltip ("設定"_jp);
    settingsButton.setIconScale (0.5f);
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

    // 「?」(画面の説明)。機能はまだなので、押すと「準備中」
    helpButton.setTooltip ("画面の説明 (準備中)"_jp);
    helpButton.setIconScale (0.6f);
    helpButton.onClick = [this] { showComingSoon ("画面の説明"_jp); };

    // タブ譜の上の左: リード・バッキング。タブ譜のギターのトラックを切り替える (右クリックでトラックを選び直す)
    leadButton.setButtonText ("リード"_jp);
    backingButton.setButtonText ("バッキング"_jp);
    for (auto* b : { &leadButton, &backingButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (2);
        b->onRightClick = [this] { chooseParts (false); };
    }
    // ラジオボタンは「オフになった」ときにも onClick が来るので、オンになったときだけ切り替える (音色のボタンと同じ)
    leadButton.onClick = [this] { if (leadButton.getToggleState()) switchPart (false, true); };
    backingButton.onClick = [this] { if (backingButton.getToggleState()) switchPart (true, true); };

    // 音色のプリセット 1・2・3
    presetLabel.setText ("プリセット"_jp, juce::dontSendNotification);
    presetLabel.setFont (juce::FontOptions (12.0f));
    presetLabel.setJustificationType (juce::Justification::centredRight);
    for (size_t i = 0; i < presetButtons.size(); ++i)
    {
        const auto slot = (int) i + 1;
        presetButtons[i].setButtonText (juce::String (slot));
        presetButtons[i].onClick = [this, slot] { pressPreset (slot); };
        presetButtons[i].onRightClick = [this, slot] { showPresetMenu (slot); };
    }

    // 音源もタブ譜も無いとき (新規作成で曲名だけ決めたとき) に、タブ譜の枠の真ん中に出すボタン
    emptyScoreButton.setButtonText ("タブ譜を選択"_jp);
    emptyFileButton.setButtonText ("音源ファイルを選ぶ"_jp);
    emptyUrlButton.setButtonText ("YouTube の URL を貼り付け"_jp);
    emptyScoreButton.onClick = [this] { chooseScoreFile(); };
    emptyFileButton.onClick = [this] { chooseSourceFile(); };
    emptyUrlButton.onClick = [this] { pasteYouTubeUrl(); };
    for (auto* b : { &emptyScoreButton, &emptyFileButton, &emptyUrlButton })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        b->setWantsKeyboardFocus (false);
        addChildComponent (b);
    }

    // 曲名の右に「—  アーティスト名」(アーティストが空なら出さない)
    artistLabel.setText (session.artist.isEmpty() ? juce::String() : "—  "_jp + session.artist, juce::dontSendNotification);
    artistLabel.setFont (juce::FontOptions (20.0f));
    artistLabel.setInterceptsMouseClicks (false, false);

    //==== 左: タブ譜 ====
    scoreNameLabel.setFont (juce::FontOptions (14.0f));
    positionLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    positionLabel.setJustificationType (juce::Justification::centredRight);
    scoreChooseButton.setButtonText ("タブ譜を選択"_jp);
    scoreRemoveButton.setButtonText ("タブ譜を外す"_jp);
    scoreChooseButton.onClick = [this] { chooseScoreFile(); };
    scoreRemoveButton.onClick = [this] { removeScore(); };
    swapButton.setButtonText ("動画と入れ替え"_jp);
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

    // 音源のカードをたたむボタン (右上)。たたんだかはアプリの設定に覚える
    videoCardCollapsed = settings.getVideoCardCollapsed();
    videoCollapseButton.setButtonText (videoCardCollapsed ? juce::String::fromUTF8 ("▼") : juce::String::fromUTF8 ("▲"));
    videoCollapseButton.setTooltip (videoCardCollapsed ? "音源のカードを広げる"_jp : "音源のカードをたたむ"_jp);
    videoCollapseButton.setMouseClickGrabsKeyboardFocus (false);
    videoCollapseButton.setWantsKeyboardFocus (false);
    videoCollapseButton.onClick = [this] { toggleVideoCard(); };
    addAndMakeVisible (videoCollapseButton);

    // 読み込み中の画面 (いちばん手前。準備ができたら消す)
    loadingStartedMs = juce::Time::getMillisecondCounterHiRes();
    addAndMakeVisible (loadingOverlay);
    pasteUrlButton.onClick = [this] { pasteYouTubeUrl(); };

    //==== 右: 音色 (簡易な音色 3 つ、TONE3000 プラグイン、NAM) ====
    const auto& presets = getTonePresets();
    for (size_t i = 0; i < toneButtons.size(); ++i)
    {
        auto& button = toneButtons[i];
        // TONE3000 は同梱できないので、ボタンは VST プラグインにする (作者の指示。TONE3000 は選べるものの1つ)。
        // 「VSTプラグイン」はボタンの幅に入らないので「VST」(NAM と同じく短く)
        button.setButtonText (i == pluginToneIndex ? juce::String ("VST")
                              : i == namToneIndex  ? juce::String ("NAM")
                                                   : juce::String::fromUTF8 (presets[i].name));
        button.setClickingTogglesState (true);
        button.setRadioGroupId (1);
        // ラジオボタンは、ほかのボタンを押したときに「オフになった」通知でも onClick が呼ばれる。
        // そのとき selectTone を呼ぶと、オフにされたボタンをまたオンに戻してしまい、2つ選ばれた状態になる。
        // オンになったときだけ処理する
        button.onClick = [this, i] { if (toneButtons[i].getToggleState()) selectTone ((int) i, true); };
    }
    toneButtons[namToneIndex].setTooltip ("NAM のアンプのモデル (.nam) とキャビネットの IR (.wav) で鳴らします"_jp);
    toneButtons[pluginToneIndex].setTooltip ("VST3 プラグイン (TONE3000、Neural DSP など) で鳴らします。プラグインの画面は別のウィンドウで開きます"_jp);

    //==== 右: TONE3000 プラグインのカード ====
    pluginCard.onChooseFile = [this] { choosePluginFile(); };
    pluginCard.onRescan = [this]
    {
        // 前に選んだファイルが無くなっていたら、標準の置き場所から探し直す
        if (session.pluginPath.isNotEmpty() && ! juce::File (session.pluginPath).exists())
            session.pluginPath = {};
        loadPluginTone();
    };
    // カードの大きさが変わったら並べ直す。並べ直しの中でカードがまた知らせることがあるので、
    // その場ではなく次のメッセージで、まとめて1回だけ並べ直す
    pluginCard.onSizeChanged = [this]
    {
        if (relayoutPending)
            return;
        relayoutPending = true;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
        {
            if (safe == nullptr)
                return;
            safe->relayoutPending = false;
            safe->resized();
        });
    };

    // エンジンからの知らせを受け取り、プラグインの編集画面を出す。
    // 画面を切り替えるときは「新しい画面を作る → 古い画面を消す」の順なので、ここですぐに登録すると、
    // 古い画面のデストラクタが登録を消してしまう。古い画面の編集画面もまだ残っている。
    // そこで、古い画面が消えた後 (次のメッセージ) に登録し、編集画面を作る
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this)]
    {
        if (safe == nullptr)
            return;

        // エンジンがプラグインを差し替える前に、編集画面を消す (編集画面はプラグインより先に消す決まり)
        safe->engine.onPluginToneAboutToReplace = [safe] { if (safe != nullptr) safe->pluginCard.deleteEditor(); };
        safe->engine.onPluginToneChanged = [safe]
        {
            if (safe == nullptr)
                return;
            safe->pluginCard.refreshEditor();
            safe->pluginScrollPending = true;   // 出てきた編集画面の全体が見えるところまでスクロールする
            safe->resized();
        };

        // すでにプラグインが読み込まれていれば (前の画面から、または読み込みが先に終わった)、編集画面を出す
        if (safe->engine.getPluginTone() != nullptr)
        {
            safe->pluginCard.refreshEditor();
            safe->pluginScrollPending = true;
            safe->resized();
        }
    });

    toneSummaryLabel.setFont (juce::FontOptions (12.0f));
    toneDetailButton.setIconScale (0.65f);   // 音色の見出しの右の歯車 (作者のレイアウト・作者のアイコン)
    toneDetailButton.setTooltip ("音色の設定: NAM のモデル・IR・入力の大きさ・メーター・オーディオデバイスの状態"_jp);
    toneDetailButton.onClick = [this] { toggleTonePanel(); };

    //==== 右: ミキサー ====
    sourceGainLabel.setText ("音源"_jp, juce::dontSendNotification);
    guitarGainLabel.setText ("ギター"_jp, juce::dontSendNotification);
    metronomeGainLabel.setText ("メトロノーム"_jp, juce::dontSendNotification);

    setUpSlider (sourceGainSlider, 0.0, 1.5, 0.01, session.mixSource);
    setUpSlider (guitarGainSlider, 0.0, 1.5, 0.01, session.mixGuitar);
    setUpSlider (metronomeGainSlider, 0.0, 1.5, 0.01, session.mixMetronome);
    for (auto* slider : { &sourceGainSlider, &guitarGainSlider, &metronomeGainSlider })
        showAsPercent (*slider, 100.0);

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

    // ギターのミュート (作者の指示。メトロノームには付けない)
    guitarMuteButton.setButtonText ("ミュート"_jp);
    guitarMuteButton.setClickingTogglesState (false);
    guitarMuteButton.setTooltip ("ギターの音を消します"_jp);
    guitarMuteButton.onClick = [this] { toggleGuitarMute(); };

    // タブ譜の音 (MIDI): リード・バッキングのトラック。音はタブ譜のページ (WebView) から出る (YouTube と同じく、アプリのミキサーは通らない)
    leadTabLabel.setText ("リード"_jp, juce::dontSendNotification);
    backingTabLabel.setText ("バッキング"_jp, juce::dontSendNotification);
    leadTabLabel.setTooltip ("タブ譜のリードのトラックの音 (MIDI)"_jp);
    backingTabLabel.setTooltip ("タブ譜のバッキングのトラックの音 (MIDI)"_jp);
    setUpSlider (leadTabSlider, 0.0, 1.5, 0.01, session.mixLeadTab);
    setUpSlider (backingTabSlider, 0.0, 1.5, 0.01, session.mixBackingTab);
    showAsPercent (leadTabSlider, 100.0);
    showAsPercent (backingTabSlider, 100.0);
    leadTabSlider.onValueChange = [this] { session.mixLeadTab = leadTabSlider.getValue(); sendTabTracks(); };
    backingTabSlider.onValueChange = [this] { session.mixBackingTab = backingTabSlider.getValue(); sendTabTracks(); };
    for (auto* b : { &leadTabMuteButton, &backingTabMuteButton })
    {
        b->setButtonText ("ミュート"_jp);
        b->setClickingTogglesState (false);
    }
    leadTabMuteButton.setTooltip ("タブ譜のリードの音を消します"_jp);
    backingTabMuteButton.setTooltip ("タブ譜のバッキングの音を消します"_jp);
    leadTabMuteButton.onClick = [this]
    {
        leadTabMuted = ! leadTabMuted;
        leadTabMuteButton.setToggleState (leadTabMuted, juce::dontSendNotification);
        sendTabTracks();
    };
    backingTabMuteButton.onClick = [this]
    {
        backingTabMuted = ! backingTabMuted;
        backingTabMuteButton.setToggleState (backingTabMuted, juce::dontSendNotification);
        sendTabTracks();
    };
    guitarGainSlider.onValueChange = [this]
    {
        session.mixGuitar = guitarGainSlider.getValue();
        applyGuitarGain();
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
    playButton.setIconScale (0.5f);
    playButton.setTooltip ("再生・停止 (Space キー)。続けて2回押すと、待たずにすぐ始めます"_jp);
    metronomeButton.setFixedText (FixedTextId::Metronome, "曲に合わせる"_jp);
    metronomeButton.setTooltip ("オンにすると、再生中に曲の拍に合わせて鳴ります"_jp);
    metronomeSoloButton.setButtonText ("単体"_jp);
    metronomeSoloButton.setTooltip ("曲を再生せずに、メトロノームだけを鳴らします。「曲に合わせる」とは別のボタンです。再生を始めると止まります"_jp);
    loopStartButton.setButtonText ("A");
    loopEndButton.setButtonText ("B");
    loopStartButton.setTooltip ("今の位置にいちばん近い小節の頭を、くり返しの始まり (A) にします"_jp);
    loopEndButton.setTooltip ("今弾いている小節を、くり返しの最後 (B) にします。その小節の終わりまで弾いてから A に戻ります"_jp);
    loopButton.setFixedText (FixedTextId::Loop, "繰り返す"_jp);
    tunerButton.setFixedText (FixedTextId::Tuner, "チューナー"_jp);

    toStartButton.setIconScale (0.45f);
    forwardButton.setIconAfterText (true);   // 「1小節 ▶▶」(作者のレイアウト)
    toStartButton.setTooltip ("曲の頭に戻る (Home キー)"_jp);
    toStartButton.onClick = [this] { perform (Command::goToStart); };
    rewindButton.onClick = [this] { perform (Command::rewind); };
    playButton.onClick = [this] { perform (Command::togglePlay); };
    forwardButton.onClick = [this] { perform (Command::fastForward); };
    metronomeButton.onClick = [this] { perform (Command::toggleMetronome); };
    metronomeSoloButton.onClick = [this] { toggleMetronomeSolo(); };
    metronomeDots.onClick = [this] { toggleBigMetronome(); };

    metronomeStartButton.onClick = [this] { setMetronomeStartHere(); };
    metronomeStartButton.onRightClick = [this]
    {
        if (session.metronomeStartMs <= 0)
            return;

        juce::PopupMenu menu;
        menu.addItem (1, "開始位置を解除"_jp);
        menu.addItem (2, "開始位置へ移動"_jp);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&metronomeStartButton),
                            [safe = juce::Component::SafePointer (this)] (int result)
        {
            if (safe == nullptr)
                return;
            if (result == 1)
                safe->clearMetronomeStart();
            if (result == 2 && safe->sourceReady)
                safe->seekTo (safe->session.metronomeStartMs / 1000.0);
        });
    };
    metronomeDots.setTooltip ("クリックすると、画面の真ん中に大きく出します"_jp);
    metronomeDots.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    loopStartButton.onClick = [this] { perform (Command::setLoopStart); };
    loopEndButton.onClick = [this] { perform (Command::setLoopEnd); };
    loopButton.onClick = [this] { perform (Command::toggleLoop); };
    tunerButton.onClick = [this] { perform (Command::toggleTuner); };

    setUpSlider (speedSlider, 50.0, 200.0, 1.0, session.speedPercent);
    setUpSlider (bpmSlider, 40.0, 300.0, 1.0, session.bpm);
    showAsPercent (speedSlider, 1.0);   // 「100%」の形で出す (作者のレイアウト)
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 24);

    // 速度をダブルクリックすると、原速 (100%) に戻す
    speedSlider.setDoubleClickReturnValue (true, 100.0);
    speedSlider.setTooltip ("ダブルクリックで 100% に戻す"_jp);

    // BPM をダブルクリックすると、この曲に保存されている値に戻す
    bpmSlider.setDoubleClickReturnValue (true, savedBpm);
    bpmSlider.setTooltip ("ダブルクリックで、保存されている BPM に戻す"_jp);

    speedSlider.onValueChange = [this]
    {
        session.speedPercent = (int) speedSlider.getValue();
        engine.setSpeed (session.speedPercent / 100.0);
        scoreClock.setRate (session.speedPercent / 100.0, juce::Time::getMillisecondCounterHiRes());   // タブ譜だけのとき

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
             &backButton, &saveButton, &settingsButton, &titleLabel, &artistLabel, &statusLabel, &helpButton,
             &leadButton, &backingButton,
             &scoreNameLabel, &positionLabel, &scoreChooseButton, &scoreRemoveButton, &swapButton, &scoreView, &timeline,
             &splitter, &sidePanelButton,
             &sourceHeading, &toneHeading, &mixerHeading, &offsetHeading,
             &videoView, &sourceInfoLabel, &sourceRemoveButton, &chooseFileButton, &pasteUrlButton,
             &toneButtons[0], &toneButtons[1], &toneButtons[2], &toneButtons[3],
             &toneSummaryLabel, &toneDetailButton, &presetLabel, &presetButtons[0], &presetButtons[1], &presetButtons[2],
             &sourceGainLabel, &guitarGainLabel, &metronomeGainLabel,
             &sourceGainSlider, &guitarGainSlider, &metronomeGainSlider, &sourceMuteButton, &guitarMuteButton,
             &leadTabLabel, &backingTabLabel, &leadTabSlider, &backingTabSlider, &leadTabMuteButton, &backingTabMuteButton,
             &bpmLabel, &bpmSlider, &tapButton, &scoreOffsetLabel, &scoreOffsetControl, &offsetLabel, &offsetControl,
             &sideScroll, &tunerButton,
             &playHeading, &speedHeading, &metronomeHeading, &loopHeading,
             &toStartButton, &rewindButton, &playButton, &forwardButton, &speedSlider, &equalBarsButton,
             &metronomeButton, &metronomeSoloButton, &metronomeDots, &metronomeStartButton,
             &loopStartButton, &loopEndButton, &loopButton })
    {
        addAndMakeVisible (c);

        // スペースキーや Enter キーを、押したボタンではなくこの画面で受け取るため
        c->setMouseClickGrabsKeyboardFocus (false);
        c->setWantsKeyboardFocus (false);
    }

    setWantsKeyboardFocus (true);

    // 右のパネルの、音源のカードより下の部品は、スクロールする入れ物 (sideContent) に移す。
    // 音色のボタンは、同じ入れ物の中にそろって入るので、ラジオボタン (1つだけ選ぶ) はそのまま働く
    for (auto* c : std::initializer_list<juce::Component*> {
             &toneHeading, &toneButtons[0], &toneButtons[1], &toneButtons[2], &toneButtons[3],
             &toneSummaryLabel, &toneDetailButton, &presetLabel, &presetButtons[0], &presetButtons[1], &presetButtons[2],
             &mixerHeading, &sourceGainLabel, &guitarGainLabel, &metronomeGainLabel,
             &sourceGainSlider, &guitarGainSlider, &metronomeGainSlider, &sourceMuteButton, &guitarMuteButton,
             &leadTabLabel, &backingTabLabel, &leadTabSlider, &backingTabSlider, &leadTabMuteButton, &backingTabMuteButton,
             &offsetHeading, &bpmLabel, &bpmSlider, &tapButton, &scoreOffsetLabel, &scoreOffsetControl, &offsetLabel, &offsetControl })
        sideContent.addAndMakeVisible (c);

    sideContent.addChildComponent (pluginCard);
    sideScroll.setViewedComponent (&sideContent, false);
    sideScroll.setScrollBarsShown (true, false);   // 縦だけ (必要なときだけ出る)
    sideScroll.onScrolled = [this] { updatePluginEditorVisibility(); };

    // URL の欄は、ダブルクリックで文字を入力できるようにする (上でキーボードを受け取らない設定にしたため戻す)
    sourceInfoLabel.setWantsKeyboardFocus (true);

    // チューナーの後ろを暗くする幕 (チューナーを開いたときだけ表示)。クリックするとチューナーを閉じる
    dimOverlay.onClick = [this]
    {
        if (tunerPanel != nullptr) perform (Command::toggleTuner);
        if (tonePanel != nullptr)  toggleTonePanel();
    };
    addChildComponent (dimOverlay);

    // セットリストから開いたときは、速度とループはこの曲順の中だけの設定 (setlist-mode.md 3.6)
    if (settingsResolver.usesOverrides())
    {
        speedHeading.setText ("速度・セトリ専用"_jp, juce::dontSendNotification);
        loopHeading.setText ("ループ・セトリ専用"_jp, juce::dontSendNotification);
        loopHeading.setTooltip ("このループは、このセットリストの中だけで使います (単体のセッションとは別)"_jp);
        speedSlider.setTooltip ("この速度は、このセットリストの中だけで使います。ダブルクリックで 100% に戻す"_jp);
    }

    countdown.onFinished = [this] { startPlayback(); };
    addChildComponent (countdownOverlay);

    setlistBar.onPrevious = [this] { if (onSetlistPrevious != nullptr) onSetlistPrevious(); };
    setlistBar.onNext = [this] { if (onSetlistNext != nullptr) onSetlistNext(); };
    setlistBar.onAutoAdvanceToggled = [this] (bool resume) { if (onSetlistAutoAdvanceToggled != nullptr) onSetlistAutoAdvanceToggled (resume); };
    addChildComponent (setlistBar);
    setlistBar.setVisible (openContext.isSetlist());

    applyAppSettings();
    settings.addChangeListener (this);
    lookAndFeelChanged();

    setTransportEnabled (false);  // 音源の読み込みが終わってから有効にする

    applySessionToEngine();
    updatePartButtons();
    updatePresetButtons();
    updateSourceUi();
    loadSource();
    setUpYouTube();
    loadScore();
    startTimerHz (30);
    updateLoading();   // 音源もタブ譜も無いときなどは、すぐに消す

    // ラジアルメニュー: マウスの左右を同時に 0.5 秒押し続けると、カーソルのところに出る
    radialMenu = std::make_unique<RadialMenu> (*this, [this] { return makeRadialItems(); });
    radialMenu->onOpenChanged = [this] (bool open) { scoreViewer.setFrozen (open); };   // 出している間、タブ譜を止める
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

    // カウントダウンの表示と、タブ譜の音 (MIDI) は WebView のページの中にあるので、次の画面のために消す・止める
    scoreViewer.setMidiTracks ({});
    youtube.showCountdown (0);
    scoreViewer.showCountdown (0);

    // プラグインはエンジンのもの (次の画面でも使える)。この画面の編集画面だけを消し、知らせの受け取りをやめる
    engine.onPluginToneAboutToReplace = nullptr;
    engine.onPluginToneChanged = nullptr;
    pluginCard.deleteEditor();

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
    selectTone (session.tonePresetId == namTonePresetId    ? namToneIndex
              : session.tonePresetId == pluginTonePresetId ? pluginToneIndex
                                                           : findTonePresetIndex (session.tonePresetId), false);
    engine.setInputGainDb ((float) session.inputGainDb);
    loadNamTone();

    // TONE3000 を使うセッションなら、プラグインを読み込み、保存した状態 (選んだ音色・つまみ) に戻す
    if (session.tonePresetId == pluginTonePresetId || session.pluginPath.isNotEmpty())
        loadPluginTone();
    applySourceGain();
    applyGuitarGain();
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
        handleSourceFailed ("音源ファイルが見つかりません: "_jp + session.sourcePath);
        return;
    }

    setStatus ("音源を読み込み中…"_jp);

    engine.loadSourceAsync (file, [safe = juce::Component::SafePointer (this)] (bool succeeded, const juce::String& error)
    {
        if (safe == nullptr || ! safe->isLocalSource())
            return;

        if (! succeeded)
        {
            safe->handleSourceFailed (error);
            return;
        }

        safe->sourceReady = true;
        safe->setStatus ({});
        safe->setTransportEnabled (true);
        safe->recordLength (safe->engine.getSourceLengthSeconds());
        safe->handleSourceReady();
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
    youtube.onError = [this] (const juce::String& message)
    {
        if (sourceReady)
            setStatus (message);
        else
            handleSourceFailed (message);
    };

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
        recordLength (state.duration);
        handleSourceReady();

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
    updateEmptyPrompt();
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
        setUpParts();          // トラックが分かったので、リード・バッキングを決める
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
        safe->resetParts();   // 別のタブ譜なので、リード・バッキングは決め直す

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
            safe->setUpParts();

            // 曲の詳細も、タブ譜に書かれていれば入れる (一覧に出る)。
            // チューニングはタブ譜ごとに違うので上書きし、アーティストは空のときだけ入れる
            if (safe->scoreViewer.getScoreTuning().isNotEmpty())
                safe->session.tuning = safe->scoreViewer.getScoreTuning();
            if (safe->session.artist.isEmpty())
                safe->session.artist = safe->scoreViewer.getScoreArtist();

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
    resetParts();
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
    updateEmptyPrompt();
    placeWebViews();
}

void SessionScreen::updateEmptyPrompt()
{
    const auto empty = ! hasSource() && session.scorePath.isEmpty();
    scoreView.setEmptySession (empty);

    for (auto* b : { &emptyScoreButton, &emptyFileButton, &emptyUrlButton })
        b->setVisible (empty);

    if (! empty)
        return;

    // 真ん中に横に3つ並べる (案内の文の下)
    auto row = scoreView.getBounds().withSizeKeepingCentre (juce::jmin (scoreView.getWidth() - 40, 3 * 200 + 2 * 12), 48)
                                    .translated (0, 30);
    const auto width = (row.getWidth() - 2 * 12) / 3;
    emptyScoreButton.setBounds (row.removeFromLeft (width));
    row.removeFromLeft (12);
    emptyFileButton.setBounds (row.removeFromLeft (width));
    row.removeFromLeft (12);
    emptyUrlButton.setBounds (row);

    for (auto* b : { &emptyScoreButton, &emptyFileButton, &emptyUrlButton })
        b->toFront (false);
}

//==============================================================================
double SessionScreen::getPosition() const
{
    if (isScoreOnly())
        return scoreClock.getPosition (juce::Time::getMillisecondCounterHiRes());
    return isYouTube() ? youtube.getEstimatedTime() : engine.getPositionSeconds();
}

double SessionScreen::getLength() const
{
    if (isScoreOnly())
        return getScoreLengthSeconds();
    return isYouTube() ? youtube.getState().duration : engine.getSourceLengthSeconds();
}

bool SessionScreen::isTransportPlaying() const
{
    if (isScoreOnly())
        return scoreClock.isPlaying();
    return isYouTube() ? youtube.isPlaying() : (isLocalSource() && engine.isPlaying());
}

void SessionScreen::seekTo (double seconds)
{
    if (isScoreOnly())
        scoreClock.seek (juce::jlimit (0.0, getScoreLengthSeconds(), seconds), juce::Time::getMillisecondCounterHiRes());
    else if (isYouTube())
        youtube.seek (seconds);
    else
        engine.seekSeconds (seconds);
}

void SessionScreen::pauseTransport()
{
    if (isScoreOnly())
        scoreClock.pause (juce::Time::getMillisecondCounterHiRes());
    else if (isYouTube())
        youtube.pause();
    else
        engine.setPlaying (false);
}

//==============================================================================
// タブ譜だけで再生する (音源が無いとき)
bool SessionScreen::isScoreOnly() const
{
    return ! hasSource() && session.scorePath.isNotEmpty() && ! scoreViewer.getBarStartBeats().empty();
}

double SessionScreen::getScoreLengthSeconds() const
{
    const auto& bars = scoreViewer.getBarStartBeats();
    const auto& signatures = scoreViewer.getBarSignatures();
    if (bars.empty() || session.bpm <= 0.0)
        return 0.0;

    // 最後の小節の始まり + その小節の長さ (拍子から。4分音符の数)
    const auto last = signatures.empty() ? MusicInfo::TimeSignature {} : signatures.back();
    const auto lastBarBeats = last.denominator > 0 ? last.numerator * 4.0 / last.denominator : 4.0;
    return getScoreOffsetSeconds() + (bars.back() + lastBarBeats) * 60.0 / session.bpm;
}

void SessionScreen::updateScoreOnly (double nowMs)
{
    const auto scoreOnly = isScoreOnly();

    // タブ譜だけになった (タブ譜を読み込んだ・音源を外した) / 音源が入った
    if (scoreOnly != scoreOnlyActive)
    {
        scoreOnlyActive = scoreOnly;
        scoreClock.pause (nowMs);
        scoreClock.seek (0.0, nowMs);
        scoreClock.setRate (session.speedPercent / 100.0, nowMs);
        // 音源が入ったときは、音源の読み込みの側が準備の状態を決める (読み込みが先に終わることもあるので、ここでは触らない)
        if (scoreOnly || ! hasSource())
        {
            sourceReady = scoreOnly;
            setTransportEnabled (scoreOnly);
        }

        if (scoreOnly)
            setStatus ("音源がないので、タブ譜の音で再生します"_jp);
        else if (! isYouTube())
            engine.clearExternalClock();
    }

    if (! scoreOnly)
        return;

    const auto length = getScoreLengthSeconds();
    auto position = scoreClock.getPosition (nowMs);

    // A-B ループ: B まで来たら A に戻る
    if (scoreClock.isPlaying() && session.loopEnabled && session.loopEndMs > session.loopStartMs
        && position >= session.loopEndMs / 1000.0)
    {
        scoreClock.seek (session.loopStartMs / 1000.0, nowMs);
        position = session.loopStartMs / 1000.0;
    }

    // 曲の終わり: 止める (セットリストの自動送りは、止まったことで曲の終わりと分かる)
    if (scoreClock.isPlaying() && length > 0.0 && position >= length)
    {
        scoreClock.pause (nowMs);
        scoreClock.seek (length, nowMs);
        position = length;
    }

    // メトロノームは、YouTube と同じく「外の時計」として、この位置で鳴らす
    engine.setExternalClock (position, scoreClock.getRate(), scoreClock.isPlaying());
}

bool SessionScreen::isDirty() const
{
    // プラグインのつまみの変更は、セッションの値には表れないので別に見る (timerCallback で1秒ごとに確かめる)
    return juce::JSON::toString (session.toVar()) != savedSnapshot || pluginStateChanged;
}

bool SessionScreen::writeToDisk()
{
    auto split = settingsResolver.split (session);
    if (! store.save (split.session))
        return false;

    session.id = split.session.id;   // 初めて保存したときに採番される
    session.sessionId = split.session.sessionId;

    if (split.overrides.has_value() && onSaveOverrides != nullptr)
        return onSaveOverrides (*split.overrides);

    return true;
}

bool SessionScreen::save()
{
    capturePluginState();   // TONE3000 で選んだ音色・つまみも保存する

    if (! writeToDisk())
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

void SessionScreen::recordLength (double seconds)
{
    if (seconds <= 0.0 || std::abs (seconds - session.lengthSeconds) < 0.5)
        return;

    // 長さだけが変わったときは、そのまま保存する (「未保存」にして、戻るときの確認を出さないため)。
    // ほかに未保存の変更があれば、次の保存で一緒に保存される
    const auto wasDirty = isDirty();
    session.lengthSeconds = seconds;

    if (! wasDirty && session.id.isNotEmpty() && writeToDisk())
        savedSnapshot = juce::JSON::toString (session.toVar());
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
    // 1回押すとカウントダウン、続けてもう1回押すとすぐに始める。カウントダウン中に間をあけて押すと中止 (PlayButtonPresses)
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto counting = countdown.isCountingDown() || waitingForSource;

    switch (playPresses.press (now, counting, isTransportPlaying()))
    {
        case PlayButtonPresses::Action::cancel:
            countdown.cancel();
            waitingForSource = false;
            break;

        case PlayButtonPresses::Action::startNow:
            if (countdown.isCountingDown())
                countdown.skip();
            else if (! waitingForSource && sourceReady)
                startPlayback();
            break;

        case PlayButtonPresses::Action::pause:
            pauseTransport();
            break;

        case PlayButtonPresses::Action::startCountdown:
            // 再生開始前の待ち時間 (ギターに手を戻す時間)。0 秒ならすぐに startPlayback() が呼ばれる。
            // 音源とタブ譜は、待ち時間のあとに同じ時計で同時に始まる (ずらしの計算には入れない)
            if (sourceReady)
                countdown.start (settings.getPreStartDelaySeconds(), now);
            break;

        case PlayButtonPresses::Action::none:
            break;
    }

    updatePlayButton();
    updateCountdownDisplay();
}

void SessionScreen::startCountdownForSnapshot()
{
    countdown.start (3.0, juce::Time::getMillisecondCounterHiRes());
    updatePlayButton();
    updateCountdownDisplay();
}

void SessionScreen::updateCountdownDisplay()
{
    const auto number = countdown.isCountingDown() ? countdown.getDisplayNumber (juce::Time::getMillisecondCounterHiRes()) : 0;
    countdownOverlay.setBounds (scoreView.getBounds());   // ウィンドウの大きさが変わっても合わせる

    // 数字の下は曲名。カウントダウンが終わっても音源の準備がまだなら、数字なしで「読み込み中…」(setlist-mode.md 6.5)
    const auto caption = waitingForSource ? "読み込み中…"_jp : number > 0 ? session.title : juce::String();

    // メインの枠 (左の大きな枠) に今あるものの上に出す
    const bool scoreShown = session.scorePath.isNotEmpty() && juce::File (session.scorePath).existsAsFile();
    const auto target = number == 0 && caption.isEmpty() ? CountdownTarget::none
                      : isSwapped()  ? CountdownTarget::video
                      : scoreShown   ? CountdownTarget::score
                                     : CountdownTarget::native;

    // WebView への命令は、表示が変わったときだけ送る
    if (number == shownCountdownNumber && target == shownCountdownTarget && caption == shownCountdownCaption)
        return;

    if (target == CountdownTarget::score || shownCountdownTarget == CountdownTarget::score)
        scoreViewer.showCountdown (target == CountdownTarget::score ? number : 0, caption);
    if (target == CountdownTarget::video || shownCountdownTarget == CountdownTarget::video)
        youtube.showCountdown (target == CountdownTarget::video ? number : 0, caption);

    countdownOverlay.setCountdown (number, caption);
    countdownOverlay.setVisible (target == CountdownTarget::native);
    if (target == CountdownTarget::native)
        countdownOverlay.toFront (false);

    shownCountdownNumber = number;
    shownCountdownTarget = target;
    shownCountdownCaption = caption;
}

void SessionScreen::startPlayback()
{
    if (! sourceReady)
    {
        // セットリストで自動で始めるとき: 準備ができしだい始める
        if (! sourceFailed && hasSource())
            waitingForSource = true;
        else
            handleSourceFailed ("この曲には再生できる音源がありません"_jp);

        updateCountdownDisplay();
        return;
    }

    waitingForSource = false;
    songEndNotified = false;
    updateCountdownDisplay();

    // タブ譜だけ: タブ譜の時計を進める (最後まで来ていたら頭から)
    if (isScoreOnly())
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (scoreClock.getPosition (now) >= getScoreLengthSeconds() - 0.05)
            scoreClock.seek (0.0, now);
        scoreClock.play (now);
        return;
    }

    if (isYouTube())
    {
        youtube.play();   // 最後まで再生していたら、YouTube が頭から再生する
        return;
    }

    // 最後まで再生していたら、頭から
    if (engine.getPositionSeconds() >= engine.getSourceLengthSeconds() - 0.05)
        engine.seekSeconds (0.0);

    engine.setPlaying (true);
}

void SessionScreen::handleSourceReady()
{
    if (waitingForSource)
        startPlayback();
}

void SessionScreen::handleSourceFailed (const juce::String& message)
{
    sourceFailed = true;
    setStatus (message);

    // セットリストの自動送りは止める (通常どおりエラーを出し、次の曲へは進まない。setlist-mode.md 6.5)
    const auto wasWaiting = waitingForSource || countdown.isCountingDown();
    waitingForSource = false;
    countdown.cancel();
    updateCountdownDisplay();

    if (wasWaiting && openContext.isSetlist() && onSetlistAutoAdvanceToggled != nullptr)
        onSetlistAutoAdvanceToggled (false);
}

//==============================================================================
// セットリスト
void SessionScreen::setSetlistInfo (const SetlistBar::Info& info)
{
    setlistBar.setInfo (info);
}

void SessionScreen::startAutomatically (double delaySeconds)
{
    countdown.start (delaySeconds, juce::Time::getMillisecondCounterHiRes());
    updatePlayButton();
    updateCountdownDisplay();
}

void SessionScreen::stopForSetlist()
{
    countdown.cancel();
    waitingForSource = false;

    if (isTransportPlaying())
        pauseTransport();

    updatePlayButton();
    updateCountdownDisplay();
}

void SessionScreen::showSetlistEnd()
{
    // 【フェーズ6で「セットリスト終了」の画面に置き換える】
    setStatus ("セットリストの最後の曲が終わりました"_jp);
}

void SessionScreen::saveChangesQuietly()
{
    if (isDirty())
        save();
}

// 曲の終わり (setlist-mode.md 7.3): 再生していたものが、音源の終わりで止まった。
// タブ譜と長さが違っても、音源の終わりで決める。ループ中は終わらない
void SessionScreen::checkSongEnd (bool playing, double position, double length)
{
    if (! openContext.isSetlist() || songEndNotified || playing || ! wasPlaying)
        return;

    // YouTube は「終了」の状態 (0) でも終わりとみなす。位置の報告は間をあけて届くので、
    // 最後に届いた位置が、動画の長さより少し手前のことがある
    const bool reachedEnd = (length > 0.0 && position >= length - 0.25)
                            || (isYouTube() && youtube.getState().playerState == 0);

    if (session.loopEnabled || ! reachedEnd)
        return;   // 途中で止めた

    songEndNotified = true;
    if (onSongFinished != nullptr)
        onSongFinished();
}

void SessionScreen::updatePlayButton()
{
    // カウントダウン中は残りの秒数を出す (押すと中止)
    if (countdown.isCountingDown())
    {
        playButton.setIcon (nullptr);
        playButton.setButtonText (juce::String (countdown.getDisplayNumber (juce::Time::getMillisecondCounterHiRes())) + "…"_jp);
    }
    else
    {
        playButton.setIcon (isTransportPlaying() ? Icons::pause : Icons::play);
        playButton.setButtonText ({});
    }
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
        rewindButton.setButtonText ("1小節"_jp);
        forwardButton.setButtonText ("1小節"_jp);
        rewindButton.setTooltip ("前の小節の頭へ (←キー)。小節の途中なら、その小節の頭へ"_jp);
        forwardButton.setTooltip ("次の小節の頭へ (→キー)"_jp);
        playHeading.setText ("再生"_jp, juce::dontSendNotification);
    }
    else
    {
        const auto seconds = juce::String (settings.getSkipSeconds());
        rewindButton.setButtonText (seconds + "秒"_jp);
        forwardButton.setButtonText (seconds + "秒"_jp);
        rewindButton.setTooltip (seconds + " 秒戻る (←キー)。秒数は設定で変えられます"_jp);
        forwardButton.setTooltip (seconds + " 秒進む (→キー)。秒数は設定で変えられます"_jp);
        playHeading.setText ("再生"_jp, juce::dontSendNotification);
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

    // 動画をメインに表示しているときは、小節に合わせず押した位置にする (作者の指示)。
    // 動画を見ながら弾いているときは小節が見えず、戻る・進むも秒単位のため
    if (isSwapped())
    {
        const auto ms = juce::roundToInt (position * 1000.0);
        if (isStart)
            session.loopStartMs = ms;
        else
            session.loopEndMs = ms;

        if (session.loopEndMs <= session.loopStartMs)
            session.loopEnabled = false;

        updateLoop();
        return;
    }

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

// メトロノームのビジュアライザー
//   メトロノームが拍を数えている時計 (オーディオスレッドが書く) から、今が小節の何拍目かを求める。
//   拍子はタブ譜から (なければ 4/4)。止まっているときは、今の位置の小節の拍子だけを出して、ドットは点けない
void SessionScreen::updateMetronomeDots (double positionBeats)
{
    static const std::vector<double> noBars;
    static const std::vector<MusicInfo::TimeSignature> noSignatures;

    const bool scoreBars = usesScoreBars();
    const auto& bars = scoreBars ? scoreViewer.getBarStartBeats() : noBars;
    const auto& signatures = scoreBars ? scoreViewer.getBarSignatures() : noSignatures;

    // メトロノームがオフのときは動かさない (作者の指示)。「曲に合わせる」か「単体で鳴らす」がオンのときだけ
    const bool metronomeActive = metronomeOn || engine.isMetronomeSolo();
    const auto clock = engine.getBeatClock();
    const bool running = metronomeActive && clock.running && clock.seconds >= 0.0;
    const auto beats = running ? clock.seconds * session.bpm / 60.0 : positionBeats;

    const auto where = MusicInfo::locateBeat (beats, bars, signatures);
    const auto active = running ? where.beatInBar : -1;
    metronomeDots.setState (where.signature.numerator, where.signature.denominator, active);

    if (bigMetronome != nullptr)
    {
        bigMetronome->getDots().setState (where.signature.numerator, where.signature.denominator, active);
        placeBigMetronome();   // アプリのウィンドウを動かしたときも、真ん中に付いていく
    }
}

void SessionScreen::toggleBigMetronome()
{
    if (bigMetronome != nullptr)
    {
        bigMetronome = nullptr;
        return;
    }

    bigMetronome = std::make_unique<MetronomeBigWindow>();
    bigMetronome->onClose = [safe = juce::Component::SafePointer (this)]
    {
        // このウィンドウのクリックの処理の途中で消さないよう、次のメッセージで消す
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->bigMetronome = nullptr; });
    };

    // 枠のないウィンドウ。アプリのウィンドウを持ち主にして、その手前に出す (タブ譜・動画の WebView より手前)
    bigMetronome->addToDesktop (0);
    if (auto* top = getTopLevelComponent(); top != nullptr && top->isOnDesktop())
        WindowOwner::setOwner (bigMetronome->getWindowHandle(), top->getWindowHandle());

    placeBigMetronome();
    bigMetronome->setVisible (true);
}

void SessionScreen::placeBigMetronome()
{
    if (bigMetronome == nullptr)
        return;

    // 画面 (練習画面) の真ん中。幅は画面の 7 割まで、ドットが多いときは広げる
    const auto screen = getScreenBounds();
    const auto count = juce::jlimit (1, 32, metronomeDots.getCount());
    const auto width = juce::jmin (juce::roundToInt (screen.getWidth() * 0.7), 260 + count * 110);
    const auto bounds = juce::Rectangle<int> (width, 220).withCentre (screen.getCentre());

    if (bigMetronome->getBounds() != bounds)
        bigMetronome->setBounds (bounds);
}

// 小節の頭の位置 (秒) = 小節の頭の拍 × 60 ÷ BPM + ずらし
//   タブ譜の小節: タブ譜のカーソルと同じずらし (拍の位置 + タブ譜だけ) を使う。画面のタブ譜と合うように
//   4/4 とみなすとき: メトロノームと同じずらし (拍の位置) を使う
double SessionScreen::barStartSeconds (int index) const
{
    index = juce::jmax (0, index);
    const auto& bars = scoreViewer.getBarStartBeats();

    double beats = index * 4.0;
    double offset = getMetronomeOffsetSeconds();   // 4/4 とみなすときは、メトロノームと同じ起点

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

    // 動画をメインに表示しているときは、時刻で表示する (A・B を押した位置で決めるため)
    if (isSwapped())
    {
        const auto a = formatTime (session.loopStartMs / 1000.0), b = formatTime (session.loopEndMs / 1000.0);
        loopStartButton.setButtonText (unset ? juce::String ("A") : "A:" + a);
        loopEndButton.setButtonText (unset ? juce::String ("B") : "B:" + b);
        if (! unset && session.loopEndMs > session.loopStartMs)
            loopButton.setTooltip (a + "〜"_jp + b + " をくり返します"_jp);
        return;
    }

    const auto firstBar = barIndexNear (session.loopStartMs / 1000.0) + 1;
    const auto lastBar = juce::jmax (1, barIndexNear (session.loopEndMs / 1000.0));

    loopStartButton.setButtonText (unset ? juce::String ("A") : "A:" + juce::String (firstBar) + "小節"_jp);
    loopEndButton.setButtonText (unset ? juce::String ("B") : "B:" + juce::String (lastBar) + "小節"_jp);

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
    offsetControl.setValue (session.syncOffsetMs);
    updateMetronomeStart();   // エンジンには「拍の位置 + 開始位置」を送る
}

//==============================================================================
// メトロノームの開始位置 (動画モード)
//   動画モードでは、メトロノームの拍を「開始位置 + 拍の位置」から数える。それより前は鳴らない
//   (エンジンは、拍の起点より前 (時計がマイナス) では鳴らさない)。
//   タブ譜モードでは、タブ譜の小節に合わせたいので使わない
bool SessionScreen::isMetronomeStartActive() const
{
    return session.metronomeStartMs > 0 && ! isBarMode();
}

double SessionScreen::getMetronomeOffsetSeconds() const
{
    return (session.syncOffsetMs + (isMetronomeStartActive() ? session.metronomeStartMs : 0)) / 1000.0;
}

void SessionScreen::setMetronomeStartHere()
{
    if (! sourceReady)
        return;

    session.metronomeStartMs = juce::jmax (1, juce::roundToInt (getPosition() * 1000.0));   // 0 は「指定なし」なので 1ms 以上
    setStatus ("メトロノームを "_jp + formatTime (session.metronomeStartMs / 1000.0) + " から始めます"_jp);
    updateMetronomeStart();
}

void SessionScreen::clearMetronomeStart()
{
    session.metronomeStartMs = 0;
    setStatus ("メトロノームの開始位置を解除しました"_jp);
    updateMetronomeStart();
}

void SessionScreen::updateMetronomeStart()
{
    // エンジンへ (変わったときだけ)。タブ譜の読み込みや動画との入れ替えで、モードが変わることもある
    const auto offset = getMetronomeOffsetSeconds();
    if (! juce::approximatelyEqual (offset, sentMetronomeOffset))
    {
        sentMetronomeOffset = offset;
        engine.setSyncOffsetSeconds (offset);
    }

    const bool videoMode = ! isBarMode();
    metronomeStartButton.setEnabled (videoMode && sourceReady);
    metronomeStartButton.setButtonText (session.metronomeStartMs > 0 ? "開始 "_jp + formatTime (session.metronomeStartMs / 1000.0)
                                                                     : "ここから開始"_jp);
    metronomeStartButton.setTooltip (videoMode
        ? "今の位置から、メトロノームを始めます (それより前は鳴りません)。右クリックで解除"_jp
        : "動画モード (タブ譜がないとき・動画と入れ替えたとき) で使えます。タブ譜モードでは、タブ譜の小節に合わせて鳴ります"_jp);

    // 再生バーには開始位置の印を出さない (作者の指示)。位置はボタンの「開始 01:23.4」で分かる
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
    updateLoop();   // ループの A・B の表示 (小節 ⇔ 時刻) を合わせる

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
    resized();   // 曲名の幅が変わるので、アーティスト名の位置を合わせる
}

void SessionScreen::toggleSourceMute()
{
    sourceMuted = ! sourceMuted;
    sourceMuteButton.setToggleState (sourceMuted, juce::dontSendNotification);
    applySourceGain();
    setStatus (sourceMuted ? "音源をミュートしました (タブ譜とメトロノームはそのまま動きます)"_jp : juce::String());
}

void SessionScreen::toggleGuitarMute()
{
    guitarMuted = ! guitarMuted;
    guitarMuteButton.setToggleState (guitarMuted, juce::dontSendNotification);
    applyGuitarGain();
    setStatus (guitarMuted ? "ギターをミュートしました"_jp : juce::String());
}

void SessionScreen::applyGuitarGain()
{
    // 音源のミュートと同じく、スライダーの値 (session.mixGuitar) は変えない。解除すると元の音量に戻る
    engine.setGuitarGain (guitarMuted ? 0.0f : (float) session.mixGuitar);
    guitarGainSlider.setAlpha (guitarMuted ? 0.5f : 1.0f);
}

//==============================================================================
// リード・バッキング (作者の指示)
//   タブ譜のギターのトラックから、リードとバッキングを決める (ScoreParts::assign)。最大 2トラック。
//   ギターが 3つ以上なら、選ぶ画面を出す。切り替えは表示するトラックを変えるだけ (再生・音色の設定は共通)。
//   それぞれのパートに音色のプリセット (1・2・3) を割り当てると、切り替えたときにその音色になる
int SessionScreen::activePartTrack() const
{
    // リードだけ・バッキングだけのこともある (1トラックのタブ譜をバッキングにしたとき)
    if (session.backingTrack >= 0 && (session.backingActive || session.leadTrack < 0))
        return session.backingTrack;
    return session.leadTrack;
}

void SessionScreen::resetParts()
{
    session.leadTrack = session.backingTrack = -1;
    session.backingActive = false;
    updatePartButtons();
}

void SessionScreen::setUpParts()
{
    const auto& tracks = scoreViewer.getTracks();
    const auto valid = [&tracks] (int track) { return juce::isPositiveAndBelow (track, (int) tracks.size()); };
    const auto wasDirty = isDirty();
    bool askNow = false;

    if (! valid (session.leadTrack))   session.leadTrack = -1;
    if (! valid (session.backingTrack)) session.backingTrack = -1;

    // まだ決めていない (新しく入れたタブ譜・前からあるセッション) なら、ギターのトラックから仮に決め、
    // 選ぶ画面を出す (作者の指示: タブ譜を入れたら、リードかバッキングを必ず選んでもらう)。
    // セットリストの通し再生では、カウントダウンを止めないよう、聞かずに仮のまま使う
    if (session.leadTrack < 0 && session.backingTrack < 0 && ! tracks.empty())
    {
        const auto assignment = ScoreParts::assign (tracks);
        session.leadTrack = assignment.lead;
        session.backingTrack = assignment.backing;
        session.backingActive = false;
        askNow = ! openContext.isSetlist();
        partsChoiceWasClean = ! wasDirty;
    }

    if (session.backingTrack < 0)
        session.backingActive = false;
    if (session.leadTrack < 0 && session.backingTrack >= 0)
        session.backingActive = true;   // バッキングだけ

    // 表示しているトラックを、今のパートに合わせる
    if (const auto track = activePartTrack(); track >= 0 && track != session.scoreTrackIndex)
    {
        session.scoreTrackIndex = track;
        scoreViewer.showTrack (track);
    }

    // 自動で決めただけなら「未保存」にしない (次に保存したときに一緒に保存される)
    if (! wasDirty)
        savedSnapshot = juce::JSON::toString (session.toVar());

    updatePartButtons();

    if (askNow)
        chooseParts (true);
}

void SessionScreen::chooseParts (bool required)
{
    const auto& tracks = scoreViewer.getTracks();
    if (tracks.empty())
    {
        setStatus ("タブ譜を選ぶと、リードとバッキングのトラックを選べます"_jp);
        return;
    }

    // 選べるのはギターのトラック。ギターと見分けられるトラックが無ければ、すべてのトラック
    auto candidates = ScoreParts::guitarTracks (tracks);
    if (candidates.empty())
        for (int i = 0; i < (int) tracks.size(); ++i)
            candidates.push_back (i);

    juce::StringArray names;
    for (auto track : candidates)
        names.add (ScoreParts::displayName (tracks, track));

    const auto message = (required ? "タブ譜を読み込みました。"_jp : juce::String())
                       + "このタブ譜のギターのトラックを、リードとバッキングのどちらにするか選んでください (最大2つ。どちらか1つでもよい)。"_jp;
    partsWindow = std::make_unique<juce::AlertWindow> ("リードとバッキング"_jp, message, juce::MessageBoxIconType::NoIcon, this);

    // どちらも「なし」を選べる (1トラックのタブ譜をバッキングにするとき)。両方「なし」なら、最初のトラックをリードにする
    juce::StringArray choices { "なし"_jp };
    choices.addArray (names);
    partsWindow->addComboBox ("lead", choices, "リード"_jp);
    partsWindow->addComboBox ("backing", choices, "バッキング"_jp);

    const auto indexOf = [&candidates] (int track)
    {
        const auto it = std::find (candidates.begin(), candidates.end(), track);
        return it == candidates.end() ? -1 : (int) std::distance (candidates.begin(), it);
    };
    partsWindow->getComboBoxComponent ("lead")->setSelectedItemIndex (indexOf (session.leadTrack) + 1);
    partsWindow->getComboBoxComponent ("backing")->setSelectedItemIndex (indexOf (session.backingTrack) + 1);

    // タブ譜を入れたときは、必ずどちらかを選ぶ (キャンセルは無い。仮に決めた組み合わせのまま OK でもよい)
    partsWindow->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    if (! required)
        partsWindow->addButton ("キャンセル"_jp, 0, juce::KeyPress (juce::KeyPress::escapeKey));

    partsWindow->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer (this), candidates, required] (int result)
        {
            if (safe == nullptr || safe->partsWindow == nullptr)
                return;

            if (result == 1)
            {
                const auto lead = safe->partsWindow->getComboBoxComponent ("lead")->getSelectedItemIndex() - 1;
                const auto backing = safe->partsWindow->getComboBoxComponent ("backing")->getSelectedItemIndex() - 1;
                const auto trackAt = [&candidates] (int i) { return juce::isPositiveAndBelow (i, (int) candidates.size()) ? candidates[(size_t) i] : -1; };

                safe->session.leadTrack = trackAt (lead);
                safe->session.backingTrack = trackAt (backing);
                if (safe->session.backingTrack == safe->session.leadTrack)
                    safe->session.backingTrack = -1;   // 同じトラックは 2つにしない
                if (safe->session.leadTrack < 0 && safe->session.backingTrack < 0)
                    safe->session.leadTrack = candidates.front();   // どちらかは必ず決める

                // バッキングだけなら、バッキングを出す。両方あるなら、今出しているほうのまま
                safe->switchPart (safe->session.leadTrack < 0 || (safe->session.backingActive && safe->session.backingTrack >= 0), false);

                // 開いたときに聞いたのなら、選んだ結果をそのまま保存する (保存しないと、次に開いたときにまた聞くため)。
                // ほかに未保存の変更があるときは、いつもの保存に任せる
                if (required && safe->partsChoiceWasClean && safe->writeToDisk())
                    safe->savedSnapshot = juce::JSON::toString (safe->session.toVar());
            }

            safe->partsWindow = nullptr;
            safe->grabKeyboardFocus();
        }), false);
}

void SessionScreen::switchPart (bool backing, bool fromUser)
{
    // リード・バッキングの切り替え (と、それに合わせたプリセットの音色) は、セッションの変更として扱わない (作者の指示)。
    // 切り替えるだけで「保存しますか？」と聞かれないよう、ほかに変更が無ければ「保存済み」のままにする
    const auto wasDirty = isDirty();

    if (backing && session.backingTrack < 0)
        backing = false;
    if (! backing && session.leadTrack < 0 && session.backingTrack >= 0)
        backing = true;   // バッキングだけ

    session.backingActive = backing;

    if (const auto track = activePartTrack(); track >= 0 && track != session.scoreTrackIndex)
    {
        session.scoreTrackIndex = track;
        scoreViewer.showTrack (track);
    }

    // そのパートに割り当てたプリセットの音色にする
    if (fromUser)
    {
        const auto slot = currentPartPreset();
        if (slot > 0 && ! session.toneSlots[(size_t) slot - 1].isEmpty())
            applyPreset (slot);

        setStatus ((backing ? "バッキング"_jp : "リード"_jp) + "のタブ譜にしました"_jp
                   + (slot > 0 ? " (プリセット "_jp + juce::String (slot) + ")" : juce::String()));
    }

    // 最後に選んだトラックを覚える (作者の指示: 次に開いたとき、そのトラックから始める)。
    // ほかに未保存の変更が無ければ、そのまま保存する (変更として「保存 *」にはしない)。あれば次の保存で一緒に保存される
    if (fromUser && ! wasDirty && writeToDisk())
        savedSnapshot = juce::JSON::toString (session.toVar());

    updatePartButtons();
    updatePresetButtons();
}

void SessionScreen::sendTabTracks()
{
    // 鳴らすのはリード・バッキングのトラックだけ (ほかのトラックは鳴らさない)。音は再生中だけ出る (ScoreViewer::setPosition)
    std::vector<ScoreViewer::MidiTrack> tracks;
    if (hasLeadTab())
        tracks.push_back ({ session.leadTrack, session.mixLeadTab, leadTabMuted });
    if (hasBackingTab())
        tracks.push_back ({ session.backingTrack, session.mixBackingTab, backingTabMuted });
    scoreViewer.setMidiTracks (tracks);

    leadTabSlider.setAlpha (leadTabMuted ? 0.5f : 1.0f);
    backingTabSlider.setAlpha (backingTabMuted ? 0.5f : 1.0f);
}

void SessionScreen::updatePartButtons()
{
    sendTabTracks();

    // ミキサーのタブ譜の行 (リード・バッキング) は、そのトラックがあるときだけ出す
    if ((hasLeadTab() || hasBackingTab()) != tabRowsShown)
        resized();

    const auto& tracks = scoreViewer.getTracks();
    // タブ譜が無ければどちらも選ばない。タブ譜があれば、どちらか (今のパート) が必ず選ばれている
    const auto hasScore = session.scorePath.isNotEmpty();
    const auto hasLead = hasScore && session.leadTrack >= 0;
    const auto hasBacking = hasScore && session.backingTrack >= 0;
    const auto showingBacking = hasBacking && (session.backingActive || ! hasLead);

    leadButton.setEnabled (hasLead);
    backingButton.setEnabled (hasBacking);
    leadButton.setToggleState (hasLead && ! showingBacking, juce::dontSendNotification);
    backingButton.setToggleState (showingBacking, juce::dontSendNotification);

    const auto tip = [&tracks] (const juce::String& part, int track)
    {
        const auto name = ScoreParts::displayName (tracks, track);
        return part + (name.isNotEmpty() ? ": "_jp + name : juce::String()) + "。右クリックでトラックを選び直せます"_jp;
    };
    leadButton.setTooltip (hasLead ? tip ("リード"_jp, session.leadTrack)
                                   : hasScore ? "リードのトラックがありません。右クリックでトラックを選べます"_jp : "タブ譜を選ぶと使えます"_jp);
    backingButton.setTooltip (hasBacking ? tip ("バッキング"_jp, session.backingTrack)
                                         : hasScore ? "バッキングのトラックがありません。右クリックでトラックを選べます"_jp : "タブ譜を選ぶと使えます"_jp);
}

//==============================================================================
// 音色のプリセット 1・2・3
juce::String SessionScreen::describeToneSlot (const ToneSlot& slot) const
{
    if (slot.tonePresetId == namTonePresetId)
        return "NAM: "_jp + (slot.namModelPath.isEmpty() ? "なし"_jp : juce::File (slot.namModelPath).getFileNameWithoutExtension());
    if (slot.tonePresetId == pluginTonePresetId)
        return "VST: "_jp + (slot.pluginPath.isEmpty() ? "なし"_jp : juce::File (slot.pluginPath).getFileNameWithoutExtension());
    return juce::String::fromUTF8 (getTonePresets()[(size_t) findTonePresetIndex (slot.tonePresetId)].name);
}

void SessionScreen::pressPreset (int slot)
{
    if (! juce::isPositiveAndBelow (slot - 1, Session::numToneSlots))
        return;

    const auto part = session.backingActive ? "バッキング"_jp : "リード"_jp;
    if (session.toneSlots[(size_t) slot - 1].isEmpty())
    {
        savePreset (slot);
        setStatus ("今の音色をプリセット "_jp + juce::String (slot) + " に保存し、"_jp + part + "に割り当てました"_jp);
    }
    else
    {
        applyPreset (slot);
        setStatus ("プリセット "_jp + juce::String (slot) + " (" + describeToneSlot (session.toneSlots[(size_t) slot - 1])
                   + ") を"_jp + part + "に割り当てました"_jp);
    }

    currentPartPreset() = slot;
    updatePresetButtons();
}

void SessionScreen::stepPreset (int direction)
{
    // 中身のあるプリセットだけを、順に回す (1 → 2 → 3 → 1 …)
    std::vector<int> filled;
    for (int slot = 1; slot <= Session::numToneSlots; ++slot)
        if (! session.toneSlots[(size_t) slot - 1].isEmpty())
            filled.push_back (slot);

    if (filled.empty())
    {
        setStatus ("音色のプリセットがありません。音色の欄の 1・2・3 を押すと、今の音色を保存できます"_jp);
        return;
    }

    const auto n = (int) filled.size();
    const auto it = std::find (filled.begin(), filled.end(), currentPartPreset());
    const auto index = it == filled.end() ? (direction > 0 ? 0 : n - 1)
                                          : ((int) std::distance (filled.begin(), it) + direction + n) % n;
    pressPreset (filled[(size_t) index]);
}

//==============================================================================
// ラジアルメニューに並べる操作 (上から時計回り。作者の画像のとおり)
std::vector<RadialMenu::Item> SessionScreen::makeRadialItems()
{
    const bool hasLoop = session.loopEndMs > session.loopStartMs;
    const bool hasTwoParts = session.leadTrack >= 0 && session.backingTrack >= 0;
    bool hasPresets = false;
    for (const auto& slot : session.toneSlots)
        hasPresets = hasPresets || ! slot.isEmpty();

    return {
        { "再生・停止"_jp,               [this] { perform (Command::togglePlay); } },
        { session.loopEnabled ? "ループ解除"_jp : "ループ"_jp,
                                         [this] { perform (Command::toggleLoop); }, hasLoop },
        { "右の音色プリセット"_jp,       [this] { stepPreset (1); }, hasPresets },
        { guitarMuted ? "ギターミュート解除"_jp : "ギターミュート"_jp,
                                         [this] { toggleGuitarMute(); } },
        { "メトロノームオン・オフ"_jp,   [this] { perform (Command::toggleMetronome); } },
        { sourceMuted ? "音源ミュート解除"_jp : "音源ミュート"_jp,
                                         [this] { toggleSourceMute(); } },
        { "左の音色プリセット"_jp,       [this] { stepPreset (-1); }, hasPresets },
        { "トラック切り替え"_jp,         [this] { switchPart (! session.backingActive, true); }, hasTwoParts },
    };
}

void SessionScreen::savePreset (int slot)
{
    auto& s = session.toneSlots[(size_t) slot - 1];
    s.tonePresetId = session.tonePresetId;
    s.namModelPath = session.namModelPath;
    s.irPath = session.irPath;
    s.inputGainDb = session.inputGainDb;
    s.pluginPath = session.pluginPath;
    s.pluginState = session.pluginState;

    // プラグインのつまみは、今の状態を保存する (セッションの保存を待たずに)
    if (session.tonePresetId == pluginTonePresetId)
        if (auto* tone = engine.getPluginTone())
            s.pluginState = tone->getState().toBase64Encoding();

    updatePresetButtons();
}

void SessionScreen::applyPreset (int slot)
{
    const auto& s = session.toneSlots[(size_t) slot - 1];
    if (s.isEmpty())
        return;

    session.tonePresetId = s.tonePresetId;
    session.namModelPath = s.namModelPath;
    session.irPath = s.irPath;
    session.inputGainDb = s.inputGainDb;

    // 音色を開いたときと同じ順で、エンジンに渡す (applySessionToEngine)
    selectTone (session.tonePresetId == namTonePresetId    ? namToneIndex
              : session.tonePresetId == pluginTonePresetId ? pluginToneIndex
                                                           : findTonePresetIndex (session.tonePresetId), false);
    engine.setInputGainDb ((float) session.inputGainDb);
    loadNamTone();

    if (session.tonePresetId == pluginTonePresetId && s.pluginPath.isNotEmpty())
    {
        session.pluginPath = s.pluginPath;
        session.pluginState = s.pluginState;
        loadPluginTone();
    }

    updateToneSummary();
}

void SessionScreen::clearPreset (int slot)
{
    session.toneSlots[(size_t) slot - 1] = {};
    if (session.leadPreset == slot)    session.leadPreset = 0;
    if (session.backingPreset == slot) session.backingPreset = 0;
    updatePresetButtons();
}

void SessionScreen::showPresetMenu (int slot)
{
    const auto empty = session.toneSlots[(size_t) slot - 1].isEmpty();

    juce::PopupMenu menu;
    menu.addItem (1, "今の音色をプリセット "_jp + juce::String (slot) + " に保存"_jp);
    menu.addItem (2, "プリセット "_jp + juce::String (slot) + " を空にする"_jp, ! empty);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButtons[(size_t) slot - 1]),
                        [safe = juce::Component::SafePointer (this), slot] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == 1)
        {
            safe->savePreset (slot);
            safe->currentPartPreset() = slot;
            safe->updatePresetButtons();
            safe->setStatus ("今の音色をプリセット "_jp + juce::String (slot) + " に保存しました"_jp);
        }
        else if (result == 2)
        {
            safe->clearPreset (slot);
            safe->setStatus ("プリセット "_jp + juce::String (slot) + " を空にしました"_jp);
        }
    });
}

void SessionScreen::updatePresetButtons()
{
    const auto current = currentPartPreset();
    const auto part = session.backingActive ? "バッキング"_jp : "リード"_jp;

    for (int i = 0; i < Session::numToneSlots; ++i)
    {
        const auto slot = i + 1;
        const auto& s = session.toneSlots[(size_t) i];
        auto& button = presetButtons[(size_t) i];

        button.setToggleState (slot == current, juce::dontSendNotification);
        button.setAlpha (s.isEmpty() ? 0.6f : 1.0f);   // 空きは薄く
        button.setTooltip (s.isEmpty()
            ? "プリセット "_jp + juce::String (slot) + " (空き)。押すと今の音色を保存して、"_jp + part + "に割り当てます"_jp
            : "プリセット "_jp + juce::String (slot) + ": "_jp + describeToneSlot (s) + "。押すと"_jp + part
                  + "に割り当てます。右クリックで保存し直す・空にする"_jp);
    }
}

//==============================================================================
// 読み込み中の画面
void SessionScreen::updateLoading()
{
    if (! loading)
        return;

    // タブ譜: 描き終えた (ファイルが無い・タブ譜が無いなら待たない)
    const auto scoreFile = juce::File (session.scorePath);
    const bool scoreReady = session.scorePath.isEmpty() || ! scoreFile.existsAsFile() || scoreViewer.isRendered();

    // 音源: 再生できる・読めなかった (音源が無いなら待たない)
    const bool sourceDone = ! hasSource() || sourceReady || sourceFailed;

    const bool timedOut = juce::Time::getMillisecondCounterHiRes() - loadingStartedMs > maxLoadingMs;

    if ((scoreReady && sourceDone) || timedOut)
    {
        loading = false;
        loadingOverlay.setVisible (false);
        placeWebViews();   // 隠していたタブ譜・動画を出す
        return;
    }

    loadingOverlay.toFront (false);
    loadingOverlay.repaint();   // 回る弧を動かす
}

#if JUCE_DEBUG
// 開発用 (後で消す): 今流れているタブ譜の小節と拍子を、別の小さいウィンドウに出す
void SessionScreen::updateDevScoreInfo (double scoreBeats)
{
    // 画面に出ている練習画面のときだけ (開発用の画面の書き出し (DevSnapshots) では出さない)
    if (! isShowing())
        return;

    if (devScoreInfo == nullptr)
    {
        devScoreInfo = std::make_unique<DevScoreInfoWindow>();
        const auto screen = getScreenBounds();
        devScoreInfo->setTopLeftPosition (screen.getX() + 20, screen.getY() + 80);
        devScoreInfo->setVisible (true);
    }

    const auto& bars = scoreViewer.getBarStartBeats();
    const auto& signatures = scoreViewer.getBarSignatures();
    const bool scoreBars = usesScoreBars();
    static const std::vector<double> noBars;
    static const std::vector<MusicInfo::TimeSignature> noSignatures;
    const auto where = MusicInfo::locateBeat (scoreBeats, scoreBars ? bars : noBars, scoreBars ? signatures : noSignatures);

    juce::String text;
    text << "小節 "_jp << (scoreBeats >= 0.0 ? juce::String (where.bar + 1) : juce::String ("-"))
         << (scoreBars ? " / " + juce::String ((int) bars.size()) : " (タブ譜なし)"_jp) << "\n"
         << "拍子 "_jp << where.signature.numerator << "/" << where.signature.denominator
         << "   拍 "_jp << (scoreBeats >= 0.0 ? juce::String (where.beatInBar + 1) : juce::String ("-"));
    devScoreInfo->setInfo (text);
}
#endif

void SessionScreen::toggleVideoCard()
{
    videoCardCollapsed = ! videoCardCollapsed;
    settings.setVideoCardCollapsed (videoCardCollapsed);
    videoCollapseButton.setButtonText (videoCardCollapsed ? juce::String::fromUTF8 ("▼") : juce::String::fromUTF8 ("▲"));
    videoCollapseButton.setTooltip (videoCardCollapsed ? "音源のカードを広げる"_jp : "音源のカードをたたむ"_jp);
    resized();
}

void SessionScreen::showComingSoon (const juce::String& what)
{
    setStatus (what + "は準備中です"_jp);
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

// 動作ログに、メトロノームの今の状態を書く (鳴らないときに、どこで止まっているかを見分けるため)
void SessionScreen::logMetronomeState (const juce::String& event)
{
    AppLog::write ("メトロノーム: "_jp + event
                   + " / 曲に合わせる="_jp + (metronomeOn ? "オン"_jp : "オフ"_jp)
                   + " (エンジン="_jp + (engine.isMetronomeEnabled() ? "オン"_jp : "オフ"_jp) + ")"
                   + " 単体="_jp + (engine.isMetronomeSolo() ? "オン"_jp : "オフ"_jp)
                   + " モード="_jp + (isBarMode() ? "タブ譜"_jp : "動画"_jp)
                   + " 位置="_jp + juce::String (getPosition(), 2)
                   + " 拍の起点="_jp + juce::String (engine.getSyncOffsetSeconds(), 3)
                   + " (開始位置="_jp + juce::String (session.metronomeStartMs) + "ms)"
                   + " BPM=" + juce::String (engine.getBpm(), 1)
                   + " 音量="_jp + juce::String (session.mixMetronome, 2)
                   + " クリック="_jp + juce::String (engine.getClickCount()) + "回"_jp);
}

void SessionScreen::toggleMetronome()
{
    metronomeOn = ! metronomeOn;
    engine.setMetronomeEnabled (metronomeOn);
    metronomeButton.setToggleState (metronomeOn, juce::dontSendNotification);
    logMetronomeState (metronomeOn ? "曲に合わせるをオン"_jp : "曲に合わせるをオフ"_jp);

    // 基本は、再生中に曲の拍に合わせて鳴る。止まっているときは「単体」で鳴らせる
    if (metronomeOn && ! isTransportPlaying())
        setStatus ("メトロノーム: 再生すると曲に合わせて鳴ります。「単体」でメトロノームだけ鳴らせます"_jp);
}

void SessionScreen::toggleMetronomeSolo()
{
    // 「曲に合わせる」とは独立 (作者の指示)。単体で鳴らしても「曲に合わせる」はオンにしない
    const auto solo = ! engine.isMetronomeSolo();

    engine.setMetronomeSolo (solo);
    metronomeSoloButton.setToggleState (solo, juce::dontSendNotification);
    logMetronomeState (solo ? "単体をオン"_jp : "単体をオフ"_jp);
    setStatus (solo ? "メトロノームだけを鳴らしています。再生を始めると止まります"_jp : juce::String());
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
    index = juce::jlimit (0, (int) toneButtons.size() - 1, index);
    toneButtons[(size_t) index].setToggleState (true, juce::dontSendNotification);

    if (index == pluginToneIndex)
    {
        session.tonePresetId = pluginTonePresetId;
        engine.setTone (ToneType::plugin);

        // まだ読み込んでいなければ読み込む (見つからなければ、カードにダウンロードの案内が出る)
        if (fromUser && engine.getPluginTone() == nullptr && ! engine.isPluginToneLoading())
            loadPluginTone();

        // 前のセッションから残っているプラグインを、そのまま使い始めたとき: このセッションのものとして記録する
        if (fromUser && pluginBaselineState.isEmpty())
            if (auto* tone = engine.getPluginTone())
            {
                session.pluginPath = tone->getFile().getFullPathName();
                pluginBaselineState = tone->getState().toBase64Encoding();
            }

        // 音色とミキサーの間に出たカードの全体が見えるところまで、スクロールする (resized の最後で行う)。
        // プラグインの画面は別のウィンドウで開く (読み込み中なら、できたときに開く)。
        // TONE3000 を選んだままもう一度押したときも、閉じたウィンドウをまた開く
        if (fromUser)
        {
            pluginScrollPending = true;
            pluginCard.requestOpenWindow();
        }

        resized();
    }
    else if (index == namToneIndex)
    {
        session.tonePresetId = namTonePresetId;
        engine.setTone (ToneType::nam);

        // 押しても音色の設定は開かない (作者の指示: 切り替えるだけ)。モデルが無ければ、歯車から選べることを伝える
        if (fromUser && session.namModelPath.isEmpty() && session.irPath.isEmpty())
            setStatus ("NAM のモデルと IR は、音色の歯車から選べます"_jp);
    }
    else
    {
        const auto& preset = getTonePresets()[(size_t) index];
        session.tonePresetId = preset.id;
        engine.setTone (preset.type);
    }

    updateToneSummary();

    // TONE3000 のカードを出す・しまう
    if (pluginCard.isVisible() != isPluginToneSelected())
        resized();
}

//==============================================================================
// TONE3000 (VST3 プラグイン)
bool SessionScreen::isPluginToneSelected() const noexcept
{
    return session.tonePresetId == pluginTonePresetId;
}

void SessionScreen::loadPluginTone()
{
    // セッションで選んであるファイル。なければ、標準の置き場所から TONE3000 を探す
    auto file = session.pluginPath.isNotEmpty() ? juce::File (session.pluginPath) : PluginLocator::findTone3000();

    if (! file.exists())
    {
        pluginCard.setState (PluginToneCard::State::notFound,
                             session.pluginPath.isNotEmpty()
                                 ? "プラグインが見つかりません: "_jp + session.pluginPath
                                       + "\n移動・削除していないか確かめるか、「VST3 を選ぶ…」で選び直してください。"_jp
                                 : juce::String());
        return;
    }

    // 保存した状態 (選んだ音色・つまみ)。別のプラグインに切り替えたときは使わない
    juce::MemoryBlock state;
    if (session.pluginState.isNotEmpty() && file.getFullPathName() == session.pluginPath)
        state.fromBase64Encoding (session.pluginState);

    pluginCard.setState (PluginToneCard::State::loading);

    engine.loadPluginToneAsync (file, state, [safe = juce::Component::SafePointer (this), file] (bool succeeded, const juce::String& error)
    {
        if (safe == nullptr)
            return;

        if (! succeeded)
        {
            safe->pluginCard.setState (PluginToneCard::State::error, error);
            return;
        }

        safe->session.pluginPath = file.getFullPathName();

        // 読み込んだ直後の状態を「保存されている状態」とする (つまみを動かしたら、未保存の変更になる)
        if (auto* tone = safe->engine.getPluginTone())
            safe->pluginBaselineState = tone->getState().toBase64Encoding();
        safe->pluginStateChanged = false;
    });
}

void SessionScreen::choosePluginFile()
{
    const auto folders = PluginLocator::getVst3Folders();
    fileChooser = std::make_unique<juce::FileChooser> ("VST3 プラグインを選ぶ"_jp, folders.getFirst(), "*.vst3");

    // .vst3 はフォルダのこともあるので、フォルダも選べるようにする
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer (this)] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (safe == nullptr || file == juce::File())
            return;

        safe->session.pluginPath = file.getFullPathName();
        safe->session.pluginState = {};   // 別のプラグインなので、前の状態は使わない
        safe->loadPluginTone();
    });
}

void SessionScreen::capturePluginState()
{
    // TONE3000 を使っていないセッションには書かない (前のセッションのプラグインがエンジンに残っていることがあるため)
    if (! isPluginToneSelected() && session.pluginPath.isEmpty())
        return;

    if (auto* tone = engine.getPluginTone())
    {
        const auto state = tone->getState().toBase64Encoding();
        session.pluginPath = tone->getFile().getFullPathName();
        session.pluginState = state;
        pluginBaselineState = state;
        pluginStateChanged = false;
    }
}

void SessionScreen::updatePluginEditorVisibility()
{
    // プラグインの画面は Windows の別の窓として描かれ、ほかの部品の上にはみ出す。
    // チューナーなどを重ねているとき、右のパネルを隠しているとき、スクロールで一部が隠れているときは出さない
    bool allowed = pluginCard.isVisible() && sidePanelVisible
                && tunerPanel == nullptr && tonePanel == nullptr && ! dimOverlay.isVisible();

    // 上下が見えているかで判断する (幅は、パネルを広げて収めてある。収まらないときは別のウィンドウで開いている)
    if (allowed)
    {
        const auto editorInContent = pluginCard.getEditorBounds().translated (pluginCard.getX(), pluginCard.getY());
        const auto view = sideScroll.getViewArea();
        allowed = ! editorInContent.isEmpty()
               && editorInContent.getY() >= view.getY() && editorInContent.getBottom() <= view.getBottom();
    }

    pluginCard.setEditorAllowed (allowed);
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
    if (loading)
        updateLoading();
    updateScoreOnly (juce::Time::getMillisecondCounterHiRes());
    const auto position = getPosition();
    const auto length = getLength();
    const auto playing = isTransportPlaying();

    countdown.update (juce::Time::getMillisecondCounterHiRes());
    updatePlayButton();
    updateCountdownDisplay();

    // 再生を始めたら「単体」は止める (曲に合わせて鳴らすかは「曲に合わせる」で決める)
    if (playing && ! wasPlaying && engine.isMetronomeSolo())
        toggleMetronomeSolo();

    // 再生の開始・停止を、メトロノームの状態と一緒に記録する (停止のときのクリックの回数で、鳴っていたかが分かる)
    if (playing != wasPlaying)
        logMetronomeState (playing ? "再生を開始"_jp : "再生を停止"_jp);

    checkSongEnd (playing, position, length);
    wasPlaying = playing;

    if (openContext.isSetlist())
    {
        const auto& info = setlistBar.getInfo();
        const auto fraction = length > 0.0 ? position / length : 0.0;
        if (info.count > 0)
            setlistBar.setProgress ((info.position - 1 + juce::jlimit (0.0, 1.0, fraction)) / info.count);
        setlistBar.setNotice (session.loopEnabled ? "ループ中のため自動で次に進みません"_jp : juce::String());
    }

    updateMetronomeStart();

    // 小節・拍 (一定テンポ + ずらし。設計書 3.1)。動画モードでメトロノームの開始位置があれば、そこが 1小節目
    const auto beats = (position - getMetronomeOffsetSeconds()) * session.bpm / 60.0;
    const auto bar = beats >= 0.0 ? (int) std::floor (beats / 4.0) + 1 : 0;
    const auto beat = beats >= 0.0 ? (int) std::floor (beats) % 4 + 1 : 0;
    positionLabel.setText ("小節 "_jp + juce::String (bar) + "   拍 "_jp + juce::String (beat), juce::dontSendNotification);
    scoreView.setBarAndBeat (bar, beat);
    updateMetronomeDots (beats);

   #if JUCE_DEBUG
    updateDevScoreInfo ((position - getScoreOffsetSeconds()) * session.bpm / 60.0);   // タブ譜のカーソルと同じ位置
   #endif
    // タブ譜は「拍の位置」+「タブ譜だけ」。針はページの中で、速度と経過時間からなめらかに進める
    const auto rate = isYouTube() ? youtube.getState().rate : session.speedPercent / 100.0;
    scoreViewer.setPosition (position, session.bpm, getScoreOffsetSeconds(), rate, playing);

    // YouTube: カウントダウン中に、音を消して一瞬だけ再生し、読み込みを済ませておく (作者の指示)。
    // 動画は再生を始めるまで読み込まれないので、カウントダウンのあとに読み込みの待ちが出るのを防ぐ
    if (isYouTube() && sourceReady && countdown.isCountingDown() && ! videoWarmedUp)
    {
        videoWarmedUp = true;
        youtube.warmUp();
    }

    timeline.setState (position, length, session.loopStartMs / 1000.0, session.loopEndMs / 1000.0, session.loopEnabled);

    // プラグインのつまみを動かしたか (1秒ごと。状態を取り出すのは軽くないため)
    if (--pluginStateCheckCountdown <= 0)
    {
        pluginStateCheckCountdown = 30;
        if (auto* tone = engine.getPluginTone(); tone != nullptr && pluginBaselineState.isNotEmpty())
            pluginStateChanged = tone->getState().toBase64Encoding() != pluginBaselineState;
    }

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
    engine.setGuitarMasterGain ((float) settings.getGuitarMasterPercent() / 100.0f);   // ギターの総音量 (設定画面)

    const auto& steps = settings.getWheelSteps();
    speedSlider.wheelStep = steps.speedPercent;
    bpmSlider.wheelStep = 1.0;   // BPM はいつも 1 ずつ (作者の指示。設定では変えられない)
    for (auto* slider : { &sourceGainSlider, &guitarGainSlider, &metronomeGainSlider })
        slider->wheelStep = steps.volumePercent / 100.0;

    offsetControl.setStep (steps.offsetMs);
    scoreOffsetControl.setStep (steps.offsetMs);

    sidePanelButton.setButtonText (sidePanelVisible ? ">" : "<");
}

void SessionScreen::lookAndFeelChanged()
{
    // テーマが変わったら、補助の文字の色を取り直し、タブ譜の色も合わせる
    for (auto* label : { &statusLabel, &sourceInfoLabel, &scoreNameLabel, &artistLabel })
        label->setColour (juce::Label::textColourId, findColour (ThemeColourIds::textMuted));

    scoreViewer.setColours (theme.getPalette());
    repaint();
}

void SessionScreen::toggleSidePanel()
{
    sidePanelVisible = ! sidePanelVisible;
    sidePanelButton.setButtonText (sidePanelVisible ? ">" : "<");
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

    // 再生バーの面 (タブ譜の下の帯)
    g.setColour (findColour (ThemeColourIds::panel));
    g.fillRoundedRectangle (timelinePanel.toFloat(), 8.0f);
    g.setColour (findColour (ThemeColourIds::border));
    g.drawRoundedRectangle (timelinePanel.toFloat().reduced (0.5f), 8.0f, 1.0f);

    // 下の再生操作の帯と、グループの区切り線
    const auto bottom = bottomBarArea.toFloat();
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
    const bool canShow = tunerPanel == nullptr && tonePanel == nullptr && ! loading
                      && getParentComponent() != nullptr && ! getBounds().isEmpty();
    const bool swapped = isSwapped();
    auto& videoTarget = swapped ? static_cast<juce::Component&> (scoreView) : static_cast<juce::Component&> (videoView);
    auto& scoreTarget = swapped ? static_cast<juce::Component&> (videoView) : static_cast<juce::Component&> (scoreView);
    // 音源のカードをたたんでいるときは、そこに重ねるもの (ふだんは動画、入れ替え中はタブ譜) を出さない
    const bool videoTargetVisible = swapped || (sidePanelVisible && ! videoCardCollapsed);
    const bool scoreTargetVisible = ! swapped || (sidePanelVisible && ! videoCardCollapsed);

    if (isYouTube() && canShow && videoTargetVisible)
        youtube.placeOver (videoTarget);
    else
        youtube.hide();

    if (session.scorePath.isNotEmpty() && juce::File (session.scorePath).existsAsFile() && canShow && scoreTargetVisible)
        scoreViewer.placeOver (scoreTarget);
    else
        scoreViewer.hide();

    // プラグインの画面も、WebView と同じく別の窓なので、同じときに隠す
    updatePluginEditorVisibility();
}

void SessionScreen::resized()
{
    auto area = getLocalBounds().reduced (12);

    //==== 上部 (作者のレイアウト): 「<」、曲名 — アーティスト名、(状態)、チューナー、保存、設定 ====
    auto top = area.removeFromTop (44);
    backButton.setBounds (top.removeFromLeft (48));
    top.removeFromLeft (16);
    settingsButton.setBounds (top.removeFromRight (44));   // 正方形 (上の帯の高さ)
    top.removeFromRight (8);
    saveButton.setBounds (top.removeFromRight (100));
    top.removeFromRight (8);
    tunerButton.setBounds (top.removeFromRight (110));
    top.removeFromRight (12);
    helpButton.setBounds (top.removeFromRight (40).withSizeKeepingCentre (40, 40));
    top.removeFromRight (12);

    // 曲名は文字の幅だけ取り、その右にアーティスト名。残りに状態 (「保存しました」など) を右寄せで出す
    {
        const auto textWidth = [] (const juce::Label& label)
        {
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (label.getFont(), label.getText(), 0.0f, 0.0f);
            return (int) std::ceil (glyphs.getBoundingBox (0, -1, true).getWidth()) + 12;   // ラベルの左右の余白
        };
        const auto half = top.getWidth() / 2;
        titleLabel.setBounds (top.removeFromLeft (juce::jmin (half, textWidth (titleLabel))));
        top.removeFromLeft (8);
        artistLabel.setBounds (top.removeFromLeft (artistLabel.getText().isEmpty() ? 0 : juce::jmin (top.getWidth() / 2, textWidth (artistLabel))));
        top.removeFromLeft (12);
        statusLabel.setBounds (top);
    }

    area.removeFromTop (12);

    //==== 下部: 「再生／速度／メトロノーム／ループ」のグループ。それぞれ上に見出し、間に区切り線 ====
    bottomBarArea = area.removeFromBottom (bottomBarHeight);
    auto bottom = bottomBarArea.reduced (14, 8);
    area.removeFromBottom (12);
    bottomDividers.clear();

    // 作者のレイアウト: 左から「速度 | ループ | メトロノーム | 再生」
    constexpr int gap = 6, groupGap = 28;
    constexpr int playWidth = 84 * 4 + gap * 3;
    constexpr int dotsWidth = 116;   // 拍子 ("4/4") とドット。拍子の数が多いときは、ドットを小さくして収める
    constexpr int metronomeWidth = 120 + 84 + dotsWidth + gap * 2;
    constexpr int loopWidth = 96 + 96 + 96 + gap * 2;
    const auto speedWidth = juce::jlimit (130, 360, bottom.getWidth() - playWidth - metronomeWidth - loopWidth - groupGap * 3);

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

    auto speed = group (speedHeading, speedWidth, true);
    speedSlider.setBounds (speed);

    auto loop = group (loopHeading, loopWidth, true);
    place (loop, loopStartButton, 96);
    place (loop, loopEndButton, 96);
    place (loop, loopButton, 96);

    auto metronome = group (metronomeHeading, metronomeWidth, true);
    place (metronome, metronomeButton, 120);
    place (metronome, metronomeSoloButton, 84);
    place (metronome, metronomeDots, dotsWidth);

    // 「ここから開始」は、見出しの行の「単体」の真上に小さく置く (下の帯の高さを増やさないため)
    {
        auto headingRow = metronomeHeading.getBounds();
        const auto solo = metronomeSoloButton.getBounds();
        metronomeStartButton.setBounds (solo.getX(), headingRow.getY(), solo.getWidth(), headingRow.getHeight() - 1);
        metronomeHeading.setBounds (headingRow.withRight (solo.getX() - 4));
    }

    auto play = group (playHeading, playWidth, false);
    place (play, toStartButton, 84);
    place (play, rewindButton, 84);
    place (play, playButton, 84);
    place (play, forwardButton, 84);

    //==== 右のパネルと境目 ====
    sideCards.clear();
    auto panelWidth = juce::jmin (settings.getSidePanelWidth(), area.getWidth() / 2);

    // TONE3000 のカードを出しているときは、プラグインの画面が入るよう、右のパネルを広げる (画面の幅の 6 割まで)。
    // 6 割でも入らない画面は別のウィンドウで開くので、広げない
    const bool showPlugin = isPluginToneSelected();
    if (showPlugin && ! pluginCard.getEditorNaturalSize().isEmpty())
    {
        const auto wanted = pluginCard.getEditorNaturalSize().getWidth() + 24 + sideScroll.getScrollBarThickness() + 4;
        if (wanted <= juce::roundToInt (area.getWidth() * 0.62f))
            panelWidth = juce::jmax (panelWidth, wanted);
    }

    // スクロールする部分 (sideScroll) の中の部品は、sideScroll ごと隠せばよい
    for (auto* c : std::initializer_list<juce::Component*> {
             &sourceHeading, &splitter, &videoView, &sourceInfoLabel, &sourceRemoveButton, &chooseFileButton, &pasteUrlButton,
             &sideScroll })
        c->setVisible (sidePanelVisible);

    if (sidePanelVisible)
    {
        auto right = area.removeFromRight (panelWidth);
        auto bar = area.removeFromRight (24);   // 「>」が読める幅 (ボタンの左右の余白を引いても文字が入る)
        splitter.setBounds (bar);
        sidePanelButton.setBounds (bar.withSizeKeepingCentre (24, 64));

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
        constexpr int toneHeight = headingHeight + 36 + 4 + 28 + 30;   // 音色のボタン、NAM のモデル名・プラグインの名前、プリセット
        const int tabRows = (hasLeadTab() ? 1 : 0) + (hasBackingTab() ? 1 : 0);   // タブ譜の音 (MIDI) の行
        const int mixerHeight = headingHeight + (3 + tabRows) * rowHeight;
        tabRowsShown = tabRows > 0;
        constexpr int tempoHeight = headingHeight + 3 * rowHeight + 2 * 4;
        const int sourceExtra = headingHeight + 30 + (hasSource() ? 0 : 40);
        constexpr int cardChrome = 16 + 8;   // カードの内側の余白と、カードの間
        constexpr int labelWidth = 84;       // 行の名前の列。狭くして、スライダーを長くする

        // 動画はパネルの横幅いっぱいの 16:9 (作者の指示: 縦が短すぎて小さかった)。その下のカードはスクロールするので、
        // 下に最低 160px (カード1枚ぶんほど) を残すときだけ小さくする
        constexpr int minVideoHeight = 90, minScrollHeight = 160;
        const auto videoFor16by9 = juce::roundToInt ((float) (panelWidth - 24) * 9.0f / 16.0f);
        auto videoHeight = juce::jlimit (minVideoHeight, juce::jmax (minVideoHeight, videoFor16by9),
                                         right.getHeight() - (sourceExtra + cardChrome) - minScrollHeight);

        // TONE3000: プラグインの画面がスクロールする枠に丸ごと入るか調べる。
        // 入るなら、その高さを空けるために動画を小さくする。入らなければ、カードは別のウィンドウで開く形になる
        if (showPlugin)
        {
            const auto scrollHeightAtMost = right.getHeight() - (minVideoHeight + sourceExtra + cardChrome);
            const auto contentWidthAtLeast = panelWidth - 24 - sideScroll.getScrollBarThickness() - 2;
            pluginCard.setAvailableSpace (contentWidthAtLeast, scrollHeightAtMost - 16);

            if (pluginCard.hasEditor() && ! pluginCard.isUsingWindow())
            {
                const auto pluginCardHeight = pluginCard.getPreferredHeight() + 16;
                videoHeight = juce::jlimit (minVideoHeight, 260,
                                            juce::jmin (videoFor16by9, right.getHeight() - (sourceExtra + cardChrome) - pluginCardHeight));
            }
        }

        // 音源: 見出し (右上にたたむボタン)、動画 (16:9)、その下に URL かファイル名と、外すボタン。
        // たたんでいるときは見出しだけ
        auto source = card (videoCardCollapsed ? 20 : videoHeight + sourceExtra);
        auto sourceHeadingRow = source.removeFromTop (20);
        videoCollapseButton.setBounds (sourceHeadingRow.removeFromRight (32).withSizeKeepingCentre (32, 22));
        sourceHeading.setBounds (sourceHeadingRow);
        source.removeFromTop (4);
        videoView.setBounds (source.removeFromTop (videoCardCollapsed ? 0 : videoHeight));
        videoView.setVisible (! videoCardCollapsed);

        chooseFileButton.setVisible (! hasSource() && ! videoCardCollapsed);
        pasteUrlButton.setVisible (! hasSource() && ! videoCardCollapsed);
        sourceInfoLabel.setVisible (hasSource() && ! videoCardCollapsed);
        sourceRemoveButton.setVisible (hasSource() && ! videoCardCollapsed);

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

        //==== ここから下はスクロールする部分: 音色、(TONE3000)、ミキサー、テンポ ====
        sideScroll.setBounds (right);
        pluginCard.setVisible (showPlugin);

        // 幅 width で並べ、全体の高さを返す (sideContent の中の座標)
        const auto layoutColumn = [&] (int width)
        {
            sideContent.cards.clear();
            auto column = juce::Rectangle<int> (0, 0, width, 100000);

            const auto contentCard = [&] (int height)
            {
                auto c = column.removeFromTop (height + 16);
                sideContent.cards.push_back (c);
                column.removeFromTop (8);
                return c.reduced (12, 8);
            };

            // 音色 (作者のレイアウト: 見出しの右に歯車、ボタン、その下に NAM のモデル名・プラグインの名前。
            // VST を選んでいるときは、プラグインのカードもこの枠の中に続けて置く)
            const auto pluginHeight = showPlugin ? pluginCard.getPreferredHeight() + 4 : 0;
            auto tone = contentCard (toneHeight + pluginHeight);
            auto toneHeadingRow = tone.removeFromTop (20);
            toneDetailButton.setBounds (toneHeadingRow.removeFromRight (40).withSizeKeepingCentre (40, 26));   // 歯車が「…」にならない幅
            toneHeading.setBounds (toneHeadingRow);
            tone.removeFromTop (4);
            auto toneRow = tone.removeFromTop (36);
            const auto toneWidth = (toneRow.getWidth() - 4 * (int) (toneButtons.size() - 1)) / (int) toneButtons.size();
            for (auto index : { 0, 1, namToneIndex, pluginToneIndex })   // 作者のレイアウトの順: クリーン・歪み・NAM・VST
            {
                toneButtons[(size_t) index].setBounds (toneRow.removeFromLeft (toneWidth));
                toneRow.removeFromLeft (4);
            }

            tone.removeFromTop (4);
            toneSummaryLabel.setBounds (tone.removeFromTop (28));

            // プリセット 1・2・3 (右寄せ)
            auto presetRow = tone.removeFromTop (30).withTrimmedTop (2);
            for (int i = (int) presetButtons.size() - 1; i >= 0; --i)
            {
                presetButtons[(size_t) i].setBounds (presetRow.removeFromRight (30));
                presetRow.removeFromRight (4);
            }
            presetLabel.setBounds (presetRow.removeFromRight (80));

            if (showPlugin)
            {
                tone.removeFromTop (4);
                pluginCard.setBounds (tone.removeFromTop (pluginCard.getPreferredHeight()));
            }

            // ミキサー。「音源」の行の右端にミュート (ほかの行も同じ幅を空けて、スライダーの長さを揃える)
            auto mixer = contentCard (mixerHeight);
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
                if (slider == &guitarGainSlider)
                    guitarMuteButton.setBounds (muteArea.reduced (0, 3).withTrimmedLeft (6));
                slider->setBounds (row);
            }

            // タブ譜の音 (MIDI): リード・バッキングのトラックがあるときだけ
            for (auto [label, slider, mute, shown] : { std::tuple<juce::Label*, juce::Slider*, juce::Button*, bool>
                                                           { &leadTabLabel, &leadTabSlider, &leadTabMuteButton, hasLeadTab() },
                                                       { &backingTabLabel, &backingTabSlider, &backingTabMuteButton, hasBackingTab() } })
            {
                label->setVisible (shown);
                slider->setVisible (shown);
                mute->setVisible (shown);
                if (! shown)
                    continue;

                auto row = mixer.removeFromTop (rowHeight);
                label->setBounds (row.removeFromLeft (labelWidth));
                mute->setBounds (row.removeFromRight (64).reduced (0, 3).withTrimmedLeft (6));
                slider->setBounds (row);
            }

            // テンポ: BPM と TAP、タブ譜だけのずらし、拍の位置のずらし
            auto tempo = contentCard (tempoHeight);
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

            return column.getY() - 8;   // 最後のカードの下の間は数えない
        };

        // まず全体の幅で並べ、縦に収まらなければ、スクロールバーの分だけ狭くして並べ直す
        auto contentWidth = right.getWidth();
        auto contentHeight = layoutColumn (contentWidth);
        if (contentHeight > right.getHeight())
        {
            contentWidth -= sideScroll.getScrollBarThickness() + 2;
            contentHeight = layoutColumn (contentWidth);
        }

        sideContent.setSize (contentWidth, contentHeight);
        sideContent.repaint();

        // TONE3000 を選んだとき・画面が出てきたとき: カードの全体が見えるところまでスクロールする
        if (pluginScrollPending && showPlugin)
        {
            pluginScrollPending = false;
            const auto view = sideScroll.getViewArea();
            const auto cardArea = pluginCard.getBounds().expanded (0, 8);
            if (cardArea.getY() < view.getY() || cardArea.getBottom() > view.getBottom())
                sideScroll.setViewPosition (0, juce::jmax (0, juce::jmin (cardArea.getY(), cardArea.getBottom() - view.getHeight())));
        }
    }
    else
    {
        sidePanelButton.setBounds (area.removeFromRight (24).withSizeKeepingCentre (24, 64));
        area.removeFromRight (6);
    }

    //==== 左: タブ譜の見出し、タブ譜、再生位置バー ====
    // 右寄せで「小節を同じ幅に・動画と入れ替え・タブ譜を選択・タブ譜を外す」。左はタブ譜の名前
    // (作者のレイアウトの左の「リード」「バッキング」は、タブ譜の切り替えを作るときに置く)
    auto scoreHeader = area.removeFromTop (34);
    leadButton.setBounds (scoreHeader.removeFromLeft (90).reduced (0, 2));
    scoreHeader.removeFromLeft (6);
    backingButton.setBounds (scoreHeader.removeFromLeft (90).reduced (0, 2));
    scoreHeader.removeFromLeft (12);
    scoreRemoveButton.setBounds (scoreHeader.removeFromRight (110).reduced (0, 2));
    scoreHeader.removeFromRight (6);
    scoreChooseButton.setBounds (scoreHeader.removeFromRight (110).reduced (0, 2));
    scoreHeader.removeFromRight (6);
    swapButton.setBounds (scoreHeader.removeFromRight (120).reduced (0, 2));
    scoreHeader.removeFromRight (6);
    equalBarsButton.setBounds (scoreHeader.removeFromRight (120).reduced (0, 2));
    scoreHeader.removeFromRight (12);
    positionLabel.setVisible (false);   // 「小節 ○ 拍 ○」は、作者のレイアウトに無いので出さない (タブ譜のカーソルと再生バーで分かる)
    scoreNameLabel.setBounds (scoreHeader);
    area.removeFromTop (6);

    // セットリスト用メニュー (Setlist で開いたときだけ)。右のパネルの高さを減らさないよう、左の列の下に置く
    if (openContext.isSetlist())
    {
        setlistBar.setBounds (area.removeFromBottom (SetlistBar::preferredHeight));
        area.removeFromBottom (8);
    }

    // 再生バーは面 (帯) の中に置く
    timelinePanel = area.removeFromBottom (56);
    timeline.setBounds (timelinePanel.reduced (10, 6));
    area.removeFromBottom (8);
    scoreView.setBounds (area);
    updateEmptyPrompt();

    dimOverlay.setBounds (getLocalBounds());
    loadingOverlay.setBounds (getLocalBounds());
    if (tunerPanel != nullptr)
        tunerPanel->setBounds (getLocalBounds().withSizeKeepingCentre (TunerPanel::preferredWidth, TunerPanel::preferredHeight));
    if (tonePanel != nullptr)
        tonePanel->setBounds (getLocalBounds().withSizeKeepingCentre (ToneDetailPanel::preferredWidth, ToneDetailPanel::preferredHeight));

    placeWebViews();
    repaint();
}
