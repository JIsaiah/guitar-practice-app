#include "YouTubePlayer.h"
#include "WebResources.h"
#include <cstdlib>
#include "../Text.h"

namespace
{
    // https://juce.backend/ へのリクエストに、同梱のファイルを返す (ネットには出ない)
    std::optional<juce::WebBrowserComponent::Resource> provideResource (const juce::String& path)
    {
        return WebResources::findBundled (path == "/" ? juce::String ("/youtube-player.html") : path);
    }
}

YouTubePlayer::YouTubePlayer()
{
    setInterceptsMouseClicks (false, true);
}

YouTubePlayer::~YouTubePlayer() = default;

void YouTubePlayer::prepare()
{
    if (browser != nullptr)
        return;

    // アプリからの命令 (evaluateJavascript) で再生を始められるように、自動再生の制限を外す。
    // WebView2 が作られる前に、この環境変数を設定しておく必要がある
    _putenv_s ("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", "--autoplay-policy=no-user-gesture-required");

    const auto userData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                              .getChildFile ("GuitarPractice").getChildFile ("WebView2");

    auto options = juce::WebBrowserComponent::Options()
                       .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
                       .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2()
                                                    .withUserDataFolder (userData)
                                                    .withBackgroundColour (juce::Colour (0xff272b32)))
                       .withNativeIntegrationEnabled()
                       .withKeepPageLoadedWhenBrowserIsHidden()   // 隠している間もページを残す (先行準備のため)
                       .withResourceProvider (provideResource)
                       .withEventListener ("player", [this] (const juce::var& event) { handleEvent (event); });

    browser = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*browser);
    browser->setBounds (getLocalBounds());
    browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
}

void YouTubePlayer::resized()
{
    if (browser != nullptr)
        browser->setBounds (getLocalBounds());
}

void YouTubePlayer::placeOver (juce::Component& target)
{
    prepare();

    if (auto* parent = getParentComponent())
        setBounds (parent->getLocalArea (&target, target.getLocalBounds()));

    setVisible (true);
    toFront (false);
}

void YouTubePlayer::hide()
{
    setVisible (false);
}

//==============================================================================
void YouTubePlayer::send (const juce::var& command)
{
    if (browser == nullptr)
        return;

    browser->evaluateJavascript ("window.playerCommand && window.playerCommand(" + juce::JSON::toString (command, true) + ")");
}

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
}

void YouTubePlayer::load (const juce::String& videoId, double startSeconds)
{
    state.videoReady = false;
    state.playerState = -1;
    state.time = startSeconds;
    state.duration = 0.0;
    send (makeCommand ("load", { { "videoId", videoId }, { "start", startSeconds } }));
}

void YouTubePlayer::unload()
{
    send (makeCommand ("unload"));
    send (makeCommand ("loop", { { "a", 0.0 }, { "b", 0.0 }, { "on", false } }));
    state.videoReady = false;
    state.playerState = -1;
}

void YouTubePlayer::play()                      { send (makeCommand ("play")); }
void YouTubePlayer::pause()                     { send (makeCommand ("pause")); }
void YouTubePlayer::seek (double seconds)
{
    state.time = seconds;   // 次の報告が届くまでの間も、表示が新しい位置になるように
    state.receivedAtMs = juce::Time::getMillisecondCounterHiRes();
    send (makeCommand ("seek", { { "seconds", seconds } }));
}
void YouTubePlayer::setRate (double rate)       { send (makeCommand ("rate", { { "rate", rate } })); }
void YouTubePlayer::setVolume (double gain)     { send (makeCommand ("volume", { { "volume", juce::jlimit (0, 100, juce::roundToInt (gain * 100.0)) } })); }

void YouTubePlayer::setLoop (double startSeconds, double endSeconds, bool enabled)
{
    send (makeCommand ("loop", { { "a", startSeconds }, { "b", endSeconds }, { "on", enabled } }));
}

double YouTubePlayer::getEstimatedTime() const
{
    if (! isPlaying())
        return state.time;

    const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - state.receivedAtMs) / 1000.0;
    return state.time + juce::jlimit (0.0, 0.5, elapsed) * state.rate;
}

//==============================================================================
void YouTubePlayer::handleEvent (const juce::var& event)
{
    const auto type = event["type"].toString();

    if (type == "pageReady")
    {
        state.pageReady = true;
        return;
    }

    if (type == "apiReady")
    {
        state.apiReady = true;
        if (onApiReady != nullptr)
            onApiReady();
        return;
    }

    if (type == "key")
    {
        if (onKey != nullptr)
            onKey (event["key"].toString());
        return;
    }

    if (type == "error")
    {
        if (onError != nullptr)
            onError (describeError (event["code"]));
        return;
    }

    if (type == "ready" || type == "state" || type == "tick")
    {
        if (event.hasProperty ("state"))
        {
            state.playerState = (int) event["state"];
            state.time = (double) event["time"];
            state.duration = (double) event["duration"];
            state.rate = (double) event["rate"];
            state.receivedAtMs = juce::Time::getMillisecondCounterHiRes();
        }

        // 長さが分かれば、操作できる状態
        if (type == "ready" || state.duration > 0.0)
            state.videoReady = true;

        if (onStateChanged != nullptr)
            onStateChanged();
    }
}

juce::String YouTubePlayer::describeError (const juce::var& code)
{
    if (code.toString() == "offline")
        return "YouTube に接続できません。インターネット接続を確認してください"_jp;

    switch ((int) code)
    {
        case 2:   return "動画の URL が正しくありません"_jp;
        case 5:   return "この動画は再生できません (プレイヤーのエラー)"_jp;
        case 100: return "動画が見つかりません (削除されたか、非公開です)"_jp;
        case 101:
        case 150: return "この動画は、ほかのアプリでの再生が許可されていません。YouTube のサイトで再生してください"_jp;
        case 153: return "YouTube が埋め込み先を確認できず、再生を拒否しました (エラー 153)"_jp;
        default:  return "YouTube のエラー ("_jp + code.toString() + ")";
    }
}
