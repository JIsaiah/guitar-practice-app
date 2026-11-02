#include "CountdownController.h"

void CountdownController::start (double seconds, double nowMs)
{
    if (seconds <= 0.0)
    {
        finish();
        return;
    }

    state = State::countingDown;
    endMs = nowMs + seconds * 1000.0;
}

void CountdownController::update (double nowMs)
{
    if (isCountingDown() && nowMs >= endMs)
        finish();
}

void CountdownController::skip()
{
    if (isCountingDown())
        finish();
}

void CountdownController::cancel()
{
    state = State::stopped;
}

int CountdownController::getDisplayNumber (double nowMs) const
{
    if (! isCountingDown())
        return 0;

    return juce::jmax (1, (int) std::ceil ((endMs - nowMs) / 1000.0));
}

PlayButtonPresses::Action PlayButtonPresses::press (double nowMs, bool countingDown, bool playing)
{
    const auto quick = nowMs - lastPressMs <= quickPressMs;

    auto action = Action::startCountdown;
    if (countingDown)
        action = quick ? Action::startNow : Action::cancel;
    else if (playing)
        action = quick && lastAction == Action::startCountdown ? Action::none : Action::pause;
    else if (quick && lastAction == Action::cancel)
        action = Action::startNow;

    lastPressMs = nowMs;
    lastAction = action;
    return action;
}

void CountdownController::finish()
{
    // onFinished の中で、もう一度 start() してもよいように、先に状態を戻す
    state = State::stopped;
    if (onFinished != nullptr)
        onFinished();
}
