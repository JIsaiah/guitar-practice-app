#include "DevAudioCheck.h"

#if JUCE_DEBUG
#include <juce_audio_devices/juce_audio_devices.h>
#include "Audio/NamTone.h"

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

    folder.getChildFile ("audio-check.txt").replaceWithText (report);
    return report;
}
#endif
