#include "DebugLogPanel.h"
#include "../AppLog.h"
#include "../Text.h"

namespace
{
    constexpr int maxLogLines = 400;   // 長いときは最後の部分だけ出す

    // Windows がアプリの落ちたときに残すファイル (WER のクラッシュダンプ)
    juce::File getDumpFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("AppData").getChildFile ("Local").getChildFile ("CrashDumps");
    }

    juce::Array<juce::File> findDumps()
    {
        auto dumps = getDumpFolder().findChildFiles (juce::File::findFiles, false, "Guitar Practice.exe.*.dmp");

        // 新しい順
        std::sort (dumps.begin(), dumps.end(), [] (const juce::File& a, const juce::File& b)
        {
            return a.getLastModificationTime() > b.getLastModificationTime();
        });
        return dumps;
    }
}

DebugLogPanel::DebugLogPanel()
{
    heading.setText ("動作ログとクラッシュの記録"_jp, juce::dontSendNotification);
    heading.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    summary.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (heading);
    addAndMakeVisible (summary);

    text.setMultiLine (true, false);
    text.setReadOnly (true);
    text.setScrollbarsShown (true);
    text.setCaretVisible (false);
    // 等幅で、日本語も入っているフォント (ない環境では標準の等幅フォント)
    const auto mono = juce::Font::findAllTypefaceNames().contains ("MS Gothic") ? juce::String ("MS Gothic")
                                                                                  : juce::Font::getDefaultMonospacedFontName();
    text.setFont (juce::FontOptions (mono, 14.0f, juce::Font::plain));
    addAndMakeVisible (text);

    reloadButton.setButtonText ("読み直す"_jp);
    copyButton.setButtonText ("すべてコピー"_jp);
    copyButton.setTooltip ("中身をクリップボードにコピーします。不具合を伝えるときに貼り付けてください"_jp);
    logFolderButton.setButtonText ("ログのフォルダを開く"_jp);
    dumpFolderButton.setButtonText ("ダンプのフォルダを開く"_jp);

    reloadButton.onClick = [this] { reload(); };
    copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (summary.getText() + "\n\n" + text.getText()); };
    logFolderButton.onClick = [] { AppLog::getFile().getParentDirectory().startAsProcess(); };
    dumpFolderButton.onClick = [] { if (getDumpFolder().isDirectory()) getDumpFolder().startAsProcess(); };

    for (auto* b : { &reloadButton, &copyButton, &logFolderButton, &dumpFolderButton })
        addAndMakeVisible (b);

    setSize (preferredWidth, preferredHeight);
    reload();
}

void DebugLogPanel::reload()
{
    // 前回の終わり方: 今の起動の直前の記録を探す (今の起動の「起動」の行の1つ前)
    juce::StringArray lines;
    AppLog::getFile().readLines (lines);
    lines.removeEmptyStrings();

    // 起動するとき、前回が正しく終わっていなければ、「起動」の直前に「!! 前回は正しく終了していません」を書いている
    juce::String previousEnd = "記録なし"_jp;
    for (int i = lines.size() - 1; i >= 0; --i)
    {
        if (! AppLog::isEntryLine (lines[i]) || ! lines[i].substring (20).startsWith ("起動"_jp))
            continue;

        if (i > 0 && lines[i - 1].contains ("前回は正しく終了していません"_jp))
            previousEnd = "正しく終了していません (落ちたか、強制終了された)"_jp;
        else if (i > 0)
            previousEnd = "正しく終了しました"_jp;
        break;
    }

    const auto dumps = findDumps();
    juce::String info;
    info << "前回の終わり方: "_jp << previousEnd << "\n"
         << "クラッシュダンプ: "_jp << dumps.size() << " 件"_jp;
    if (! dumps.isEmpty())
        info << " (最新: "_jp << dumps.getFirst().getLastModificationTime().formatted ("%Y-%m-%d %H:%M") << ")";
    info << "\nログ: "_jp << AppLog::getFile().getFullPathName();
    summary.setText (info, juce::dontSendNotification);

    juce::String body;
    if (! dumps.isEmpty())
    {
        body << "==== クラッシュダンプ (新しい順。開発者に調べてもらうときに使う) ====\n"_jp;
        for (int i = 0; i < juce::jmin (10, dumps.size()); ++i)
            body << dumps[i].getLastModificationTime().formatted ("%Y-%m-%d %H:%M:%S  ")
                 << dumps[i].getFileName() << "  (" << juce::File::descriptionOfSizeInBytes (dumps[i].getSize()) << ")\n";
        body << "\n";
    }

    body << "==== 動作ログ (最後の "_jp << juce::jmin (maxLogLines, lines.size()) << " 行) ====\n"_jp;
    for (int i = juce::jmax (0, lines.size() - maxLogLines); i < lines.size(); ++i)
        body << lines[i] << "\n";

    text.setText (body, false);
    text.moveCaretToEnd();   // 新しい記録 (最後) を見せる
}

void DebugLogPanel::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void DebugLogPanel::resized()
{
    auto area = getLocalBounds().reduced (16);
    heading.setBounds (area.removeFromTop (32));
    summary.setBounds (area.removeFromTop (60));
    area.removeFromTop (6);

    auto buttons = area.removeFromBottom (32);
    reloadButton.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (8);
    copyButton.setBounds (buttons.removeFromLeft (130));
    dumpFolderButton.setBounds (buttons.removeFromRight (190));
    buttons.removeFromRight (8);
    logFolderButton.setBounds (buttons.removeFromRight (190));

    area.removeFromBottom (10);
    text.setBounds (area);
}
