#include "ScoreViewer.h"
#include "WebResources.h"

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

    browser = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*browser);
    browser->setBounds (getLocalBounds());
    browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot() + "score-view.html");
}

std::optional<juce::WebBrowserComponent::Resource> ScoreViewer::provide (const juce::String& path)
{
    // タブ譜のファイルそのもの。/score?n=番号 (番号は、同じファイルでも読み直させるため)
    if (path.startsWith ("/score?") || path == "/score")
    {
        const juce::ScopedLock lock (fileLock);
        juce::MemoryBlock data;
        if (scoreFile.loadFileAsData (data))
            return WebResources::fromMemory (data.getData(), data.getSize(), "application/octet-stream");
        return std::nullopt;
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
    }

    rendered = false;
    scoreTempo = 0.0;
    barStartBeats.clear();
    barSignatures.clear();
    scoreTitle = scoreArtist = scoreTuning = {};
    lastSentSeconds = -1.0;
    pending.erase (std::remove_if (pending.begin(), pending.end(),
                                   [] (const juce::var& c) { return c["type"] == "load" || c["type"] == "clear"; }),
                   pending.end());

    if (file.existsAsFile())
        send (makeCommand ("load", { { "url", "/score?n=" + juce::String (++loadCount) }, { "track", trackIndex } }));
    else
        send (makeCommand ("clear"));
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

        if (onLoaded != nullptr)
            onLoaded (scoreTempo);
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

    if (type == "key")
    {
        if (onKey != nullptr)
            onKey (event["key"].toString());
        return;
    }

    if (type == "error" && onError != nullptr)
        onError (event["message"].toString());
}
