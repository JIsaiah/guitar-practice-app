#include "SessionScreen.h"
#include "../Model/TonePresets.h"
#include "../Model/YouTubeUrl.h"
#include "../Audio/PluginLocator.h"
#include "../Audio/BuiltInTones.h"
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

    const char* const scoreFilePatterns = "*.gp;*.pdf;*.gpx;*.gp5;*.gp4;*.gp3;*.musicxml;*.xml";   // .pdf も (作者の指示)
    const char* const audioFilePatterns = "*.mp3;*.wav;*.flac;*.ogg;*.aif;*.aiff";
}

SessionScreen::SessionScreen (AudioEngine& e, SessionStore& s, ThemeManager& t, AppSettings& appSettings,
                              YouTubePlayer& yt, ScoreViewer& sv, Session sessionToOpen,
                              SessionOpenContext context, PlaybackSettingsResolver resolver)
    : engine (e), store (s), theme (t), youtube (yt), scoreViewer (sv), settings (appSettings),
      session (std::move (sessionToOpen)), openContext (std::move (context)), settingsResolver (std::move (resolver)),
      tapButton (t), metronomeButton (t), loopButton (t)
{
    // 音源の音量は 100% まで (作者の指示。YouTube はプレイヤーの音量が 100 までなので、全部の音源で揃える)。
    // 前に 100% より大きく保存したセッションは、ここで 100% にする (開いただけで「未保存」にならないよう、控えを取る前に)
    session.mixSource = juce::jlimit (0.0, 1.0, session.mixSource);

    savedSnapshot = juce::JSON::toString (session.toVar());
    savedBpm = session.bpm;
    savedTempoOrigin = session.tempoOrigin;

    //==== 上部 ====
    backButton.setFrameless (true);
    backButton.setIconScale (0.5f);
    backButton.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    backButton.setTooltip ("一覧に戻る"_jp);
    saveButton.setTooltip ("保存 (Ctrl+S)"_jp);
    saveButton.setIconScale (0.5f);
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
        b->onRightClick = [this, b] { if (session.isPdfScore()) showPdfPartMenu (b == &backingButton); else chooseParts (false); };
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
    // タブ譜の編集 (作者の指示。.gp だけ): 変えた音を覚えておき、保存で .gp に書き込む
    scoreViewer.onNoteEdited = [this] (const GpScoreEdit::NoteEdit& edit)
    {
        GpScoreEdit::addEdit (scoreEdits, edit);
        setStatus (scoreEdits.empty() ? "タブ譜の編集を、すべて元に戻しました"_jp
                                      : "タブ譜を変えました。保存すると、タブ譜のファイル (.gp) に書き込みます"_jp);
    };
    scoreViewer.onEditModeChanged = [this] (bool editing)
    {
        if (editing)
            setStatus ("タブ譜の編集: 数字キーでフレット (音が無ければ足す)、Delete で消す、Ctrl+Z で戻す、矢印で移動、Esc で終わる"_jp);
        else
            grabKeyboardFocus();   // 編集を終えたら、キー操作 (Space など) を練習画面に戻す
    };
    // 拍・小節の挿入 (作者の指示: 編集中の右クリックのメニューで、拍か小節かを選び、後ろに入れる)
    scoreViewer.onEditContext = [this] (const GpScoreEdit::NoteEdit& place) { showInsertMenu (place); };
    scoreViewer.onUndoInsert = [this] { undoInsert(); };
    scoreViewer.onPreviewLoaded = [this]
    {
        updateLoop();          // 小節が増えたので、A・B の小節の番号を出し直す
        updateSkipButtons();
    };
    scoreViewer.onPdfContext = [this] (double position, int repeat) { showPdfRepeatMenu (position, repeat); };
    youtube.onKey = [this] (const juce::String& key) { handlePageKey (key); };
    // 動画表示 (入れ替え中) で、動画を押すと再生・停止 (作者の指示)。再生ボタンと同じ (1回目はカウントダウン)
    youtube.onClicked = [this] { if (isSwapped()) perform (Command::togglePlay); grabKeyboardFocus(); };
    scoreView.onClicked = [this] { if (isSwapped()) perform (Command::togglePlay); };
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
    videoFold.setTarget (! videoCardCollapsed, false);
    videoFold.onChange = [this] { resized(); repaint(); };
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
    toneButtons[1].setTooltip ("アプリに入っているアンプのモデル (NAM) で歪ませます"_jp);
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

    setUpSlider (sourceGainSlider, 0.0, 1.0, 0.01, session.mixSource);   // 音源は 100% まで (作者の指示)
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
        session.muteLeadTab = ! session.muteLeadTab;
        leadTabMuteButton.setToggleState (session.muteLeadTab, juce::dontSendNotification);
        sendTabTracks();
    };
    backingTabMuteButton.onClick = [this]
    {
        session.muteBackingTab = ! session.muteBackingTab;
        backingTabMuteButton.setToggleState (session.muteBackingTab, juce::dontSendNotification);
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

    // ダブルクリックで 100% に戻す (作者の指示。前は保存されている音量に戻していた)
    for (auto* slider : { &sourceGainSlider, &guitarGainSlider, &metronomeGainSlider, &leadTabSlider, &backingTabSlider })
    {
        slider->setDoubleClickReturnValue (true, 1.0);
        slider->setTooltip ("ダブルクリックで 100% に戻す"_jp);
    }

    // 保存されているミュートの状態をボタンに出す (音への反映は applySessionToEngine と sendTabTracks で行う)
    sourceMuteButton.setToggleState (session.muteSource, juce::dontSendNotification);
    guitarMuteButton.setToggleState (session.muteGuitar, juce::dontSendNotification);
    leadTabMuteButton.setToggleState (session.muteLeadTab, juce::dontSendNotification);
    backingTabMuteButton.setToggleState (session.muteBackingTab, juce::dontSendNotification);

    //==== 右: テンポ (BPM) とずらし ====
    bpmLabel.setText ("BPM", juce::dontSendNotification);
    scoreOffsetLabel.setText ("タブ譜だけ"_jp, juce::dontSendNotification);
    scoreOffsetLabel.setTooltip ("タブ譜のカーソルだけをずらします (メトロノームと音源は合っているのに、タブ譜がずれるとき)"_jp);
    offsetLabel.setText ("拍の位置"_jp, juce::dontSendNotification);
    // PDF のタブ譜: 流れる速さ (%)。作者の指示: 小節の数より % のほうが想像しやすい
    pdfSpeedLabel.setText ("タブ譜が流れる速度"_jp, juce::dontSendNotification);
    pdfSpeedLabel.setJustificationType (juce::Justification::centredRight);
    pdfSpeedLabel.setMinimumHorizontalScale (0.7f);   // 幅が足りないときは、文字を少し細くして収める
    pdfSpeedLabel.setBorderSize ({ 0, 0, 0, 4 });     // 数字との間を詰める (作者の指示)
    pdfSpeedLabel.setTooltip ("PDF のタブ譜が流れる速さ。100% は BPM に合わせた標準の速さ (画面1つ分を 12 小節) です。速すぎたら下げ、遅すぎたら上げます。PDF の上の右クリックで繰り返しを置けます"_jp);
    // ミキサーと同じ形のスライダー (作者の指示)。範囲は 10〜400%、5% ずつ。ホイールでも 5% ずつ
    setUpSlider (pdfSpeedSlider, (double) Session::minPdfSpeedPercent, (double) Session::maxPdfSpeedPercent, 5.0, session.pdfSpeedPercent);
    showAsPercent (pdfSpeedSlider, 1.0);
    pdfSpeedSlider.wheelStep = 5.0;
    pdfSpeedSlider.setDoubleClickReturnValue (true, 100.0);
    pdfSpeedSlider.setTooltip (pdfSpeedLabel.getTooltip());
    pdfSpeedSlider.setMouseClickGrabsKeyboardFocus (false);
    pdfSpeedSlider.onValueChange = [this]
    {
        session.pdfSpeedPercent = juce::roundToInt (pdfSpeedSlider.getValue());
        scoreViewer.setPdfSpeedPercent (session.pdfSpeedPercent);
    };
    addChildComponent (pdfSpeedLabel);
    addChildComponent (pdfSpeedSlider);

    scoreStartButton.onClick = [this] { setScoreStartHere(); };
    scoreStartButton.onRightClick = [this]
    {
        if (session.scoreStartMs <= 0)
            return;

        juce::PopupMenu menu;
        menu.addItem (1, "開始位置を解除 (音源の頭から流す)"_jp);
        menu.addItem (2, "開始位置へ移動"_jp);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scoreStartButton),
                            [safe = juce::Component::SafePointer (this)] (int result)
        {
            if (safe == nullptr)
                return;
            if (result == 1)
                safe->clearScoreStart();
            if (result == 2 && safe->sourceReady)
                safe->seekTo (safe->session.scoreStartMs / 1000.0);
        });
    };
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
    tunerButton.setTooltip ("チューナー"_jp);
    tunerButton.setIconScale (0.5f);

    toStartButton.setIconScale (0.45f);
    forwardButton.setIconAfterText (true);   // 「1小節 ▶▶」(作者のレイアウト)
    toStartButton.setTooltip ("曲の頭に戻る (Home キー)"_jp);
    toStartButton.onClick = [this] { perform (Command::goToStart); };
    toEndButton.setIconScale (0.45f);
    toEndButton.setTooltip ("音源の最後へ (終了位置を決めていれば、そこへ)"_jp);
    toEndButton.onClick = [this] { perform (Command::goToEnd); };
    rewindButton.onClick = [this] { perform (Command::rewind); };
    playButton.onClick = [this] { perform (Command::togglePlay); };
    forwardButton.onClick = [this] { perform (Command::fastForward); };
    metronomeButton.onClick = [this] { perform (Command::toggleMetronome); };
    metronomeSoloButton.onClick = [this] { toggleMetronomeSolo(); };
    metronomeDots.onClick = [this] { toggleBigMetronome(); };
    metronomeDots.onRightClick = [this] { showMeterMenu(); };

    // 音源の開始・終了位置: 押すと今の位置。右クリックで解除・そこへ移動 (メトロノームの「ここから開始」と同じ)
    for (auto* b : { &sourceStartButton, &sourceEndButton })
    {
        const bool start = b == &sourceStartButton;
        b->setMouseClickGrabsKeyboardFocus (false);
        b->onClick = [this, start] { setSourceRangeHere (start); };
        b->onRightClick = [this, b, start]
        {
            if ((start ? session.sourceStartMs : session.sourceEndMs) <= 0)
                return;

            juce::PopupMenu menu;
            menu.addItem (1, start ? "開始位置を解除"_jp : "終了位置を解除"_jp);
            menu.addItem (2, start ? "開始位置へ移動"_jp : "終了位置へ移動"_jp);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (b),
                                [safe = juce::Component::SafePointer (this), start] (int result)
            {
                if (safe == nullptr)
                    return;
                if (result == 1)
                    safe->clearSourceRange (start);
                if (result == 2 && safe->sourceReady)
                    safe->seekTo ((start ? safe->session.sourceStartMs : safe->session.sourceEndMs) / 1000.0);
            });
        };
        addChildComponent (b);
    }
    updateSourceRangeButtons();

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
        scoreClock.setRate (practiceRate(), juce::Time::getMillisecondCounterHiRes());   // タブ譜だけのとき

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
             &positionLabel, &scoreChooseButton, &scoreRemoveButton, &swapButton, &scoreView, &timeline,
             &splitter, &sidePanelButton,
             &sourceHeading, &toneHeading, &mixerHeading, &offsetHeading,
             &videoView, &sourceInfoLabel, &sourceRemoveButton, &chooseFileButton, &pasteUrlButton,
             &toneButtons[0], &toneButtons[1], &toneButtons[2], &toneButtons[3],
             &toneSummaryLabel, &toneDetailButton, &presetLabel, &presetButtons[0], &presetButtons[1], &presetButtons[2],
             &sourceGainLabel, &guitarGainLabel, &metronomeGainLabel,
             &sourceGainSlider, &guitarGainSlider, &metronomeGainSlider, &sourceMuteButton, &guitarMuteButton,
             &leadTabLabel, &backingTabLabel, &leadTabSlider, &backingTabSlider, &leadTabMuteButton, &backingTabMuteButton,
             &bpmLabel, &bpmSlider, &tapButton, &scoreOffsetLabel, &scoreOffsetControl, &offsetLabel, &offsetControl,
             &scoreStartButton,
             &sideScroll, &tunerButton,
             &playHeading, &speedHeading, &metronomeHeading, &loopHeading,
             &toStartButton, &rewindButton, &playButton, &forwardButton, &toEndButton, &speedSlider, &equalBarsButton,
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
    loadDriveTone();   // 「歪み」の音色 (同梱)。一度読み込めば、ほかの曲を開いても読み込み直さない
    updatePartButtons();
    updatePresetButtons();
    updateSourceUi();
    loadSource();
    setUpYouTube();
    loadScore();
    startTimerHz (30);
    updateLoading();   // 音源もタブ譜も無いときなどは、すぐに消す

    // ラジアルメニュー: マウスの左右を同時に (または真ん中のボタンを) 0.2 秒押し続けると、カーソルのところに出る
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
    youtube.onClicked = nullptr;
    scoreViewer.onLoaded = nullptr;
    scoreViewer.onError = nullptr;
    scoreViewer.onBarClicked = nullptr;
    scoreViewer.onKey = nullptr;
    scoreViewer.onPdfContext = nullptr;
    scoreViewer.onNoteEdited = nullptr;
    scoreViewer.onEditModeChanged = nullptr;
    scoreViewer.onEditContext = nullptr;
    scoreViewer.onUndoInsert = nullptr;
    scoreViewer.onPreviewLoaded = nullptr;
    scoreViewer.setCursorHidden (false);   // ほかの画面でもページを使うので、カーソルを戻しておく
    youtube.setCursorHidden (false);
    scoreViewer.endEditing();
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
    engine.setNoiseGateDb ((float) session.noiseGateDb);
    loadNamTone();

    // VST を使うセッションなら、プラグインを読み込み、保存した状態 (選んだ音色・つまみ) に戻す。
    // プリセット 1・2・3 の VST も、すべて読み込んでおく (押したときに、すぐ切り替わるように)
    setUpPluginSlots();
    applySourceGain();
    applyGuitarGain();
    engine.setMetronomeGain ((float) session.mixMetronome);
    engine.setSpeed (session.speedPercent / 100.0);
    engine.setBpm (session.bpm);
    engine.setMetronomeEnabled (false);
    setSyncOffset (session.syncOffsetMs);
    setScoreOffset (session.scoreOffsetMs);
    updateScoreStartButton();   // 「ここから流す」の文字 (以後はタイマーで合わせる)
    updateLoop();
    updateSwapUi();
}

void SessionScreen::setTransportEnabled (bool enabled)
{
    for (auto* button : std::initializer_list<juce::Button*> { &toStartButton, &rewindButton, &playButton, &forwardButton, &toEndButton,
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
                       .withIconType (juce::MessageBoxIconType::NoIcon)
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
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon,
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
    scoreEdits.clear();   // タブ譜を読み直すと、ページの編集は消える
    scoreViewer.load (currentScoreFile(), session.scoreTrackIndex);
    updateScoreUi();
}

void SessionScreen::chooseScoreFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("タブ譜ファイルを選ぶ"_jp, juce::File(), scoreFilePatterns);
    // PDF は2つまで選べる (作者の指示: 1つ目がリード、2つ目がバッキング)
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [safe = juce::Component::SafePointer (this)] (const juce::FileChooser& chooser)
    {
        const auto files = chooser.getResults();
        if (safe == nullptr || files.isEmpty())
            return;

        const auto twoPdfs = safe->session.setScoreFiles (files);
        safe->pdfPendingRepeatStart = -1.0;
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

        if (twoPdfs)
            safe->setStatus ("PDF を2つ入れました (リード: "_jp + juce::File (safe->session.scorePath).getFileName()
                             + "、バッキング: "_jp + juce::File (safe->session.pdfBackingPath).getFileName() + ")");
    });
}

// タブ譜の拍・小節の挿入 (作者の指示: 編集中に右クリック → 拍か小節かを選び、選んでいる場所の後ろに入れる)。
// 挿入は、ページの楽譜をその場で組み替えるのではなく、編集 (scoreEdits) に足し、ファイルの楽譜に当てたものを
// ページに読み直させる (alphaTab が楽譜を一から組み立てるので、小節の番号・拍の長さ・タブ譜の音が必ずそろう)
void SessionScreen::showInsertMenu (const GpScoreEdit::NoteEdit& place)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("後ろに挿入"_jp);
    menu.addItem (1, "拍を挿入 (同じ長さ。選んでいる弦に 0 フレット)"_jp);
    menu.addItem (2, "小節を挿入 (同じ拍子の空の小節。すべてのトラック)"_jp);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer (this), place, file = currentScoreFile()] (int result)
    {
        if (safe == nullptr || result == 0 || safe->currentScoreFile() != file)
            return;
        safe->insertIntoScore (place, result == 2);
    });
}

void SessionScreen::insertIntoScore (const GpScoreEdit::NoteEdit& place, bool bar)
{
    auto select = place;   // 読み直したあとに選ぶ場所
    select.tuning.clear();

    if (bar)
    {
        GpScoreEdit::NoteEdit insert;
        insert.kind = GpScoreEdit::Kind::insertBar;
        insert.slot = place.slot;
        insert.masterBar = place.masterBar;
        GpScoreEdit::addEdit (scoreEdits, insert);

        select.masterBar = place.masterBar + 1;   // 入れた小節の最初の拍
        select.voice = 0;
        select.beat = 0;
        setStatus ("小節を挿入しました ("_jp + juce::String (place.masterBar + 2) + " 小節目)。保存すると .gp に書き込みます。Ctrl+Z で戻せます"_jp);
    }
    else
    {
        auto insert = place;
        insert.kind = GpScoreEdit::Kind::insertBeat;
        insert.tuning.clear();
        GpScoreEdit::addEdit (scoreEdits, insert);

        // 入れた拍の、選んでいる弦に 0 フレットの音 (数字キーで、すぐに変えられる)
        auto add = place;
        add.kind = GpScoreEdit::Kind::add;
        add.beat = place.beat + 1;
        add.fret = 0;
        GpScoreEdit::addEdit (scoreEdits, add);

        select.beat = place.beat + 1;
        setStatus ("拍を挿入しました。数字キーでフレットを変えられます。保存すると .gp に書き込みます。Ctrl+Z で戻せます"_jp);
    }

    scoreViewer.reloadWithEdits (scoreEdits, session.scoreTrackIndex, select);
}

void SessionScreen::undoInsert()
{
    // ページで戻せる編集が無いときの Ctrl+Z: 最後の挿入と、そのあとの編集を外して、読み直す
    // (挿入の前の編集は、読み直したときにページの「元に戻す」から消えているので、ここでは戻さない)
    const auto last = std::find_if (scoreEdits.rbegin(), scoreEdits.rend(),
                                    [] (const GpScoreEdit::NoteEdit& e) { return GpScoreEdit::isInsert (e.kind); });
    if (last == scoreEdits.rend())
    {
        setStatus ("これ以上は元に戻せません"_jp);
        return;
    }

    auto select = *last;
    select.tuning.clear();
    if (select.kind == GpScoreEdit::Kind::insertBar)
    {
        select.voice = 0;
        select.beat = 0;
    }
    scoreEdits.erase (std::prev (last.base()), scoreEdits.end());

    setStatus (select.kind == GpScoreEdit::Kind::insertBar ? "小節の挿入を元に戻しました"_jp : "拍の挿入を元に戻しました"_jp);
    scoreViewer.reloadWithEdits (scoreEdits, session.scoreTrackIndex, select);
}

// PDF の繰り返し (作者の指示: 繰り返し記号を手で置き、決めた回数だけ戻って流す)。
// PDF の上で右クリック → 「ここから繰り返し」を置き、終わりで右クリック → 「ここまで繰り返し」で回数を選ぶ
void SessionScreen::sendPdfRepeats()
{
    juce::Array<juce::var> list;
    for (const auto& r : session.currentPdfRepeats())
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("start", r.start);
        object->setProperty ("end", r.end);
        object->setProperty ("count", r.count);
        list.add (juce::var (object));
    }
    scoreViewer.setPdfRepeats (list, pdfPendingRepeatStart);
}

void SessionScreen::showPdfRepeatMenu (double position, int repeat)
{
    if (! session.isPdfScore())
        return;

    auto& repeats = session.currentPdfRepeats();
    const auto onRepeat = juce::isPositiveAndBelow (repeat, (int) repeats.size());
    enum { startId = 1, cancelStartId = 2, removeId = 3, removeAllId = 4, endBase = 100, countBase = 200 };

    juce::PopupMenu menu;
    menu.addItem (startId, "ここから繰り返し"_jp);

    juce::PopupMenu endMenu;
    for (int n = 2; n <= Session::maxPdfRepeatCount; ++n)
        endMenu.addItem (endBase + n, juce::String (n) + " 回弾く"_jp);
    const auto canEnd = pdfPendingRepeatStart >= 0.0 && position > pdfPendingRepeatStart;
    menu.addSubMenu (canEnd ? "ここまで繰り返し"_jp : "ここまで繰り返し (先に「ここから繰り返し」)"_jp, endMenu, canEnd);

    if (pdfPendingRepeatStart >= 0.0)
        menu.addItem (cancelStartId, "置きかけの「ここから」を消す"_jp);

    if (onRepeat)
    {
        menu.addSeparator();
        juce::PopupMenu countMenu;
        for (int n = 2; n <= Session::maxPdfRepeatCount; ++n)
            countMenu.addItem (countBase + n, juce::String (n) + " 回弾く"_jp, true, repeats[(size_t) repeat].count == n);
        menu.addSubMenu ("この繰り返しの回数"_jp, countMenu);
        menu.addItem (removeId, "この繰り返しを消す"_jp);
    }

    menu.addSeparator();
    menu.addItem (removeAllId, "繰り返しをすべて消す"_jp, ! repeats.empty() || pdfPendingRepeatStart >= 0.0);

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer (this), position, repeat,
                         file = session.currentScorePath()] (int result)
    {
        // メニューを出している間に、表示する PDF が変わっていたら何もしない
        if (safe == nullptr || result == 0 || safe->session.currentScorePath() != file)
            return;

        auto& list = safe->session.currentPdfRepeats();
        if (result == startId)
        {
            safe->pdfPendingRepeatStart = position;
            safe->setStatus ("繰り返しの始めを置きました。終わりの場所で右クリックし「ここまで繰り返し」を選んでください"_jp);
        }
        else if (result == cancelStartId)
        {
            safe->pdfPendingRepeatStart = -1.0;
        }
        else if (result > endBase && result <= endBase + Session::maxPdfRepeatCount)
        {
            if (Session::addPdfRepeat (list, { safe->pdfPendingRepeatStart, position, result - endBase }))
                safe->setStatus ("繰り返しを置きました ("_jp + juce::String (result - endBase) + " 回弾く)"_jp);
            safe->pdfPendingRepeatStart = -1.0;
        }
        else if (result > countBase && result <= countBase + Session::maxPdfRepeatCount)
        {
            if (juce::isPositiveAndBelow (repeat, (int) list.size()))
                list[(size_t) repeat].count = result - countBase;
        }
        else if (result == removeId)
        {
            if (juce::isPositiveAndBelow (repeat, (int) list.size()))
                list.erase (list.begin() + repeat);
        }
        else if (result == removeAllId)
        {
            list.clear();
            safe->pdfPendingRepeatStart = -1.0;
        }

        safe->sendPdfRepeats();
    });
}

// PDF のリード (1つ目)・バッキング (2つ目) の PDF を選ぶ。表示もそのパートに切り替える
void SessionScreen::choosePdfPart (bool backing)
{
    fileChooser = std::make_unique<juce::FileChooser> (backing ? "バッキングの PDF を選ぶ"_jp : "リードの PDF を選ぶ"_jp,
                                                       juce::File (session.scorePath).getParentDirectory(), "*.pdf");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer (this), backing] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (safe == nullptr || file == juce::File() || ! file.hasFileExtension ("pdf") || ! safe->session.isPdfScore())
            return;

        // 選んだ PDF を読み込んで表示し、そのパートに切り替える (プリセットの音色・知らせは switchPart)
        (backing ? safe->session.pdfBackingPath : safe->session.scorePath) = file.getFullPathName();
        (backing ? safe->session.pdfBackingRepeats : safe->session.pdfRepeats).clear();   // 別の PDF なので
        safe->pdfPendingRepeatStart = -1.0;
        safe->session.backingActive = backing;
        safe->scoreEdits.clear();   // タブ譜を読み直すと、ページの編集は消える
        safe->scoreViewer.load (safe->currentScoreFile(), 0);
        safe->updateScoreUi();
        safe->switchPart (backing, true);
    });
}

void SessionScreen::showPdfPartMenu (bool backing)
{
    juce::PopupMenu menu;
    if (backing)
    {
        menu.addItem (1, session.hasPdfBacking() ? "バッキングの PDF を選び直す…"_jp : "バッキングの PDF を選ぶ…"_jp);
        menu.addItem (2, "バッキングの PDF を外す"_jp, session.hasPdfBacking());
    }
    else
    {
        menu.addItem (1, "リードの PDF を選び直す…"_jp);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe = juce::Component::SafePointer (this), backing] (int result)
    {
        if (safe == nullptr)
            return;

        if (result == 1)
            safe->choosePdfPart (backing);

        if (result == 2)
        {
            safe->session.pdfBackingPath = {};
            safe->session.pdfBackingRepeats.clear();
            safe->pdfPendingRepeatStart = -1.0;
            safe->session.backingActive = false;
            safe->scoreEdits.clear();   // タブ譜を読み直すと、ページの編集は消える
            safe->scoreViewer.load (safe->currentScoreFile(), 0);
            safe->updateScoreUi();
            safe->switchPart (false, true);
        }
    });
}

void SessionScreen::removeScore()
{
    session.scorePath = {};
    session.pdfBackingPath = {};
    session.pdfRepeats.clear();
    session.pdfBackingRepeats.clear();
    pdfPendingRepeatStart = -1.0;
    session.scoreTrackIndex = 0;
    resetParts();
    scoreEdits.clear();   // タブ譜を読み直すと、ページの編集は消える
    scoreViewer.load ({}, 0);
    updateScoreUi();
    updateLoop();   // 4/4 の小節に戻るので、小節の番号を出し直す
    updateSkipButtons();
}

void SessionScreen::updateScoreUi()
{
    const juce::File file (session.scorePath);
    scoreRemoveButton.setEnabled (session.scorePath.isNotEmpty());
    updatePdfUi();

    if (session.scorePath.isNotEmpty() && ! file.existsAsFile())
        setStatus ("タブ譜のファイルが見つかりません: "_jp + session.scorePath);
    else if (session.hasPdfBacking() && ! juce::File (session.pdfBackingPath).existsAsFile())
        setStatus ("バッキングの PDF が見つかりません: "_jp + session.pdfBackingPath);

    updateSwapUi();
    updateEmptyPrompt();
    placeWebViews();
}

// PDF のタブ譜のとき: 「小節を同じ幅に」は使えない (小節の情報が無い)。リード・バッキングは PDF の1つ目・2つ目を切り替える。
// リード・バッキングの右に「速さ ○ %」(流れる速さ) を出す。小節の操作 (ループ・戻る・進む) は、小節が無いので自動で秒単位になる
void SessionScreen::updatePdfUi()
{
    const auto pdf = session.isPdfScore();
    const auto hasScore = session.scorePath.isNotEmpty();
    const auto layoutChanged = pdf != pdfSpeedSlider.isVisible() || hasScore != leadButton.isVisible();
    pdfSpeedLabel.setVisible (pdf);
    pdfSpeedSlider.setVisible (pdf);
    // PDF のときは「小節を同じ幅に」を隠す (使えないうえに、「速さ」の欄のぶん幅が足りず、「ここから流す」が見えなくなるため)
    equalBarsButton.setVisible (hasScore && ! pdf);
    // タブ譜が無いときは、タブ譜のためのボタンを出さない (作者の指示: 今のセッションと関係ないボタンは隠す)。
    // タブ譜を読み込んだら出す
    for (auto* b : std::initializer_list<juce::Component*> { &leadButton, &backingButton, &scoreStartButton, &scoreRemoveButton })
        b->setVisible (hasScore);

    if (pdf)
    {
        scoreViewer.setPdfSpeedPercent (session.pdfSpeedPercent);
        sendPdfRepeats();
    }

    updatePartButtons();
    if (layoutChanged)
        resized();   // 見出しの並びが変わる
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
        scoreClock.seek (clampToLength (seconds), juce::Time::getMillisecondCounterHiRes());
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
    if (hasSource() || session.scorePath.isEmpty())
        return false;

    // PDF: 小節の情報は無いが、BPM に合わせて流せるので、タブ譜の時計で再生する (作者の指示)。
    // 曲の長さは分からないので、再生バーは進まず、最後で止まることもない (止めるまで流れる)。
    // 読み込みが終わったかではなくファイルがあるかで決める (リード・バッキングを切り替えて読み直す間に、時計が 0 に戻らないように)
    if (session.isPdfScore())
        return currentScoreFile().existsAsFile();

    return ! scoreViewer.getBarStartBeats().empty();
}

// 下の帯の「速度」は音源の速度 (作者の指示)。音源が無い PDF のセッションでは使えない
// (PDF の流れる速さは、見出しの「タブ譜が流れる速度」で決める)。
// .gp だけのセッションは、タブ譜の音 (MIDI) が音源の代わりなので、今までどおり使える
bool SessionScreen::usesPracticeSpeed() const
{
    return hasSource() || ! session.isPdfScore();
}

double SessionScreen::practiceRate() const
{
    return usesPracticeSpeed() ? session.speedPercent / 100.0 : 1.0;
}

void SessionScreen::updateSpeedEnabled (double nowMs)
{
    const auto usable = usesPracticeSpeed();
    if (speedSlider.isEnabled() == usable)
        return;

    speedSlider.setEnabled (usable);   // 使えないときは薄くなる (値はそのまま残し、音源を入れたらまた効く)
    speedHeading.setAlpha (usable ? 1.0f : 0.5f);
    scoreClock.setRate (practiceRate(), nowMs);
}

double SessionScreen::clampToLength (double seconds) const
{
    // 長さが分からない (PDF だけ) ときは、0 より前に行かないようにだけする
    const auto length = getLength();
    return length > 0.0 ? juce::jlimit (0.0, length, seconds) : juce::jmax (0.0, seconds);
}

double SessionScreen::getScoreLengthSeconds() const
{
    const auto& bars = scoreViewer.getBarStartBeats();
    const auto& signatures = scoreViewer.getBarSignatures();
    if (bars.empty() || session.bpm <= 0.0)
        return 0.0;

    // 曲の終わりの拍 (ページが、繰り返しを展開して数えたもの)。無ければ、最後の小節の始まり + その小節の長さ (拍子から)
    auto endBeats = scoreViewer.getScoreEndBeats();
    if (endBeats <= bars.back())
    {
        const auto last = signatures.empty() ? MusicInfo::TimeSignature {} : signatures.back();
        endBeats = bars.back() + (last.denominator > 0 ? last.numerator * 4.0 / last.denominator : 4.0);
    }
    return getScoreOffsetSeconds() + endBeats * 60.0 / session.bpm;
}

void SessionScreen::updateScoreOnly (double nowMs)
{
    const auto scoreOnly = isScoreOnly();
    updateSpeedEnabled (nowMs);

    // タブ譜だけになった (タブ譜を読み込んだ・音源を外した) / 音源が入った
    if (scoreOnly != scoreOnlyActive)
    {
        scoreOnlyActive = scoreOnly;
        scoreClock.pause (nowMs);
        scoreClock.seek (0.0, nowMs);
        scoreClock.setRate (practiceRate(), nowMs);
        // 音源が入ったときは、音源の読み込みの側が準備の状態を決める (読み込みが先に終わることもあるので、ここでは触らない)
        if (scoreOnly || ! hasSource())
        {
            sourceReady = scoreOnly;
            setTransportEnabled (scoreOnly);
        }

        if (scoreOnly)
            setStatus (session.isPdfScore() ? "音源がないので、タブ譜だけを流します (再生バーは進みません)"_jp
                                            : "音源がないので、タブ譜の音で再生します"_jp);
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
    return juce::JSON::toString (session.toVar()) != savedSnapshot || pluginStateChanged || ! scoreEdits.empty();
}

bool SessionScreen::writeToDisk()
{
    auto split = settingsResolver.split (session);

    // セットリストで弾いているときは、ファイルの更新日時を変えない (作者の指示: セットリストの曲が、
    // 一覧の並び (最後の練習日。開いたことが無いセッションは更新日時で代える) を変えないように)
    const auto fileOf = [this] (const juce::String& id) { return store.getDirectory().getChildFile (id + SessionStore::fileExtension); };
    const auto keepModified = openContext.isSetlist() && split.session.id.isNotEmpty() && fileOf (split.session.id).existsAsFile();
    const auto modified = keepModified ? fileOf (split.session.id).getLastModificationTime() : juce::Time();

    if (! store.save (split.session))
        return false;

    if (keepModified)
        fileOf (split.session.id).setLastModificationTime (modified);   // 曲名を変えて名前が変わっていても、新しいファイルに

    session.id = split.session.id;   // 初めて保存したときに採番される
    session.sessionId = split.session.sessionId;

    if (split.overrides.has_value() && onSaveOverrides != nullptr)
        return onSaveOverrides (*split.overrides);

    return true;
}

bool SessionScreen::save()
{
    capturePluginState();   // TONE3000 で選んだ音色・つまみも保存する

    // タブ譜の編集を .gp のファイルに書き込む (初めてのときは、元のファイルを「.gp.bak」に残す)
    if (! scoreEdits.empty())
    {
        const auto result = GpScoreEdit::applyToGpFile (currentScoreFile(), scoreEdits);
        if (result.failed())
        {
            setStatus ("タブ譜を保存できませんでした: "_jp + result.getErrorMessage());
            return false;
        }
        scoreEdits.clear();
        scoreViewer.clearPreviewEdits();   // ファイルが編集どおりになったので、ページに渡すときに当て直さない
    }

    if (! writeToDisk())
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
                       .withIconType (juce::MessageBoxIconType::NoIcon)
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
        case Command::goToStart:       if (sourceReady) seekTo (getPlayStart()); break;   // 開始位置があれば、そこへ
        case Command::goToEnd:         if (sourceReady) seekTo (getPlayEnd (getLength())); break;
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
    // Esc: チューナーを閉じる (作者の指示)
    if (key == juce::KeyPress::escapeKey && tunerPanel != nullptr) { perform (Command::toggleTuner); return true; }
    if (key == juce::KeyPress::spaceKey)  { perform (Command::togglePlay); return true; }
    if (key == juce::KeyPress::returnKey) { perform (Command::tapTempo); return true; }
    if (key == juce::KeyPress::leftKey)   { perform (Command::rewind); return true; }
    if (key == juce::KeyPress::homeKey)   { perform (Command::goToStart); return true; }
    if (key == juce::KeyPress::rightKey)  { perform (Command::fastForward); return true; }
    if (key == juce::KeyPress ('s', juce::ModifierKeys::ctrlModifier, 0)) { perform (Command::save); return true; }
    // タブ譜の編集の、元に戻す・やり直す (.gp のとき。ページがキーを持っているときは、ページが自分で受け取る)
    if (key == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier, 0)) { scoreViewer.undoEdit(); return true; }
    if (key == juce::KeyPress ('y', juce::ModifierKeys::ctrlModifier, 0)
        || key == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0)) { scoreViewer.redoEdit(); return true; }
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
    const bool scoreShown = session.scorePath.isNotEmpty() && currentScoreFile().existsAsFile();
    const auto target = number == 0 && caption.isEmpty() ? CountdownTarget::none
                      : isSwapped()  ? (isYouTube() ? CountdownTarget::video : CountdownTarget::native)
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
        const auto length = getScoreLengthSeconds();   // PDF は 0 (長さが分からない): 頭に戻さず、そのまま続ける
        if (length > 0.0 && scoreClock.getPosition (now) >= length - 0.05)
            scoreClock.seek (0.0, now);
        scoreClock.play (now);
        return;
    }

    // 音源の開始・終了位置 (作者の指示): 開始位置より前・終了位置より後ろからは、開始位置から始める
    if (hasSourceRange())
    {
        const auto position = getPosition();
        if (position < getPlayStart() - 0.05 || position >= getPlayEnd (getLength()) - 0.05)
            seekTo (getPlayStart());
    }

    if (isYouTube())
    {
        youtube.play();   // 最後まで再生していたら、YouTube が頭から再生する
        return;
    }

    // 最後まで再生していたら、頭から
    if (engine.getPositionSeconds() >= engine.getSourceLengthSeconds() - 0.05)
        engine.seekSeconds (getPlayStart());

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
    // 終了位置を決めていれば、そこが曲の終わり
    const auto end = getPlayEnd (length);
    const bool reachedEnd = (end > 0.0 && position >= end - 0.25)
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
        seekTo (clampToLength (getPosition() + seconds));
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

    seekTo (clampToLength (barStartSeconds (juce::jmax (0, target))));
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

    auto where = MusicInfo::locateBeat (beats, bars, signatures);
    if (! scoreBars)
    {
        // .gp が無いとき: 手で決めた拍子 (作者の指示)。1小節は分子の数の拍
        const auto meter = getManualMeter();
        where.signature = meter;
        where.beatInBar = beats >= 0.0 ? (int) std::floor (beats) % juce::jmax (1, meter.numerator) : 0;
    }
    engine.setMetronomeBeatsPerBar (scoreBars ? 4 : session.meterNumerator);   // 1拍目を強く鳴らす間隔
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

    // 番号は楽譜の小節の番号 (繰り返しのある曲では、演奏する順の何番目かとは違う)
    const auto numberOf = [this] (int index) { return usesScoreBars() ? scoreViewer.getBarNumber (index) : index + 1; };
    const auto firstBar = numberOf (barIndexNear (session.loopStartMs / 1000.0));
    const auto lastBar = numberOf (juce::jmax (0, barIndexNear (session.loopEndMs / 1000.0) - 1));

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
    // 動画モード: メトロノームの開始位置から数える。タブ譜モード: タブ譜の開始位置から数える (タブ譜の小節と合うように)
    const auto start = isMetronomeStartActive() ? session.metronomeStartMs
                                                : (isBarMode() ? getScoreStartMs() : 0);
    return (session.syncOffsetMs + start) / 1000.0;
}

void SessionScreen::setMetronomeStartHere()
{
    if (! sourceReady)
        return;

    session.metronomeStartMs = juce::jmax (1, juce::roundToInt (getPosition() * 1000.0));   // 0 は「指定なし」なので 1ms 以上
    setStatus ("メトロノームを "_jp + formatTime (session.metronomeStartMs / 1000.0) + " から始めます"_jp);
    updateMetronomeStart();
}

//==============================================================================
// メトロノームの拍子 (作者の指示: .gp が無いときに手で決める。例: 8/9 にすると、8拍ごとに1拍目を強く鳴らす)
void SessionScreen::showMeterMenu()
{
    juce::PopupMenu menu;
    if (usesScoreBars())
    {
        menu.addItem (0, "タブ譜 (.gp) の拍子を使っています"_jp, false);
    }
    else
    {
        menu.addSectionHeader ("メトロノームの拍子"_jp);
        static const int common[][2] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 } };
        int id = 1;
        for (const auto& m : common)
            menu.addItem (id++, juce::String (m[0]) + "/" + juce::String (m[1]), true,
                          session.meterNumerator == m[0] && session.meterDenominator == m[1]);
        menu.addSeparator();
        menu.addItem (100, "ほかの拍子を入力…"_jp);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&metronomeDots),
                        [safe = juce::Component::SafePointer (this)] (int result)
    {
        if (safe == nullptr || result <= 0)
            return;

        static const int common[][2] = { { 2, 4 }, { 3, 4 }, { 4, 4 }, { 5, 4 }, { 6, 8 }, { 7, 8 }, { 9, 8 }, { 12, 8 } };
        if (result <= (int) std::size (common))
        {
            safe->setMeter (common[result - 1][0], common[result - 1][1]);
            return;
        }

        // ほかの拍子: 「8/9」のように入れる
        auto* window = new juce::AlertWindow ("メトロノームの拍子"_jp, "「8/9」のように、分子/分母 を入れてください (分子が1小節の拍の数)"_jp,
                                              juce::MessageBoxIconType::NoIcon, safe.getComponent());
        window->addTextEditor ("meter", juce::String (safe->session.meterNumerator) + "/" + juce::String (safe->session.meterDenominator));
        window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("キャンセル"_jp, 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int ok)
        {
            if (safe == nullptr || ok != 1)
                return;
            const auto text = window->getTextEditorContents ("meter").trim();
            const auto n = text.upToFirstOccurrenceOf ("/", false, false).trim().getIntValue();
            const auto d = text.containsChar ('/') ? text.fromFirstOccurrenceOf ("/", false, false).trim().getIntValue() : 4;
            if (n < 1 || n > Session::maxMeterNumerator || d < 1 || d > Session::maxMeterDenominator)
            {
                safe->setStatus ("拍子は 1/1 から "_jp + juce::String (Session::maxMeterNumerator) + "/" + juce::String (Session::maxMeterDenominator)
                                 + " の間で入れてください"_jp);
                return;
            }
            safe->setMeter (n, d);
        }), true);
    });
}

void SessionScreen::setMeter (int numerator, int denominator)
{
    session.meterNumerator = juce::jlimit (1, Session::maxMeterNumerator, numerator);
    session.meterDenominator = juce::jlimit (1, Session::maxMeterDenominator, denominator);
    setStatus ("メトロノームの拍子を "_jp + juce::String (session.meterNumerator) + "/" + juce::String (session.meterDenominator)
               + " にしました"_jp);
}

//==============================================================================
// 音源の開始・終了位置 (作者の指示)
double SessionScreen::getPlayStart() const
{
    return session.sourceStartMs / 1000.0;
}

double SessionScreen::getPlayEnd (double length) const
{
    return session.sourceEndMs > 0 ? session.sourceEndMs / 1000.0 : length;
}

void SessionScreen::setSourceRangeHere (bool start)
{
    if (! sourceReady)
        return;

    const auto ms = juce::jmax (1, juce::roundToInt (getPosition() * 1000.0));   // 0 は「指定なし」なので 1ms 以上
    // 開始は終了より前、終了は開始より後ろでないといけない。逆になったら、もう片方を外す
    if (start)
    {
        session.sourceStartMs = ms;
        if (session.sourceEndMs > 0 && session.sourceEndMs <= ms)
            session.sourceEndMs = 0;
    }
    else
    {
        session.sourceEndMs = ms;
        if (session.sourceStartMs > 0 && session.sourceStartMs >= ms)
            session.sourceStartMs = 0;
    }

    setStatus ((start ? "音源の開始位置を "_jp : "音源の終了位置を "_jp) + formatTime (ms / 1000.0) + " にしました"_jp);
    updateSourceRangeButtons();
}

void SessionScreen::clearSourceRange (bool start)
{
    (start ? session.sourceStartMs : session.sourceEndMs) = 0;
    setStatus (start ? "音源の開始位置を解除しました"_jp : "音源の終了位置を解除しました"_jp);
    updateSourceRangeButtons();
}

void SessionScreen::updateSourceRangeButtons()
{
    sourceStartButton.setEnabled (sourceReady);
    sourceEndButton.setEnabled (sourceReady);
    sourceStartButton.setButtonText (session.sourceStartMs > 0 ? "開始 "_jp + formatTime (session.sourceStartMs / 1000.0) : "開始位置"_jp);
    sourceEndButton.setButtonText (session.sourceEndMs > 0 ? "終了 "_jp + formatTime (session.sourceEndMs / 1000.0) : "終了位置"_jp);
    sourceStartButton.setTooltip ("今の位置を、音源の開始位置にします (頭へ・再生はここから)。右クリックで解除"_jp);
    sourceEndButton.setTooltip ("今の位置を、音源の終了位置にします (ここで曲の終わりとして止まる)。右クリックで解除"_jp);
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

//==============================================================================
// タブ譜の開始位置 (「ここから流す」)
//   再生バーで、タブ譜の1小節目が始まる位置 (前奏の後など) を選んでボタンを押すと、そこにタブ譜の頭を合わせる。
//   タブ譜のカーソル・タブ譜の音・小節のクリック・ループは、getScoreOffsetSeconds() を通して付いてくる。
//   それより前は、カーソルは1小節目で待ち、タブ譜の音は鳴らない。音源が無い (タブ譜だけ) ときは使わない
void SessionScreen::setScoreStartHere()
{
    if (! sourceReady || ! hasSource())
        return;

    // 0 は「指定なし (音源の頭)」と同じ意味なので、そのまま 0 を入れてよい
    session.scoreStartMs = juce::jmax (0, juce::roundToInt (getPosition() * 1000.0));
    setStatus ("タブ譜を "_jp + formatTime (session.scoreStartMs / 1000.0) + " から流します"_jp);
    updateScoreStartButton();
    updateMetronomeStart();   // タブ譜モードでは、メトロノームの起点も変わる
}

void SessionScreen::clearScoreStart()
{
    session.scoreStartMs = 0;
    setStatus ("タブ譜の開始位置を解除しました (音源の頭から流します)"_jp);
    updateScoreStartButton();
    updateMetronomeStart();
}

void SessionScreen::updateScoreStartButton()
{
    const bool hasScore = ! scoreViewer.getBarStartBeats().empty();
    scoreStartButton.setEnabled (hasSource() && sourceReady && hasScore);
    scoreStartButton.setButtonText (session.scoreStartMs > 0 ? formatTime (session.scoreStartMs / 1000.0) + " から"_jp
                                                             : "ここから流す"_jp);
    scoreStartButton.setTooltip (hasSource()
        ? "再生バーで、タブ譜の1小節目が始まる位置を選んでから押すと、そこからタブ譜が流れます。右クリックで解除・移動"_jp
        : "音源があるときに使えます"_jp);
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
    seekTo (clampToLength (seconds));
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

    // タブ譜の編集中は、キーをページに残す (数字キーで続けて編集できるように)
    if (! scoreViewer.isEditing())
        grabKeyboardFocus();
}

//==============================================================================
// タブ譜と動画 (音源) の入れ替え。YouTube とローカル音源 (作者の指示で、ローカル音源でもできるようにした)
// ローカル音源には映像が無いので、左の大きな枠には、今の時刻と音源の名前を大きく出す (ScoreView の音源の表示)
void SessionScreen::toggleSwap()
{
    if (! hasSource())
        return;

    session.videoInMainArea = ! session.videoInMainArea;
    updateSourceUi();
}

void SessionScreen::updateSwapUi()
{
    swapButton.setEnabled (hasSource());
    // 入れ替えは「オン・オフ」ではなく「切り替え」なので、押したあともアクセントの色にしない (作者の指示)
    swapButton.setTooltip (isYouTube()       ? "タブ譜と動画の場所を入れ替えます (タブ譜つきの動画で練習するとき)"_jp
                           : isLocalSource() ? "タブ譜と音源の場所を入れ替えます。ループ・戻る・進むが秒単位になります"_jp
                                             : "音源があるときに使えます"_jp);
    // ローカル音源を入れ替えたとき: 左の大きな枠に、今の時刻と音源の名前を出す (クリックで再生・停止)
    scoreView.setAudioMode (isSwapped() && isLocalSource(), juce::File (session.sourcePath).getFileName());
    sourceHeading.setText (isSwapped() ? "タブ譜"_jp : "音源"_jp, juce::dontSendNotification);   // 「(動画と入れ替え中)」は出さない (作者の指示)
    updateSkipButtons();
    updateLoop();   // ループの A・B の表示 (小節 ⇔ 時刻) を合わせる

    if (isSwapped())
        videoView.setText (session.scorePath.isEmpty() ? "タブ譜がありません"_jp : juce::String());
}

//==============================================================================
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
    session.muteSource = ! session.muteSource;
    sourceMuteButton.setToggleState (session.muteSource, juce::dontSendNotification);
    applySourceGain();
    setStatus (session.muteSource ? "音源をミュートしました (タブ譜とメトロノームはそのまま動きます)"_jp : juce::String());
}

void SessionScreen::toggleGuitarMute()
{
    session.muteGuitar = ! session.muteGuitar;
    guitarMuteButton.setToggleState (session.muteGuitar, juce::dontSendNotification);
    applyGuitarGain();
    setStatus (session.muteGuitar ? "ギターをミュートしました"_jp : juce::String());
}

void SessionScreen::applyGuitarGain()
{
    // 音源のミュートと同じく、スライダーの値 (session.mixGuitar) は変えない。解除すると元の音量に戻る
    engine.setGuitarGain (session.muteGuitar ? 0.0f : (float) session.mixGuitar);
    guitarGainSlider.setAlpha (session.muteGuitar ? 0.5f : 1.0f);
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
    // PDF: トラックは無い。リード・バッキングは PDF の1つ目・2つ目 (タブ譜の音も無いので、トラックの番号は使わない)
    if (session.isPdfScore())
    {
        session.leadTrack = session.backingTrack = -1;
        if (! session.hasPdfBacking())
            session.backingActive = false;
        updatePartButtons();
        return;
    }

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

    if (session.isPdfScore())
    {
        // PDF: バッキングの PDF がまだ無ければ、選んでもらう (選んだら、そのまま切り替わる)
        if (backing && ! session.hasPdfBacking())
        {
            updatePartButtons();   // 押してオンになったボタンを戻す
            if (fromUser)
                choosePdfPart (true);
            return;
        }

        // 表示する PDF が変わったら読み直す
        const auto before = session.currentScorePath();
        session.backingActive = backing;
        if (session.currentScorePath() != before)
        {
            pdfPendingRepeatStart = -1.0;
            scoreEdits.clear();   // タブ譜を読み直すと、ページの編集は消える
            scoreViewer.load (currentScoreFile(), 0);
            updateScoreUi();
        }
    }
    else
    {
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
        tracks.push_back ({ session.leadTrack, session.mixLeadTab, session.muteLeadTab });
    if (hasBackingTab())
        tracks.push_back ({ session.backingTrack, session.mixBackingTab, session.muteBackingTab });
    scoreViewer.setMidiTracks (tracks);

    leadTabSlider.setAlpha (session.muteLeadTab ? 0.5f : 1.0f);
    backingTabSlider.setAlpha (session.muteBackingTab ? 0.5f : 1.0f);
}

void SessionScreen::updatePartButtons()
{
    sendTabTracks();

    // ミキサーのタブ譜の行 (リード・バッキング) は、そのトラックがあるときだけ出す
    if ((hasLeadTab() || hasBackingTab()) != tabRowsShown)
        resized();

    const auto& tracks = scoreViewer.getTracks();

    // PDF: リードは1つ目の PDF、バッキングは2つ目の PDF。バッキングが無くても押せる (押すと PDF を選ぶ)
    if (session.isPdfScore())
    {
        const auto showingBacking = session.hasPdfBacking() && session.backingActive;
        leadButton.setEnabled (true);
        backingButton.setEnabled (true);
        leadButton.setToggleState (! showingBacking, juce::dontSendNotification);
        backingButton.setToggleState (showingBacking, juce::dontSendNotification);
        leadButton.setTooltip ("リード: "_jp + juce::File (session.scorePath).getFileName() + "。右クリックで PDF を選び直せます"_jp);
        backingButton.setTooltip (session.hasPdfBacking()
                                      ? "バッキング: "_jp + juce::File (session.pdfBackingPath).getFileName() + "。右クリックで選び直す・外す"_jp
                                      : "バッキングの PDF がありません。押すと選べます"_jp);
        return;
    }

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

    // VST を選んでいるときの、空のプリセット 2・3: VST を選んでもらう (作者の指示。1 は最初に選んだ VST になる)
    if (session.toneSlots[(size_t) slot - 1].isEmpty() && isPluginToneSelected() && slot >= 2)
    {
        chooseVstForPreset (slot);
        return;
    }

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
    const bool hasTwoParts = (session.leadTrack >= 0 && session.backingTrack >= 0) || session.hasPdfBacking();
    bool hasPresets = false;
    for (const auto& slot : session.toneSlots)
        hasPresets = hasPresets || ! slot.isEmpty();

    return {
        { "再生・停止"_jp,               [this] { perform (Command::togglePlay); } },
        { session.loopEnabled ? "ループ解除"_jp : "ループ"_jp,
                                         [this] { perform (Command::toggleLoop); }, hasLoop },
        { "右の音色プリセット"_jp,       [this] { stepPreset (1); }, hasPresets },
        { session.muteGuitar ? "ギターミュート解除"_jp : "ギターミュート"_jp,
                                         [this] { toggleGuitarMute(); } },
        { "メトロノームオン・オフ"_jp,   [this] { perform (Command::toggleMetronome); } },
        { session.muteSource ? "音源ミュート解除"_jp : "音源ミュート"_jp,
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
    s.noiseGateDb = session.noiseGateDb;
    s.pluginPath = session.pluginPath;
    s.pluginState = session.pluginState;

    // プラグインのつまみは、今の状態を保存する (セッションの保存を待たずに)
    if (session.tonePresetId == pluginTonePresetId)
        if (auto* tone = engine.getPluginTone())
        {
            s.pluginState = tone->getState().toBase64Encoding();
            if (pluginSlotForPreset (slot) == engine.getActivePluginSlot())
                pluginSlotOwner[(size_t) engine.getActivePluginSlot()] = slot;   // 今鳴らしているものが、このプリセット
        }

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
    session.noiseGateDb = s.noiseGateDb;

    // 音色を開いたときと同じ順で、エンジンに渡す (applySessionToEngine)
    selectTone (session.tonePresetId == namTonePresetId    ? namToneIndex
              : session.tonePresetId == pluginTonePresetId ? pluginToneIndex
                                                           : findTonePresetIndex (session.tonePresetId), false);
    engine.setInputGainDb ((float) session.inputGainDb);
    engine.setNoiseGateDb ((float) session.noiseGateDb);
    loadNamTone();

    if (session.tonePresetId == pluginTonePresetId && s.pluginPath.isNotEmpty())
    {
        session.pluginPath = s.pluginPath;
        session.pluginState = s.pluginState;
        activatePresetPlugin (slot);   // 読み込んであれば、鳴らすスロットを切り替えるだけ
    }

    updateToneSummary();
}

//==============================================================================
// VST の複数プリセット
int SessionScreen::pluginSlotForPreset (int preset) const
{
    const auto& s = session.toneSlots[(size_t) preset - 1];
    const juce::File file (s.pluginPath);

    // 同時に2つ作れないプラグイン: すでにあるスロット、または前のプリセットと同じスロットを使い回す
    if (PluginLocator::isSingleInstance (file))
    {
        for (int slot = 0; slot < AudioEngine::numPluginSlots; ++slot)
            if (auto* tone = engine.getPluginTone (slot); tone != nullptr && tone->getFile() == file)
                return slot;

        for (int q = 1; q < preset; ++q)
        {
            const auto& other = session.toneSlots[(size_t) q - 1];
            if (other.tonePresetId == pluginTonePresetId && juce::File (other.pluginPath) == file)
                return pluginSlotForPreset (q);
        }
    }
    return preset - 1;
}

int SessionScreen::directPluginSlot() const
{
    // 今のパートのプリセットがこの VST なら、そのスロット。そうでなければ、VST のプリセットが使っていないスロット
    const auto current = currentPartPresetValue();
    if (current > 0)
    {
        const auto& s = session.toneSlots[(size_t) current - 1];
        if (s.tonePresetId == pluginTonePresetId && s.pluginPath == session.pluginPath)
            return pluginSlotForPreset (current);
    }

    for (int slot = 0; slot < AudioEngine::numPluginSlots; ++slot)
        if (session.toneSlots[(size_t) slot].tonePresetId != pluginTonePresetId || session.toneSlots[(size_t) slot].pluginPath.isEmpty())
            return slot;
    return 0;
}

void SessionScreen::setUpPluginSlots()
{
    const bool usesPlugin = session.tonePresetId == pluginTonePresetId || session.pluginPath.isNotEmpty();

    // 鳴らすスロットを先に決める (今の VST の読み込み・編集画面は、このスロットのもの)
    if (usesPlugin)
    {
        const auto slot = directPluginSlot();
        engine.setActivePluginSlot (slot);
        const auto current = currentPartPresetValue();
        pluginSlotOwner[(size_t) slot] = current > 0 && pluginSlotForPreset (current) == slot ? current : 0;
        loadPluginTone();
    }

    // プリセットの VST を、それぞれのスロットに読み込む (保存した設定で)。今鳴らしているスロットは上で読み込んだ
    for (int preset = 1; preset <= Session::numToneSlots; ++preset)
    {
        const auto& s = session.toneSlots[(size_t) preset - 1];
        if (s.tonePresetId != pluginTonePresetId || s.pluginPath.isEmpty() || ! juce::File (s.pluginPath).exists())
            continue;

        const auto slot = pluginSlotForPreset (preset);
        if (slot != preset - 1)
            continue;   // 使い回し (同時に2つ作れないプラグイン): 設定は、そのプリセットを押したときに入れる
        if (usesPlugin && slot == engine.getActivePluginSlot())
            continue;

        juce::MemoryBlock state;
        if (s.pluginState.isNotEmpty())
            state.fromBase64Encoding (s.pluginState);
        pluginSlotOwner[(size_t) slot] = preset;
        engine.loadPluginToneAsync (slot, juce::File (s.pluginPath), state, nullptr);
    }
}

void SessionScreen::activatePresetPlugin (int preset)
{
    const auto slot = pluginSlotForPreset (preset);
    const juce::File file (session.toneSlots[(size_t) preset - 1].pluginPath);
    const bool windowOpen = pluginCard.isWindowShowing();
    auto* tone = engine.getPluginTone (slot);

    if (tone != nullptr && tone->getFile() == file && pluginSlotOwner[(size_t) slot] == preset)
    {
        // 読み込んであり、このプリセットの設定のまま: 鳴らすスロットを切り替えるだけ
        engine.setActivePluginSlot (slot);
        pluginBaselineState = tone->getState().toBase64Encoding();
        pluginCard.setState (PluginToneCard::State::ready);
    }
    else
    {
        // 使い回しのスロットで、ほかのプリセットの設定が入っている: その設定 (つまみを動かした分も) を
        // 元のプリセットに残してから、このプリセットの設定を入れる
        const auto owner = pluginSlotOwner[(size_t) slot];
        if (tone != nullptr && owner > 0 && owner != preset
            && juce::File (session.toneSlots[(size_t) owner - 1].pluginPath) == tone->getFile())
            session.toneSlots[(size_t) owner - 1].pluginState = tone->getState().toBase64Encoding();

        engine.setActivePluginSlot (slot);
        pluginSlotOwner[(size_t) slot] = preset;
        loadPluginTone();   // 同じプラグインなら設定だけ入れる。違えば作る (今のスロットに)
    }

    if (windowOpen)
        pluginCard.requestOpenWindow();   // 開いていたら、新しいプリセットのプラグインの画面を開く
}

void SessionScreen::chooseVstForPreset (int preset)
{
    const auto folders = PluginLocator::getVst3Folders();
    fileChooser = std::make_unique<juce::FileChooser> ("プリセット "_jp + juce::String (preset) + " の VST3 プラグインを選ぶ"_jp,
                                                       folders.getFirst(), "*.vst3");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer (this), preset] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();
        if (safe == nullptr || file == juce::File())
            return;

        // 今の音色の設定 (入力の大きさなど) で、選んだ VST のプリセットを作る。プラグインの設定は、保存のときに入る
        auto& s = safe->session.toneSlots[(size_t) preset - 1];
        s = {};
        s.tonePresetId = pluginTonePresetId;
        s.pluginPath = file.getFullPathName();
        s.inputGainDb = safe->session.inputGainDb;
        s.noiseGateDb = safe->session.noiseGateDb;

        safe->currentPartPreset() = preset;
        safe->applyPreset (preset);
        safe->updatePresetButtons();
        safe->setStatus ("プリセット "_jp + juce::String (preset) + " に "_jp + file.getFileNameWithoutExtension()
                         + " を入れました。プリセットを押すと、すぐに切り替わります"_jp);
    });
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
    const auto scoreFile = currentScoreFile();
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

void SessionScreen::toggleVideoCard()
{
    videoCardCollapsed = ! videoCardCollapsed;
    settings.setVideoCardCollapsed (videoCardCollapsed);
    videoCollapseButton.setButtonText (videoCardCollapsed ? juce::String::fromUTF8 ("▼") : juce::String::fromUTF8 ("▲"));
    videoCollapseButton.setTooltip (videoCardCollapsed ? "音源のカードを広げる"_jp : "音源のカードをたたむ"_jp);
    videoFold.setTarget (! videoCardCollapsed);   // 上へたたむ・下へ広げる (動いている間、毎回 resized を呼ぶ)
    resized();
}

void SessionScreen::showComingSoon (const juce::String& what)
{
    setStatus (what + "は準備中です"_jp);
}

void SessionScreen::applySourceGain()
{
    // ミュートしても、スライダーの値 (session.mixSource) は変えない。解除すると元の音量に戻る
    const auto gain = session.muteSource ? 0.0 : session.mixSource;
    engine.setSourceGain ((float) gain);

    if (isYouTube())   // YouTube の音はアプリのミキサーを通らないので、プレイヤーの音量で合わせる
        youtube.setVolume (gain);

    sourceGainSlider.setAlpha (session.muteSource ? 0.5f : 1.0f);
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

SetlistCarryOver SessionScreen::getSetlistCarryOver() const
{
    return { metronomeOn, sidePanelVisible };
}

void SessionScreen::applySetlistCarryOver (const SetlistCarryOver& carry)
{
    // メトロノームの「曲に合わせる」: 前の曲と同じにする (案内の文は出さない)
    if (metronomeOn != carry.metronomeOn)
    {
        metronomeOn = carry.metronomeOn;
        engine.setMetronomeEnabled (metronomeOn);
        metronomeButton.setToggleState (metronomeOn, juce::dontSendNotification);
        logMetronomeState ("セットリストの前の曲から引き継ぎ"_jp);
    }

    // 右のパネル: 前の曲で隠していたら、隠したまま開く
    if (sidePanelVisible != carry.sidePanelVisible)
        toggleSidePanel();
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
    grabKeyboardFocus();   // Esc で閉じられるように (タブ譜のページがキーを持っていることがある)
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
        // プラグインのウィンドウは開かない (作者の指示: 音色を切り替えるだけにする)。
        // ウィンドウは、カードの「ウィンドウを開く」ボタンで開く
        if (fromUser)
            pluginScrollPending = true;

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
        safe->pluginSlotOwner[(size_t) safe->engine.getActivePluginSlot()] = 0;   // プリセットの VST ではなくなる
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

    // VST のプリセット: 読み込んであるものは、今の設定 (つまみを動かした分も) をプリセットに残す
    for (int slot = 0; slot < AudioEngine::numPluginSlots; ++slot)
    {
        const auto owner = pluginSlotOwner[(size_t) slot];
        auto* tone = engine.getPluginTone (slot);
        if (owner <= 0 || tone == nullptr)
            continue;
        auto& s = session.toneSlots[(size_t) owner - 1];
        if (s.tonePresetId == pluginTonePresetId && juce::File (s.pluginPath) == tone->getFile())
            s.pluginState = tone->getState().toBase64Encoding();
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

// 「歪み」の音色: 同梱の NAM のモデルと IR を読み込む (作者の指示)。
// 「NAM」のボタンの音色とは別に持つので、セッションには保存しない。一度読み込めば、ほかの曲でもそのまま使う
void SessionScreen::loadDriveTone()
{
    const auto files = BuiltInTones::getDriveTone();
    if (! files.amp.existsAsFile())
    {
        AppLog::write ("歪みの音色のファイルを書き出せませんでした"_jp);
        return;
    }

    engine.loadDriveToneAsync (files.amp, files.cabinet,
                               [safe = juce::Component::SafePointer (this)] (bool succeeded, const juce::String& error)
    {
        if (safe != nullptr && ! succeeded)
            safe->setStatus ("歪みの音色を読み込めませんでした: "_jp + error);
    });
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
namespace
{
    // その場所のいちばん上の窓が、この画面の窓か (メニュー・ポップアップが重なっていない)
    bool isTopWindowAt (juce::Component& self, juce::Point<float> screen)
    {
        auto* under = juce::Desktop::getInstance().findComponentAt (screen.roundToInt());
        return under != nullptr && under->getTopLevelComponent() == self.getTopLevelComponent();
    }
}

void SessionScreen::updateCursorHiding()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto mouse = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition();
    if (mouse != lastMousePosition)
    {
        lastMousePosition = mouse;
        lastMouseMoveMs = now;
    }

    // 大きな枠 (タブ譜・入れ替えた動画・ローカル音源の時刻) の上にあり、3 秒動いていない。
    // メニュー・ポップアップを出しているとき、マウスのボタンを押しているときは隠さない
    const auto overMain = isShowing() && scoreView.isShowing()
                       && scoreView.getScreenBounds().toFloat().contains (mouse)
                       && juce::ModalComponentManager::getInstance()->getNumModalComponents() == 0
                       && isTopWindowAt (*this, mouse)
                       && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown();
    const bool hide = overMain && now - lastMouseMoveMs >= 3000.0;
    if (hide == mainCursorHidden)
        return;

    mainCursorHidden = hide;
    scoreView.setMouseCursor (hide ? juce::MouseCursor::NoCursor : juce::MouseCursor::NormalCursor);
    scoreViewer.setCursorHidden (hide);
    youtube.setCursorHidden (hide);
    juce::Desktop::getInstance().getMainMouseSource().forceMouseCursorUpdate();
}

void SessionScreen::timerCallback()
{
    updateCursorHiding();
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

    // 終了位置に来たら止める (ループ中は止めない)。止めたあと、checkSongEnd が曲の終わりとして扱う
    if (playing && session.sourceEndMs > 0 && ! session.loopEnabled && ! isScoreOnly() && position >= session.sourceEndMs / 1000.0)
        pauseTransport();

    checkSongEnd (playing, position, length);
    wasPlaying = playing;

    if (openContext.isSetlist())
    {
        const auto& info = setlistBar.getInfo();
        const auto rangeStart = getPlayStart(), rangeEnd = getPlayEnd (length);
        const auto fraction = rangeEnd > rangeStart ? (position - rangeStart) / (rangeEnd - rangeStart) : 0.0;
        // 最後の曲になったら 100% にする (作者の指示)
        if (info.count > 0)
            setlistBar.setProgress (info.position >= info.count ? 1.0
                                                                : (info.position - 1 + juce::jlimit (0.0, 1.0, fraction)) / info.count);
        setlistBar.setNotice (session.loopEnabled ? "ループ中のため自動で次に進みません"_jp : juce::String());
    }

    updateMetronomeStart();
    updateScoreStartButton();
    updateSourceRangeButtons();

    // 小節・拍 (一定テンポ + ずらし。設計書 3.1)。動画モードでメトロノームの開始位置があれば、そこが 1小節目
    const auto beats = (position - getMetronomeOffsetSeconds()) * session.bpm / 60.0;
    const auto beatsPerBar = usesScoreBars() ? 4 : juce::jmax (1, session.meterNumerator);   // 手で決めた拍子 (.gp が無いとき)
    const auto bar = beats >= 0.0 ? (int) std::floor (beats / beatsPerBar) + 1 : 0;
    const auto beat = beats >= 0.0 ? (int) std::floor (beats) % beatsPerBar + 1 : 0;
    positionLabel.setText ("小節 "_jp + juce::String (bar) + "   拍 "_jp + juce::String (beat), juce::dontSendNotification);
    scoreView.setBarAndBeat (bar, beat);
    scoreView.setTimeText (formatTime (position) + " / " + formatTime (length));   // ローカル音源を入れ替えたときに出す
    updateMetronomeDots (beats);

    // タブ譜は「拍の位置」+「タブ譜だけ」。針はページの中で、速度と経過時間からなめらかに進める
    const auto rate = isYouTube() ? youtube.getState().rate : practiceRate();
    scoreViewer.setPosition (position, session.bpm, getScoreOffsetSeconds(), rate, playing);

    // YouTube: カウントダウン中に、音を消して一瞬だけ再生し、読み込みを済ませておく (作者の指示)。
    // 動画は再生を始めるまで読み込まれないので、カウントダウンのあとに読み込みの待ちが出るのを防ぐ
    if (isYouTube() && sourceReady && countdown.isCountingDown() && ! videoWarmedUp)
    {
        videoWarmedUp = true;
        youtube.warmUp();
    }

    timeline.setState (position, length, session.loopStartMs / 1000.0, session.loopEndMs / 1000.0, session.loopEnabled);
    timeline.setRange (session.sourceStartMs > 0 ? session.sourceStartMs / 1000.0 : -1.0,
                       session.sourceEndMs > 0 ? session.sourceEndMs / 1000.0 : -1.0);

    // プラグインのつまみを動かしたか (1秒ごと。状態を取り出すのは軽くないため)
    if (--pluginStateCheckCountdown <= 0)
    {
        pluginStateCheckCountdown = 30;
        if (auto* tone = engine.getPluginTone(); tone != nullptr && pluginBaselineState.isNotEmpty())
            pluginStateChanged = tone->getState().toBase64Encoding() != pluginBaselineState;
    }

    const auto dirty = isDirty();
    saveButton.setMarked (dirty);   // 未保存の変更があると、右上に印
    saveButton.setTooltip (dirty ? "保存 (Ctrl+S)。保存していない変更があります"_jp : "保存 (Ctrl+S)"_jp);

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
    for (auto* label : { &statusLabel, &sourceInfoLabel, &artistLabel })
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

    for (const auto& line : bottomDividers)   // グループの区切り (縦) と、2段のときの段の間 (横)
        g.fillRect (line);
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
    // たたむ・広げる動きの間も出さない (ページを毎回動かすと、ガタついて見えるため)。着いたら出す
    const bool cardOpen = ! videoCardCollapsed && ! videoFold.isAnimating();
    const bool videoTargetVisible = swapped || (sidePanelVisible && cardOpen);
    const bool scoreTargetVisible = ! swapped || (sidePanelVisible && cardOpen);

    if (isYouTube() && canShow && videoTargetVisible)
        youtube.placeOver (videoTarget);
    else
        youtube.hide();

    if (session.scorePath.isNotEmpty() && currentScoreFile().existsAsFile() && canShow && scoreTargetVisible)
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
    saveButton.setBounds (top.removeFromRight (44));    // 正方形のアイコン (設定と同じ。作者の指示)
    top.removeFromRight (8);
    tunerButton.setBounds (top.removeFromRight (44));
    top.removeFromRight (12);
    helpButton.setBounds (top.removeFromRight (40).withSizeKeepingCentre (40, 40));
    top.removeFromRight (12);

    // 曲名は文字の幅だけ取り、その右にアーティスト名。残りに状態 (「保存しました」など) を右寄せで出す
    {
        const auto textWidth = [] (juce::Label& label)
        {
            // 描くときと同じ Font で測る (文字の大きさの倍率・今のテーマの書体が入る)
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (label.getLookAndFeel().getLabelFont (label), label.getText(), 0.0f, 0.0f);
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
    // 作者のレイアウト: 左から「速度 | ループ | メトロノーム | 再生」。
    // 1段に入らない狭いウィンドウでは2段にする (作者の指示): 上「速度 | ループ」、下「メトロノーム | 再生」。
    // 2段のときは、ボタンを低くし、どのボタンも同じ幅にする
    constexpr int gap = 6, groupGap = 28;
    constexpr int playWidth = 84 * 5 + gap * 4;   // 頭へ・戻る・再生・進む・最後へ
    constexpr int dotsWidth = 116;   // 拍子 ("4/4") とドット。拍子の数が多いときは、ドットを小さくして収める
    constexpr int metronomeWidth = 120 + 84 + dotsWidth + gap * 2;
    constexpr int loopWidth = 96 + 96 + 96 + gap * 2;
    constexpr int minSpeedWidth = 160;
    bottomTwoRows = area.getWidth() - 28 < minSpeedWidth + loopWidth + metronomeWidth + playWidth + groupGap * 3;
    constexpr int rowGap = 10;
    bottomBarHeight = bottomTwoRows ? 2 * (20 + 2 + 36) + rowGap + 16 : 84;

    bottomBarArea = area.removeFromBottom (bottomBarHeight);
    auto bottom = bottomBarArea.reduced (14, 8);
    area.removeFromBottom (12);
    bottomDividers.clear();

    const auto place = [] (juce::Rectangle<int>& row, juce::Component& c, int width)
    {
        c.setBounds (row.removeFromLeft (width));
        row.removeFromLeft (gap);
    };

    // 見出し (20px) と、その下に部品の行 (残り)。divider: 後ろにグループの間 (区切り線) を取る
    const auto group = [this] (juce::Rectangle<int>& row, SectionHeading& heading, int width, int sectionGap, bool divider)
    {
        auto g = row.removeFromLeft (width);
        heading.setBounds (g.removeFromTop (20));
        g.removeFromTop (2);

        if (divider)
        {
            bottomDividers.push_back ({ row.getX() + sectionGap / 2, row.getY() + 4, 1, row.getHeight() - 8 });
            row.removeFromLeft (sectionGap);
        }
        return g;
    };

    if (! bottomTwoRows)
    {
        const auto speedWidth = juce::jlimit (minSpeedWidth, 360, bottom.getWidth() - playWidth - metronomeWidth - loopWidth - groupGap * 3);

        // 大きな画面では、速度の幅が上限になっても余白が残る。残りを、帯の左右の端とグループの間 (3か所) に分ける。
        // 作者の指示で、端を多め (1か所あたり、間の2倍) にする: 端2か所 × 2 + 間3か所 × 1 = 7 つ分に割る
        const auto spare = juce::jmax (0, bottom.getWidth() - (speedWidth + loopWidth + metronomeWidth + playWidth + groupGap * 3));
        const auto spareUnit = spare / 7;
        const auto sectionGap = groupGap + spareUnit;
        bottom.reduce ((spare - spareUnit * 3) / 2, 0);   // 端の分 (割り切れない端数も、ここで左右に分ける)

        auto speed = group (bottom, speedHeading, speedWidth, sectionGap, true);
        speedSlider.setBounds (speed);

        auto loop = group (bottom, loopHeading, loopWidth, sectionGap, true);
        place (loop, loopStartButton, 96);
        place (loop, loopEndButton, 96);
        place (loop, loopButton, 96);

        auto metronome = group (bottom, metronomeHeading, metronomeWidth, sectionGap, true);
        place (metronome, metronomeButton, 120);
        place (metronome, metronomeSoloButton, 84);
        place (metronome, metronomeDots, dotsWidth);

        auto play = group (bottom, playHeading, playWidth, sectionGap, false);
        for (auto* b : std::initializer_list<juce::Component*> { &toStartButton, &rewindButton, &playButton, &forwardButton, &toEndButton })
            place (play, *b, 84);
    }
    else
    {
        // ボタンの幅 (どれも同じ): 下の段 (メトロノーム3つ + 再生5つ) が入る幅
        auto row1 = bottom.removeFromTop ((bottom.getHeight() - rowGap) / 2);
        bottom.removeFromTop (rowGap);
        auto row2 = bottom;
        const auto unit = juce::jlimit (60, 110, (row2.getWidth() - groupGap - gap * 6) / 8);
        const auto metronomeW = unit * 3 + gap * 2, playW = unit * 5 + gap * 4;
        const auto total = metronomeW + groupGap + playW;
        const auto inset = juce::jmax (0, (row2.getWidth() - total) / 2);   // 2つの段を、同じ幅で真ん中に
        row1 = row1.withTrimmedLeft (inset).withWidth (total);
        row2 = row2.withTrimmedLeft (inset).withWidth (total);

        // 段の間の線
        bottomDividers.push_back ({ row1.getX(), row1.getBottom() + rowGap / 2, total, 1 });

        const auto loopW = unit * 3 + gap * 2;
        auto speed = group (row1, speedHeading, total - loopW - groupGap, groupGap, true);
        speedSlider.setBounds (speed);
        auto loop = group (row1, loopHeading, loopW, groupGap, false);
        for (auto* b : std::initializer_list<juce::Component*> { &loopStartButton, &loopEndButton, &loopButton })
            place (loop, *b, unit);

        auto metronome = group (row2, metronomeHeading, metronomeW, groupGap, true);
        place (metronome, metronomeButton, unit);
        place (metronome, metronomeSoloButton, unit);
        place (metronome, metronomeDots, unit);

        auto play = group (row2, playHeading, playW, groupGap, false);
        for (auto* b : std::initializer_list<juce::Component*> { &toStartButton, &rewindButton, &playButton, &forwardButton, &toEndButton })
            place (play, *b, unit);
    }

    // 「ここから開始」は、見出しの行の「単体」の真上に小さく置く (下の帯の高さを増やさないため)
    {
        auto headingRow = metronomeHeading.getBounds();
        const auto solo = metronomeSoloButton.getBounds();
        metronomeStartButton.setBounds (solo.getX(), headingRow.getY(), solo.getWidth(), headingRow.getHeight() - 1);
        metronomeHeading.setBounds (headingRow.withRight (solo.getX() - 4));
    }

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
             &sourceHeading, &videoCollapseButton, &splitter, &videoView, &sourceInfoLabel, &sourceRemoveButton,
             &chooseFileButton, &pasteUrlButton, &sideScroll })
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
        constexpr int tempoHeight = headingHeight + 3 * rowHeight + 2 * 4;   // BPM・タブ譜だけ・拍の位置
        const int sourceExtra = headingHeight + 30 + (hasSource() ? 34 : 40);   // 音源があれば、開始・終了位置のボタンの行
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
        // たたむ・広げるときは、開き具合 (videoFold) の分だけの高さにして、カードを上へたたむ (作者の指示)。
        // 中身はカードの外にはみ出さないよう、カードの高さで切る (動いている間は動画・タブ譜のページを出さない: placeWebViews)
        const auto openHeight = videoHeight + sourceExtra;
        const auto fold = videoFold.getValue();
        const auto shownHeight = 20 + juce::roundToInt ((openHeight - 20) * fold);
        auto source = card (shownHeight);
        const auto sourceClip = source.expanded (12, 8);
        const auto sourceOpen = fold > 0.0;
        auto sourceHeadingRow = source.removeFromTop (20);
        videoCollapseButton.setBounds (sourceHeadingRow.removeFromRight (32).withSizeKeepingCentre (32, 22));
        sourceHeading.setBounds (sourceHeadingRow);
        source.removeFromTop (4);
        source.setHeight (openHeight - 24);   // 中身は開いたときの並びのまま (はみ出す分は下で隠す)
        videoView.setBounds (source.removeFromTop (videoHeight));
        videoView.setVisible (sourceOpen);

        chooseFileButton.setVisible (! hasSource() && sourceOpen);
        pasteUrlButton.setVisible (! hasSource() && sourceOpen);
        sourceInfoLabel.setVisible (hasSource() && sourceOpen);
        sourceRemoveButton.setVisible (hasSource() && sourceOpen);
        sourceStartButton.setVisible (hasSource() && sourceOpen);
        sourceEndButton.setVisible (hasSource() && sourceOpen);

        if (hasSource())
        {
            auto row = source.removeFromTop (30).withTrimmedTop (4);
            sourceRemoveButton.setBounds (row.removeFromRight (26));
            sourceInfoLabel.setBounds (row);

            // リンク・ファイル名の下に「開始位置」「終了位置」(作者の指示)
            auto rangeRow = source.removeFromTop (34).withTrimmedTop (4);
            sourceStartButton.setBounds (rangeRow.removeFromLeft ((rangeRow.getWidth() - 8) / 2));
            rangeRow.removeFromLeft (8);
            sourceEndButton.setBounds (rangeRow);
        }

        // たたんでいる途中: カードの外に出た部品を隠す
        for (auto* c : std::initializer_list<juce::Component*> { &videoView, &sourceInfoLabel, &sourceRemoveButton, &sourceStartButton,
                                                                 &sourceEndButton, &chooseFileButton, &pasteUrlButton })
            if (c->isVisible() && ! sourceClip.contains (c->getBounds()))
                c->setVisible (false);
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
    {
        // PDF のときは「タブ譜が流れる速度」の欄が入るので、ボタンを少し細くする (文字は収まる幅)
        const bool pdf = session.isPdfScore();
        const bool hasScore = session.scorePath.isNotEmpty();   // 無ければ、タブ譜のためのボタンは隠している (updatePdfUi)
        const int partWidth = pdf ? 76 : 90;
        if (hasScore)
        {
            auto partsArea = scoreHeader.removeFromLeft (partWidth + 6 + partWidth);
            leadButton.setBounds (partsArea.removeFromLeft (partWidth).reduced (0, 2));
            partsArea.removeFromLeft (6);
            backingButton.setBounds (partsArea.removeFromLeft (partWidth).reduced (0, 2));
            scoreHeader.removeFromLeft (pdf ? 8 : 12);
        }

        // 右から並べる。隠しているボタンは詰める
        const auto placeRight = [&scoreHeader] (juce::Component& c, int width)
        {
            if (! c.isVisible())
                return;
            c.setBounds (scoreHeader.removeFromRight (width).reduced (0, 2));
            scoreHeader.removeFromRight (6);
        };
        placeRight (scoreRemoveButton, pdf ? 96 : 110);
        placeRight (scoreChooseButton, pdf ? 96 : 110);
        placeRight (swapButton, pdf ? 108 : 120);
        placeRight (equalBarsButton, 120);
        placeRight (scoreStartButton, pdf ? 100 : 120);   // 「ここから流す」(作者のレイアウト)
        scoreHeader.removeFromRight (2);

        // PDF のときは、リード・バッキングの右に「タブ譜が流れる速度」とスライダー (ミキサーと同じ形)。
        // 名前は文字の幅、スライダーは残りの幅 (広いときは 220 まで)
        if (pdf)
        {
            const auto labelWidth = juce::GlyphArrangement::getStringWidthInt (pdfSpeedLabel.getFont(), pdfSpeedLabel.getText()) + 10;
            pdfSpeedLabel.setBounds (scoreHeader.removeFromLeft (juce::jmin (labelWidth, scoreHeader.getWidth())));
            pdfSpeedSlider.setBounds (scoreHeader.removeFromLeft (juce::jmin (220, scoreHeader.getWidth())).reduced (0, 3));
        }
    }
    positionLabel.setVisible (false);   // 「小節 ○ 拍 ○」は、作者のレイアウトに無いので出さない (タブ譜のカーソルと再生バーで分かる)
    // タブ譜のファイル名は出さない (作者の指示)。残りの幅は空けておく
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
