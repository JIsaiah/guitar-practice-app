#include "SessionSetupScreen.h"
#include "../Model/YouTubeUrl.h"
#include "../Text.h"

namespace
{
    bool looksLikeYouTubeUrl (const juce::String& text)
    {
        // 動画 ID を取り出せる URL だけを受け付ける (再生時に ID が必要なため)
        return YouTubeUrl::extractVideoId (text).isNotEmpty();
    }
}

SessionSetupScreen::SessionSetupScreen (ScoreViewer& reader)
    : scoreReader (reader)
{
    heading.setText ("新しいセッション"_jp, juce::dontSendNotification);
    heading.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    addAndMakeVisible (heading);

    // 左の項目には番号を付けない (作者の指示)
    titleLabel.setText ("曲名"_jp, juce::dontSendNotification);
    sourceLabel.setText ("音源"_jp, juce::dontSendNotification);
    scoreLabel.setText ("タブ譜"_jp, juce::dontSendNotification);
    bpmLabel.setText ("BPM", juce::dontSendNotification);
    artistLabel.setText ("アーティスト"_jp, juce::dontSendNotification);
    tuningLabel.setText ("チューニング"_jp, juce::dontSendNotification);
    for (auto* label : { &titleLabel, &artistLabel, &sourceLabel, &scoreLabel, &bpmLabel, &tuningLabel,
                         &sourceValue, &scoreValue, &bpmNote })
        addAndMakeVisible (label);

    titleEditor.setTextToShowWhenEmpty ("必須。音源やタブ譜を選ぶと自動で入ります"_jp, juce::Colours::grey);
    titleEditor.onTextChange = [this] { session.title = titleEditor.getText().trim(); updateState(); };
    artistEditor.setTextToShowWhenEmpty ("タブ譜に書かれていれば自動で入ります"_jp, juce::Colours::grey);
    artistEditor.onTextChange = [this] { session.artist = artistEditor.getText().trim(); };
    tuningEditor.setTextToShowWhenEmpty ("例: E Standard、Drop D。タブ譜から自動で入ります"_jp, juce::Colours::grey);
    tuningEditor.onTextChange = [this] { session.tuning = tuningEditor.getText().trim(); };
    for (auto* editor : { &titleEditor, &artistEditor, &tuningEditor })
        addAndMakeVisible (editor);

    sourceFileButton.setButtonText ("ファイルを選ぶ…"_jp);
    youtubeButton.setButtonText ("YouTube の URL を貼り付け"_jp);
    scoreFileButton.setButtonText ("ファイルを選ぶ…"_jp);
    scoreClearButton.setButtonText ("なし"_jp);
    sourceFileButton.onClick = [this] { chooseSourceFile(); };
    youtubeButton.onClick = [this] { pasteYouTubeUrl(); };
    scoreFileButton.onClick = [this] { chooseScoreFile(); };
    scoreClearButton.onClick = [this] { session.scorePath = {}; bpmNote.setText ({}, juce::dontSendNotification); updateState(); };
    for (auto* button : { &sourceFileButton, &youtubeButton, &scoreFileButton, &scoreClearButton })
        addAndMakeVisible (button);

    bpmSlider.setRange (40.0, 300.0, 1.0);
    bpmSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 28);
    bpmSlider.onValueChange = [this]
    {
        session.bpm = bpmSlider.getValue();
        session.tempoOrigin = TempoOrigin::manual;
        bpmNote.setText ({}, juce::dontSendNotification);
    };
    bpmNote.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (bpmSlider);

    tapButton.setButtonText ("TAP");
    tapButton.onClick = [this] { tap(); };
    addAndMakeVisible (tapButton);

    cancelButton.setButtonText ("キャンセル"_jp);
    startButton.setButtonText ("練習を始める"_jp);
    cancelButton.onClick = [this] { if (onCancel != nullptr) onCancel(); };
    startButton.onClick = [this] { if (onStart != nullptr) onStart (session); };
    addAndMakeVisible (cancelButton);
    addAndMakeVisible (startButton);

    bpmSlider.setValue (session.bpm, juce::dontSendNotification);
    updateState();
}

void SessionSetupScreen::chooseSourceFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("音源ファイルを選ぶ"_jp, juce::File(),
                                                       "*.mp3;*.wav;*.flac;*.ogg;*.aif;*.aiff");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();
                                  if (file == juce::File())
                                      return;

                                  session.sourceType = SourceType::local;
                                  session.sourcePath = file.getFullPathName();
                                  session.youtubeUrl = {};

                                  if (titleEditor.isEmpty())
                                      titleEditor.setText (file.getFileNameWithoutExtension());

                                  updateState();
                              });
}

void SessionSetupScreen::pasteYouTubeUrl()
{
    // クリップボードは、このボタンを押したときだけ読む (設計書 4.1 の 2)
    const auto text = juce::SystemClipboard::getTextFromClipboard().trim();

    if (! looksLikeYouTubeUrl (text))
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon,
                                                "YouTube の URL ではありません"_jp,
                                                "YouTube の動画ページの URL をコピーしてから、もう一度押してください。"_jp);
        return;
    }

    session.sourceType = SourceType::youtube;
    session.youtubeUrl = text;
    session.sourcePath = {};
    updateState();
}

void SessionSetupScreen::chooseScoreFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("タブ譜ファイルを選ぶ"_jp, juce::File(),
                                                       "*.gp;*.gpx;*.gp5;*.gp4;*.gp3;*.musicxml;*.xml");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();
                                  if (file == juce::File())
                                      return;

                                  session.scorePath = file.getFullPathName();
                                  updateState();
                                  readScoreDetails();
                              });
}

void SessionSetupScreen::readScoreDetails()
{
    bpmNote.setText ("タブ譜を読んでいます…"_jp, juce::dontSendNotification);
    const auto path = session.scorePath;

    // 練習画面に移ると、練習画面が自分の処理に置き換える。この画面が先に消えても落ちないよう SafePointer で見る
    // (この画面のデストラクタでは消さない。新しい画面が作られたあとに古い画面が消えるため、練習画面の処理を消してしまう)
    scoreReader.onLoaded = [safe = juce::Component::SafePointer (this), path] (double tempo)
    {
        if (safe == nullptr || safe->session.scorePath != path)
            return;

        auto& s = *safe;
        juce::StringArray filled;

        if (tempo > 0.0)
        {
            s.bpmSlider.setValue (std::round (tempo), juce::dontSendNotification);
            s.session.bpm = s.bpmSlider.getValue();
            s.session.tempoOrigin = TempoOrigin::score;
            filled.add ("BPM " + juce::String (juce::roundToInt (tempo)));
        }

        // 入っていない欄だけ埋める (自分で書いたものは消さない)
        const auto fill = [&filled] (juce::TextEditor& editor, const juce::String& value, const juce::String& name)
        {
            if (editor.isEmpty() && value.isNotEmpty())
            {
                editor.setText (value, juce::sendNotification);
                filled.add (name);
            }
        };
        fill (s.titleEditor, s.scoreReader.getScoreTitle(), "曲名"_jp);
        fill (s.artistEditor, s.scoreReader.getScoreArtist(), "アーティスト"_jp);
        fill (s.tuningEditor, s.scoreReader.getScoreTuning(), "チューニング"_jp);

        juce::String note;
        if (! filled.isEmpty())
            note = "タブ譜から入れました: "_jp + filled.joinIntoString ("、"_jp);
        if (tempo <= 0.0)
            note << (note.isEmpty() ? "" : "。") << "タブ譜にテンポが書かれていません。TAP か手入力で決めてください"_jp;
        s.bpmNote.setText (note, juce::dontSendNotification);
    };
    scoreReader.onError = [safe = juce::Component::SafePointer (this)] (const juce::String&)
    {
        if (safe != nullptr)
            safe->bpmNote.setText ("タブ譜を読めませんでした。BPM は TAP か手入力で決めてください"_jp, juce::dontSendNotification);
    };

    scoreReader.load (juce::File (path), 0);
}

void SessionSetupScreen::tap()
{
    if (const auto bpm = tapTempo.tap (juce::Time::getMillisecondCounterHiRes()))
    {
        bpmSlider.setValue (std::round (*bpm), juce::dontSendNotification);
        session.bpm = bpmSlider.getValue();
        session.tempoOrigin = TempoOrigin::tap;
        bpmNote.setText ({}, juce::dontSendNotification);
    }
}

void SessionSetupScreen::updateState()
{
    switch (session.sourceType)
    {
        case SourceType::local:   sourceValue.setText (session.sourcePath, juce::dontSendNotification); break;
        case SourceType::youtube: sourceValue.setText ("YouTube: "_jp + session.youtubeUrl, juce::dontSendNotification); break;
        case SourceType::none:    sourceValue.setText ("(なし)"_jp, juce::dontSendNotification); break;
    }

    scoreValue.setText (session.scorePath.isEmpty() ? "(なし)"_jp : session.scorePath, juce::dontSendNotification);

    // 必須は曲名だけ (作者の指示)。音源もタブ譜も無いまま作ると、練習画面のタブ譜の枠で選んでもらう
    startButton.setEnabled (session.title.isNotEmpty());
    startButton.setTooltip (session.title.isEmpty() ? "曲名を入れると始められます"_jp : juce::String());

    // 始められるようになったら、アクセント色で塗る (作者の指示: 始められることを伝える)。
    // オンの状態の見た目 (アクセント色・白い文字) を使う。押して切り替わるボタンではないので、状態だけを変える
    startButton.setToggleState (session.title.isNotEmpty(), juce::dontSendNotification);
}

void SessionSetupScreen::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void SessionSetupScreen::resized()
{
    auto area = getLocalBounds().reduced (24);
    heading.setBounds (area.removeFromTop (44));
    area.removeFromTop (16);

    constexpr int labelWidth = 160;
    constexpr int rowHeight = 36;
    constexpr int gap = 14;

    const auto nextRow = [&] (juce::Label& label)
    {
        auto row = area.removeFromTop (rowHeight);
        label.setBounds (row.removeFromLeft (labelWidth));
        area.removeFromTop (gap);
        return row;
    };

    titleEditor.setBounds (nextRow (titleLabel).removeFromLeft (480));
    artistEditor.setBounds (nextRow (artistLabel).removeFromLeft (480));

    auto sourceRow = nextRow (sourceLabel);
    sourceFileButton.setBounds (sourceRow.removeFromLeft (160));
    sourceRow.removeFromLeft (8);
    youtubeButton.setBounds (sourceRow.removeFromLeft (240));
    sourceRow.removeFromLeft (12);
    sourceValue.setBounds (sourceRow);

    auto scoreRow = nextRow (scoreLabel);
    scoreFileButton.setBounds (scoreRow.removeFromLeft (160));
    scoreRow.removeFromLeft (8);
    scoreClearButton.setBounds (scoreRow.removeFromLeft (80));
    scoreRow.removeFromLeft (12);
    scoreValue.setBounds (scoreRow);

    auto bpmRow = nextRow (bpmLabel);
    bpmSlider.setBounds (bpmRow.removeFromLeft (360));
    bpmRow.removeFromLeft (8);
    tapButton.setBounds (bpmRow.removeFromLeft (100));
    bpmRow.removeFromLeft (12);
    bpmNote.setBounds (bpmRow);

    tuningEditor.setBounds (nextRow (tuningLabel).removeFromLeft (480));

    auto buttons = getLocalBounds().reduced (24).removeFromBottom (48);
    startButton.setBounds (buttons.removeFromRight (200));
    buttons.removeFromRight (12);
    cancelButton.setBounds (buttons.removeFromRight (140));
}
