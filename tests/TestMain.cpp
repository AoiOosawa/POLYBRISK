#include <juce_audio_processors/juce_audio_processors.h>

#include <iostream>

int main()
{
    // APVTS が Timer / MessageManager を使うため GUI サブシステムを初期化する
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    int passes = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (const auto* result = runner.getResult (i))
        {
            failures += result->failures;
            passes += result->passes;
        }
    }

    std::cout << "\nPOLYBRISK tests: " << passes << " passed, " << failures << " failed" << std::endl;
    return failures == 0 ? 0 : 1;
}
