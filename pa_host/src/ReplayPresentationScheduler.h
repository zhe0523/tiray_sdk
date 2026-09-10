#pragma once

#include <QtGlobal>

/* 纯时间调度策略，用累计时间线补偿 Qt 5 整数毫秒定时误差。 */
class ReplayPresentationScheduler {
public:
    void setTargetFps(int fps);
    int targetFps() const;

    void reset();
    int delayMs(qint64 nowNs) const;
    void markPresented(qint64 nowNs);

private:
    qint64 framePeriodNs() const;

    qint64 nextPresentationNs_ = 0;
    int targetFps_ = 30;
};
