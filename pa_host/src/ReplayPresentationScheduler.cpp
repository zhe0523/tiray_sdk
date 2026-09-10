#include "ReplayPresentationScheduler.h"

#include <algorithm>

namespace {
constexpr qint64 kNanosecondsPerSecond = 1000000000LL;
constexpr qint64 kNanosecondsPerMillisecond = 1000000LL;
}

void ReplayPresentationScheduler::setTargetFps(int fps) {
    const int clampedFps = std::max(1, std::min(120, fps));
    if (targetFps_ != clampedFps) {
        targetFps_ = clampedFps;
        reset();
    }
}

int ReplayPresentationScheduler::targetFps() const {
    return targetFps_;
}

void ReplayPresentationScheduler::reset() {
    nextPresentationNs_ = 0;
}

int ReplayPresentationScheduler::delayMs(qint64 nowNs) const {
    const qint64 remainingNs = nextPresentationNs_ - std::max<qint64>(0, nowNs);
    if (remainingNs <= 0) {
        return 0;
    }
    return static_cast<int>((remainingNs + kNanosecondsPerMillisecond - 1)
        / kNanosecondsPerMillisecond);
}

void ReplayPresentationScheduler::markPresented(qint64 nowNs) {
    const qint64 periodNs = framePeriodNs();
    const qint64 presentationNs = std::max<qint64>(0, nowNs);
    do {
        nextPresentationNs_ += periodNs;
    } while (nextPresentationNs_ <= presentationNs);
}

qint64 ReplayPresentationScheduler::framePeriodNs() const {
    return kNanosecondsPerSecond / targetFps_;
}
