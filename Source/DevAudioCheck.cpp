#include "DevAudioCheck.h"

#if JUCE_DEBUG
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Audio/AudioEngine.h"
#include "Audio/NamTone.h"
#include "UI/PluginToneCard.h"
#include "Platform/WindowOwner.h"

namespace
{
    // コマンドラインから、flag の次の引数 (引用符つきのパスにも対応) を取り出す
    juce::String argumentAfter (const juce::String& commandLine, const juce::String& flag)
    {
        auto tokens = juce::StringArray::fromTokens (commandLine, true);
        const auto index = tokens.indexOf (flag);
        if (index < 0)
            return {};   // その指定がない (-1 のまま次を読むと、先頭の引数を取ってしまう)

        return juce::isPositiveAndBelow (index + 1, tokens.size()) ? tokens[index + 1].unquoted() : juce::String();
    }

    void listDevices (juce::String& report)
    {
        report << "== Audio device types ==\n";

        juce::AudioDeviceManager manager;
        for (auto* type : manager.getAvailableDeviceTypes())
        {
            type->scanForDevices();   // 一覧を作るだけで、デバイスは開かない
            report << "[" << type->getTypeName() << "]\n";
            report << "  outputs: " << type->getDeviceNames (false).joinIntoString (" | ") << "\n";
            report << "  inputs:  " << type->getDeviceNames (true).joinIntoString (" | ") << "\n";
        }
        report << "\n";
    }

    void benchmarkNam (const juce::File& model, const juce::File& ir, juce::String& report)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 256;
        constexpr double seconds = 10.0;

        report << "== NAM benchmark ==\n"
               << "model: " << model.getFullPathName() << "\n"
               << "ir:    " << (ir == juce::File() ? juce::String ("(none)") : ir.getFullPathName()) << "\n";

        const auto loadStart = juce::Time::getMillisecondCounterHiRes();
        auto result = NamTone::load (model, ir, sampleRate, 2048);
        report << "load: " << juce::String (juce::Time::getMillisecondCounterHiRes() - loadStart, 1) << " ms\n";

        if (result.tone == nullptr)
        {
            report << "ERROR: " << result.error << "\n\n";
            return;
        }

        report << "model sample rate: " << result.tone->getModelSampleRate() << " Hz\n";

        // ギターの音の代わり: 110 Hz (A 弦) を、0.5 秒ごとに弾き直して減衰させる
        std::vector<float> block ((size_t) blockSize);
        const auto totalBlocks = (int) (seconds * sampleRate / blockSize);
        double totalMs = 0.0, worstMs = 0.0, sumSquares = 0.0;
        float peak = 0.0f;
        bool finite = true;
        juce::int64 sampleIndex = 0;

        for (int b = 0; b < totalBlocks; ++b)
        {
            for (auto& s : block)
            {
                const auto t = (double) (sampleIndex % (juce::int64) (sampleRate * 0.5)) / sampleRate;
                s = (float) (0.3 * std::exp (-t * 4.0) * std::sin (juce::MathConstants<double>::twoPi * 110.0 * (double) sampleIndex / sampleRate));
                ++sampleIndex;
            }

            const auto start = juce::Time::getHighResolutionTicks();
            result.tone->process (block.data(), blockSize);
            const auto ms = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start) * 1000.0;

            // 最初の数ブロックは、キャッシュなどの準備で遅いことがあるので、最悪値からは除く
            if (b >= 4)
                worstMs = juce::jmax (worstMs, ms);
            totalMs += ms;

            for (auto s : block)
            {
                finite = finite && std::isfinite (s);
                peak = juce::jmax (peak, std::abs (s));
                sumSquares += (double) s * s;
            }
        }

        const auto budgetMs = blockSize * 1000.0 / sampleRate;
        const auto averageMs = totalMs / totalBlocks;
        report << "blocks: " << totalBlocks << " x " << blockSize << " samples @ " << sampleRate << " Hz\n"
               << "budget per block: " << juce::String (budgetMs, 3) << " ms\n"
               << "average: " << juce::String (averageMs, 3) << " ms (" << juce::String (averageMs / budgetMs * 100.0, 1) << "% of budget)\n"
               << "worst:   " << juce::String (worstMs, 3) << " ms (" << juce::String (worstMs / budgetMs * 100.0, 1) << "% of budget)\n"
               << "output peak: " << juce::String (juce::Decibels::gainToDecibels (peak), 1) << " dBFS, rms: "
               << juce::String (juce::Decibels::gainToDecibels ((float) std::sqrt (sumSquares / (double) sampleIndex)), 1) << " dBFS\n"
               << "output finite: " << (finite ? "yes" : "NO") << "\n\n";
    }

    // VST3 プラグインを読み込み、チャンネル数・処理時間・編集画面の大きさ・状態の保存と復元を確かめる
    // (練習画面では非同期で作るが、ここはメッセージループが動く前なので同期で作る)
    void checkPlugin (const juce::File& vst3, juce::String& report)
    {
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 256;

        report << "== VST3 plugin check ==\n" << "file: " << vst3.getFullPathName() << "\n";

        juce::AudioPluginFormatManager formats;
        juce::addDefaultFormatsToManager (formats);

        juce::OwnedArray<juce::PluginDescription> types;
        for (auto* format : formats.getFormats())
            if (format->fileMightContainThisPluginType (vst3.getFullPathName()))
                format->findAllTypesForFile (types, vst3.getFullPathName());

        if (types.isEmpty())
        {
            report << "ERROR: no plugin types found\n\n";
            return;
        }

        report << "name: " << types[0]->name << " / " << types[0]->manufacturerName << " / " << types[0]->version << "\n";

        juce::String error;
        auto plugin = formats.createPluginInstance (*types[0], sampleRate, blockSize, error);
        if (plugin == nullptr)
        {
            report << "ERROR: " << error << "\n\n";
            return;
        }

        plugin->setRateAndBufferSizeDetails (sampleRate, blockSize);
        plugin->prepareToPlay (sampleRate, blockSize);
        report << "inputs: " << plugin->getTotalNumInputChannels() << ", outputs: " << plugin->getTotalNumOutputChannels()
               << ", latency: " << plugin->getLatencySamples() << " samples\n";

        // 処理時間 (5 秒分)
        const auto channels = juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
        juce::AudioBuffer<float> buffer (channels, blockSize);
        juce::MidiBuffer midi;
        const auto totalBlocks = (int) (5.0 * sampleRate / blockSize);
        double totalMs = 0.0, worstMs = 0.0;
        float peak = 0.0f;
        juce::int64 sampleIndex = 0;

        for (int b = 0; b < totalBlocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i, ++sampleIndex)
            {
                const auto t = (double) (sampleIndex % (juce::int64) (sampleRate * 0.5)) / sampleRate;
                const auto s = (float) (0.3 * std::exp (-t * 4.0) * std::sin (juce::MathConstants<double>::twoPi * 110.0 * (double) sampleIndex / sampleRate));
                for (int ch = 0; ch < channels; ++ch)
                    buffer.setSample (ch, i, ch < plugin->getTotalNumInputChannels() ? s : 0.0f);
            }

            const auto start = juce::Time::getHighResolutionTicks();
            plugin->processBlock (buffer, midi);
            const auto ms = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start) * 1000.0;
            totalMs += ms;
            if (b >= 4)
                worstMs = juce::jmax (worstMs, ms);
            peak = juce::jmax (peak, buffer.getMagnitude (0, 0, blockSize));
        }

        const auto budgetMs = blockSize * 1000.0 / sampleRate;
        report << "process average: " << juce::String (totalMs / totalBlocks, 3) << " ms, worst: " << juce::String (worstMs, 3)
               << " ms (budget " << juce::String (budgetMs, 3) << " ms)\n"
               << "output peak (left): " << juce::String (juce::Decibels::gainToDecibels (peak), 1) << " dBFS\n";

        // 状態の保存と復元
        juce::MemoryBlock state;
        plugin->getStateInformation (state);
        plugin->setStateInformation (state.getData(), (int) state.getSize());
        juce::MemoryBlock again;
        plugin->getStateInformation (again);
        report << "state: " << (int) state.getSize() << " bytes, restored: " << (int) again.getSize() << " bytes, same: "
               << (state == again ? "yes" : "no (plugin rewrites some values)") << "\n";

        // 編集画面の大きさ (画面には出さない)
        if (plugin->hasEditor())
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (plugin->createEditorAndMakeActive());
            if (editor != nullptr)
            {
                report << "editor size: " << editor->getWidth() << " x " << editor->getHeight()
                       << ", resizable: " << (editor->isResizable() ? "yes" : "no") << "\n";

                // 小さくできるか: 半分の大きさを頼んで、実際にどうなったか
                editor->setSize (editor->getWidth() / 2, editor->getHeight() / 2);
                report << "after asking half size: " << editor->getWidth() << " x " << editor->getHeight() << "\n";

                if (auto* constrainer = editor->getConstrainer())
                    report << "constrainer min: " << constrainer->getMinimumWidth() << " x " << constrainer->getMinimumHeight()
                           << ", fixed aspect: " << constrainer->getFixedAspectRatio() << "\n";
            }
            editor = nullptr;   // プラグインより先に消す
        }
        else
        {
            report << "editor: none (generic editor will be used)\n";
        }

        plugin->releaseResources();
        report << "\n";
    }
}

juce::String DevAudioCheck::run (const juce::String& commandLine)
{
    const juce::File folder (argumentAfter (commandLine, commandLineFlag));
    folder.createDirectory();

    juce::String report;
    report << "Guitar Practice audio check  " << juce::Time::getCurrentTime().toString (true, true) << "\n"
           << "JUCE_ASIO=" << JUCE_ASIO << "\n\n";

    listDevices (report);

    const auto model = argumentAfter (commandLine, "--nam");
    const auto ir = argumentAfter (commandLine, "--ir");
    if (model.isNotEmpty())
        benchmarkNam (juce::File (model), ir.isNotEmpty() ? juce::File (ir) : juce::File(), report);

    const auto plugin = argumentAfter (commandLine, "--plugin");
    if (plugin.isNotEmpty())
        checkPlugin (juce::File (plugin), report);

    folder.getChildFile ("audio-check.txt").replaceWithText (report);
    return report;
}

//==============================================================================
namespace
{
    // プラグインの画面のウィンドウのテスト。持ち主役の小さなウィンドウにカードを入れ、
    // エンジンにプラグインを非同期で読み込ませ、カードにウィンドウを開かせる。数秒後に様子を書き出して終わる
    class PluginWindowTest final : public juce::Timer
    {
    public:
        PluginWindowTest (const juce::File& outputFolder, const juce::File& vst3, std::function<void()> finished)
            : folder (outputFolder), onFinished (std::move (finished))
        {
            folder.createDirectory();
            log << "plugin: " << vst3.getFullPathName() << "\n";

            card = std::make_unique<PluginToneCard> (engine);
            card->setSize (380, 200);
            owner = std::make_unique<juce::DocumentWindow> ("plugin-window-test owner", juce::Colours::darkgrey,
                                                            juce::DocumentWindow::closeButton);
            owner->setUsingNativeTitleBar (true);
            owner->setContentNonOwned (card.get(), true);
            owner->setTopLeftPosition (40, 40);
            owner->setVisible (true);

            // 練習画面と同じつなぎ方
            engine.onPluginToneAboutToReplace = [this] { card->deleteEditor(); };
            engine.onPluginToneChanged = [this]
            {
                card->refreshEditor();
                log << elapsed() << " installed, editor created: " << (card->hasEditor() ? "yes" : "no")
                    << ", window showing: " << (card->isWindowShowing() ? "yes" : "no") << "\n";
            };

            card->requestOpenWindow();   // TONE3000 のボタンを押したのと同じ (できたときに開く)
            engine.loadPluginToneAsync (vst3, {}, [this] (bool ok, const juce::String& error)
            {
                log << elapsed() << " load: " << (ok ? juce::String ("ok") : error) << "\n";
            });

            startTimer (10000);   // 重いプラグインは読み込みに数秒かかるので、待ってから調べる
        }

        ~PluginWindowTest() override
        {
            engine.onPluginToneAboutToReplace = nullptr;
            engine.onPluginToneChanged = nullptr;
            card->deleteEditor();   // プラグインより先に、画面とウィンドウを消す
            owner = nullptr;
            card = nullptr;
        }

    private:
        void timerCallback() override
        {
            stopTimer();
            log << elapsed() << " check\n"
                << "editor size: " << card->getEditorSize().toString() << "\n"
                << "window showing (JUCE): " << (card->isWindowShowing() ? "yes" : "no") << "\n"
                << "window bounds (JUCE): " << card->getWindowScreenBounds().toString() << "\n"
                << "window (Windows): " << WindowOwner::describe (card->getWindowNativeHandle()) << "\n";
            folder.getChildFile ("plugin-window.txt").replaceWithText (log);

            if (onFinished != nullptr)
                onFinished();
        }

        juce::String elapsed() const
        {
            return "[" + juce::String ((juce::Time::getMillisecondCounterHiRes() - startMs) / 1000.0, 2) + "s]";
        }

        double startMs = juce::Time::getMillisecondCounterHiRes();
        juce::File folder;
        std::function<void()> onFinished;
        AudioEngine engine;   // デバイスは開かない (音は出さない)
        std::unique_ptr<PluginToneCard> card;
        std::unique_ptr<juce::DocumentWindow> owner;
        juce::String log;
    };
}

std::unique_ptr<juce::Timer> DevAudioCheck::startPluginWindowTest (const juce::String& commandLine, std::function<void()> onFinished)
{
    return std::make_unique<PluginWindowTest> (juce::File (argumentAfter (commandLine, pluginWindowTestFlag)),
                                               juce::File (argumentAfter (commandLine, "--plugin")),
                                               std::move (onFinished));
}
#endif
