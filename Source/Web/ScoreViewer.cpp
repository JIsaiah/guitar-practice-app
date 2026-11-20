#include "ScoreViewer.h"
#include "WebResources.h"
#include "ScoreFileData.h"
#include "LockedWebBrowser.h"

namespace
{
    juce::var makeCommand (const char* type, std::initializer_list<std::pair<const char*, juce::var>> values = {})
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("type", type);
        for (const auto& [key, value] : values)
            object->setProperty (key, value);
        return juce::var (object);
    }

    juce::String css (juce::Colour c) { return "#" + c.toDisplayString (false); }
}

ScoreViewer::ScoreViewer()
{
    setInterceptsMouseClicks (false, true);
}

ScoreViewer::~ScoreViewer() = default;

void ScoreViewer::prepare()
{
    if (browser != nullptr)
        return;

    const auto userData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                              .getChildFile ("GuitarPractice").getChildFile ("WebView2");

    auto options = juce::WebBrowserComponent::Options()
                       .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                       .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2()
                                                    .withUserDataFolder (userData)
                                                    .withBackgroundColour (juce::Colour (0xff1b1c1f)))
                       .withNativeIntegrationEnabled()
                       .withKeepPageLoadedWhenBrowserIsHidden()
                       .withResourceProvider ([this] (const juce::String& path) { return provide (path); })
                       .withEventListener ("score", [this] (const juce::var& event) { handleEvent (event); });

    browser = std::make_unique<LockedWebBrowser> (options);   // 同梱のページからほかへ移れないように (LockedWebBrowser.h)
    addAndMakeVisible (*browser);
    browser->setBounds (getLocalBounds());
    browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot() + "score-view.html");
}

std::optional<juce::WebBrowserComponent::Resource> ScoreViewer::provide (const juce::String& path)
{
    // タブ譜のファイルそのもの。/score?n=番号 (番号は、同じファイルでも読み直させるため)
    if (path.startsWith ("/score?") || path == "/score")
    {
        // 音声入りの .gp は、音声を抜いて渡す (ScoreFileData.h)。トラックの切り替えなどで同じファイルを
        // 読み直すことがあるので、ファイルと更新日時が同じなら、前に作ったものを使う
        const juce::ScopedLock lock (fileLock);
        const auto modified = scoreFile.getLastModificationTime();
        if (cachedFile != scoreFile || cachedModified != modified || cachedData.isEmpty())
        {
            cachedData.reset();
            if (! ScoreFileData::load (scoreFile, cachedData))
                return std::nullopt;
            cachedFile = scoreFile;
            cachedModified = modified;
            previewDataVersion = -1;
        }

        // 拍・小節を挿入したあと: ファイルの楽譜に、今までの編集を当てたものを渡す (ファイルは書き換えない)
        if (! previewEdits.empty())
        {
            if (previewDataVersion != previewVersion)
            {
                if (const auto result = GpScoreEdit::applyToGpData (cachedData, previewEdits, previewData); result.failed())
                {
                    previewData.reset();
                    if (onError != nullptr)
                        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer (this), message = result.getErrorMessage()]
                                                         { if (safe != nullptr && safe->onError != nullptr) safe->onError (message); });
                }
                previewDataVersion = previewVersion;
            }
            if (! previewData.isEmpty())
                return WebResources::fromMemory (previewData.getData(), previewData.getSize(), "application/octet-stream");
        }

        return WebResources::fromMemory (cachedData.getData(), cachedData.getSize(), "application/octet-stream");
    }

    return WebResources::findBundled (path);
}

void ScoreViewer::resized()
{
    if (browser != nullptr)
        browser->setBounds (getLocalBounds());
}

void ScoreViewer::placeOver (juce::Component& target)
{
    prepare();

    if (auto* parent = getParentComponent())
        setBounds (parent->getLocalArea (&target, target.getLocalBounds()));

    setVisible (true);
    toFront (false);
}

void ScoreViewer::hide()
{
    setVisible (false);
}

//==============================================================================
void ScoreViewer::send (const juce::var& command)
{
    if (browser == nullptr || ! pageReady)
    {
        pending.push_back (command);
        return;
    }

    browser->evaluateJavascript ("window.scoreCommand && window.scoreCommand(" + juce::JSON::toString (command, true) + ")");
}

void ScoreViewer::load (const juce::File& file, int trackIndex)
{
    {
        const juce::ScopedLock lock (fileLock);
        scoreFile = file;
        previewEdits.clear();   // 別の読み込み: 挿入を見せるための編集は使わない
        ++previewVersion;
    }

    previewLoading = false;
    loadInternal (file, trackIndex, {});
}

void ScoreViewer::reloadWithEdits (const std::vector<GpScoreEdit::NoteEdit>& edits, int trackIndex,
                                   const GpScoreEdit::NoteEdit& keepSelection)
{
    {
        const juce::ScopedLock lock (fileLock);
        previewEdits = edits;
        ++previewVersion;
    }

    // 読み直したあと、スクロールの位置を保ち、この場所を選んで編集モードを続ける
    auto* keep = new juce::DynamicObject();
    keep->setProperty ("slot", keepSelection.slot);
    keep->setProperty ("bar", keepSelection.masterBar);
    keep->setProperty ("voice", keepSelection.voice);
    keep->setProperty ("beat", keepSelection.beat);
    keep->setProperty ("string", keepSelection.string);

    previewLoading = true;
    loadInternal (scoreFile, trackIndex, juce::var (keep));
}

void ScoreViewer::clearPreviewEdits()
{
    const juce::ScopedLock lock (fileLock);
    previewEdits.clear();
    ++previewVersion;
}

void ScoreViewer::loadInternal (const juce::File& file, int trackIndex, const juce::var& keep)
{
    rendered = false;
    scoreTempo = 0.0;
    barStartBeats.clear();
    barSignatures.clear();
    barNumbers.clear();
    scoreEndBeats = 0.0;
    scoreTitle = scoreArtist = scoreTuning = {};
    lastSentSeconds = -1.0;
    pending.erase (std::remove_if (pending.begin(), pending.end(),
                                   [] (const juce::var& c) { return c["type"] == "load" || c["type"] == "clear"; }),
                   pending.end());

    if (keep.isVoid())
        editing = false;   // 挿入を見せるための読み直しでは、編集モードを続ける (ページが同じ場所を選び直す)
    if (file.existsAsFile())
        send (makeCommand ("load", { { "url", "/score?n=" + juce::String (++loadCount) }, { "track", trackIndex },
                                     { "editable", GpScoreEdit::isEditable (file) }, { "keep", keep } }));
    else
        send (makeCommand ("clear"));
}

void ScoreViewer::endEditing()
{
    if (editing)
        send (makeCommand ("endEdit"));
}

void ScoreViewer::undoEdit() { send (makeCommand ("undo")); }
void ScoreViewer::redoEdit() { send (makeCommand ("redo")); }

void ScoreViewer::setCursorHidden (bool hidden)
{
    if (hidden != std::exchange (cursorHidden, hidden))
        send (makeCommand ("cursor", { { "hidden", hidden } }));
}

void ScoreViewer::setFrozen (bool frozen)
{
    send (makeCommand ("freeze", { { "on", frozen } }));
}

void ScoreViewer::setPosition (double seconds, double bpm, double offsetSeconds, double rate, bool playing)
{
    if (! rendered)
        return;

    // 再生中は、針をページの中で動かす (requestAnimationFrame)。こちらからは位置の補正を 0.1 秒ごとに送るだけでよい。
    // 止まっているときは、位置が変わったときに送る。再生・停止、BPM、ずらし、速度が変わったらすぐに送る
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const bool settingsChanged = playing != lastSentPlaying
                              || ! juce::approximatelyEqual (bpm, lastSentBpm)
                              || ! juce::approximatelyEqual (offsetSeconds, lastSentOffset)
                              || ! juce::approximatelyEqual (rate, lastSentRate);
    const bool positionChanged = playing ? now - lastSentAtMs >= 100.0
                                         : std::abs (seconds - lastSentSeconds) >= 0.01;
    if (! settingsChanged && ! positionChanged)
        return;

    lastSentSeconds = seconds;
    lastSentBpm = bpm;
    lastSentOffset = offsetSeconds;
    lastSentRate = rate;
    lastSentPlaying = playing;
    lastSentAtMs = now;
    send (makeCommand ("position", { { "seconds", seconds }, { "bpm", bpm }, { "offset", offsetSeconds },
                                     { "rate", rate }, { "playing", playing } }));
}

void ScoreViewer::setColours (const Palette& p)
{
    // タブ譜の色をテーマに合わせる (設計書 v0.7 4.10)。同じ種類の命令は最新のものだけ残す
    pending.erase (std::remove_if (pending.begin(), pending.end(),
                                   [] (const juce::var& c) { return c["type"] == "colours"; }),
                   pending.end());

    // 楽譜は紙に近い見た目にする: ライトなら白い背景に黒い音符、ダークなら暗い背景に白い音符
    const bool dark = p.background.getPerceivedBrightness() < 0.5f;
    const auto background = dark ? p.panel : juce::Colours::white;
    const auto notes = dark ? juce::Colours::white : juce::Colours::black;

    send (makeCommand ("colours", { { "background", css (background) },
                                    { "text", css (notes) },
                                    { "muted", css (notes.interpolatedWith (background, 0.35f)) },
                                    { "line", css (notes.interpolatedWith (background, 0.55f)) },
                                    { "accent", css (p.accent) } }));
}

void ScoreViewer::setEqualBarWidths (bool equal)
{
    pending.erase (std::remove_if (pending.begin(), pending.end(),
                                   [] (const juce::var& c) { return c["type"] == "layout"; }),
                   pending.end());

    send (makeCommand ("layout", { { "equalBars", equal } }));
}

void ScoreViewer::setMidiTracks (const std::vector<MidiTrack>& midiTracks)
{
    juce::Array<juce::var> list;
    for (const auto& t : midiTracks)
        list.add (makeCommand ("t", { { "index", t.index }, { "volume", t.volume }, { "muted", t.muted } }));

    send (makeCommand ("midiTracks", { { "tracks", list } }));
}

void ScoreViewer::setPdfSpeedPercent (int percent)
{
    send (makeCommand ("pdfSpeed", { { "percent", percent } }));
}

void ScoreViewer::setPdfRepeats (const juce::var& repeats, double pendingStart)
{
    pending.erase (std::remove_if (pending.begin(), pending.end(),
                                   [] (const juce::var& c) { return c["type"] == "pdfRepeats"; }),
                   pending.end());
    send (makeCommand ("pdfRepeats", { { "repeats", repeats }, { "pending", pendingStart } }));
}

void ScoreViewer::showTrack (int trackIndex)
{
    send (makeCommand ("track", { { "track", trackIndex } }));
}

void ScoreViewer::showCountdown (int number, const juce::String& caption)
{
    send (makeCommand ("countdown", { { "number", number }, { "caption", caption } }));
}

void ScoreViewer::handleEvent (const juce::var& event)
{
    const auto type = event["type"].toString();

    if (type == "pageReady")
    {
        pageReady = true;
        auto queued = std::move (pending);
        pending.clear();
        for (auto& command : queued)
            send (command);
        return;
    }

    if (type == "loaded")
    {
        scoreTempo = (double) event["tempo"];

        barStartBeats.clear();
        if (const auto* bars = event["bars"].getArray())
            for (const auto& beats : *bars)
                barStartBeats.push_back ((double) beats);

        barSignatures.clear();
        if (const auto* sigs = event["sigs"].getArray())
            for (const auto& sig : *sigs)
                barSignatures.push_back ({ (int) sig[0], (int) sig[1] });

        // 演奏する順の小節ごとの、楽譜の小節の番号 (ページは 0 からの番号を送ってくる)
        barNumbers.clear();
        if (const auto* numbers = event["barIndex"].getArray())
            for (const auto& n : *numbers)
                barNumbers.push_back ((int) n + 1);

        scoreEndBeats = (double) event["endBeats"];

        std::vector<int> strings;
        if (const auto* notes = event["tuning"].getArray())
            for (const auto& note : *notes)
                strings.push_back ((int) note);

        tracks.clear();
        if (const auto* list = event["tracks"].getArray())
            for (const auto& t : *list)
                tracks.push_back ({ t["name"].toString().trim(), (bool) t["guitar"] });

        scoreTitle = event["title"].toString().trim();
        scoreArtist = event["artist"].toString().trim();
        scoreTuning = MusicInfo::describeTuning (strings);

        // 挿入を見せるための読み直しでは、曲の BPM・リード・バッキングなどを決め直さない (onLoaded を呼ばない)
        if (std::exchange (previewLoading, false))
        {
            if (onPreviewLoaded != nullptr)
                onPreviewLoaded();
        }
        else if (onLoaded != nullptr)
        {
            onLoaded (scoreTempo);
        }
        return;
    }

    if (type == "rendered")
    {
        rendered = true;
        lastSentSeconds = -1.0;
        return;
    }

    if (type == "barClicked")
    {
        if (onBarClicked != nullptr)
            onBarClicked ((double) event["beats"]);
        return;
    }

    // ページの上でマウスが押された: 開いているアプリのメニューを閉じる (ふつうは「メニューの外を押した」で閉じるが、
    // ページは別の窓なので、JUCE は押されたことに気づかない)。右クリックなら、このあとで新しいメニューが開く
    if (type == "pagePointerDown")
    {
        juce::PopupMenu::dismissAllActiveMenus();
        return;
    }

    if (type == "pdfContext")
    {
        if (onPdfContext != nullptr)
            onPdfContext ((double) event["pos"], (int) event["repeat"]);
        return;
    }

    // 編集中の右クリック: 選んでいる場所 (拍・小節の挿入のメニューをアプリが出す)
    if (type == "editContext")
    {
        GpScoreEdit::NoteEdit place;
        place.slot = (int) event["slot"];
        place.masterBar = (int) event["bar"];
        place.voice = (int) event["voice"];
        place.beat = (int) event["beat"];
        place.string = (int) event["string"];
        if (const auto* tuning = event["tuning"].getArray())
            for (const auto& pitch : *tuning)
                place.tuning.push_back ((int) pitch);
        if (onEditContext != nullptr)
            onEditContext (place);
        return;
    }

    if (type == "undoInsert")
    {
        if (onUndoInsert != nullptr)
            onUndoInsert();
        return;
    }

    if (type == "noteEdited")
    {
        GpScoreEdit::NoteEdit edit;
        const auto kind = event["kind"].toString();
        edit.kind = kind == "add" ? GpScoreEdit::Kind::add : kind == "remove" ? GpScoreEdit::Kind::remove
                  : kind == "tie" ? GpScoreEdit::Kind::tie : GpScoreEdit::Kind::setFret;
        edit.tieOrigin = (bool) event["origin"];
        edit.tieDestination = (bool) event["destination"];
        edit.oldTieOrigin = (bool) event["oldOrigin"];
        edit.oldTieDestination = (bool) event["oldDestination"];
        if (const auto* tuning = event["tuning"].getArray())
            for (const auto& pitch : *tuning)
                edit.tuning.push_back ((int) pitch);
        edit.slot = (int) event["slot"];
        edit.masterBar = (int) event["bar"];
        edit.voice = (int) event["voice"];
        edit.beat = (int) event["beat"];
        edit.string = (int) event["string"];
        edit.oldFret = (int) event["oldFret"];
        edit.fret = (int) event["fret"];
        if (onNoteEdited != nullptr)
            onNoteEdited (edit);
        return;
    }

    if (type == "editMode")
    {
        editing = (bool) event["on"];
        // 編集モードに入ったら、キーの入力をページに渡す (数字キーをページで受け取るため)。
        // ダブルクリックの1回目のクリックで、練習画面が小節へ移動してキーの入力を取り戻していることがある
        if (editing && browser != nullptr)
            browser->grabKeyboardFocus();
        if (onEditModeChanged != nullptr)
            onEditModeChanged (editing);
        return;
    }

    if (type == "key")
    {
        if (onKey != nullptr)
            onKey (event["key"].toString());
        return;
    }

    if (type == "error" && onError != nullptr)
        onError (event["message"].toString());
}

int ScoreViewer::getBarNumber (int index) const
{
    if (barNumbers.empty() || index < 0)
        return index + 1;

    const auto last = (int) barNumbers.size() - 1;
    return index <= last ? barNumbers[(size_t) index] : barNumbers.back() + (index - last);
}
