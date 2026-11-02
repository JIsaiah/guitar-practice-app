#include <juce_events/juce_events.h>
#include <iostream>

// 自動テスト (JUCE UnitTest) をすべて実行する。失敗があれば終了コード 1
//   cmake --build build --config Debug --target GuitarPracticeTests
//   ctest --test-dir build -C Debug --output-on-failure
namespace
{
    // JUCE はテストの記録をデバッガーに出すので、コンソールにも出す
    class ConsoleRunner final : public juce::UnitTestRunner
    {
        void logMessage (const juce::String& message) override
        {
            std::cout << message.toStdString() << std::endl;
        }
    };
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;   // juce::Time や juce::Uuid などを使うため、JUCE を初期化する

    ConsoleRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int passes = 0, failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (const auto* result = runner.getResult (i))
        {
            passes += result->passes;
            failures += result->failures;
        }

    std::cout << "\n" << runner.getNumResults() << " tests, " << passes << " checks passed, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
