// SPDX-License-Identifier: GPL-2.0+
#pragma once

#include <functional>
#include <string>

#include "capture_frame.h"

enum class TriggerSlope { Rising, Falling };

/// Common interface implemented by both Dso5200Control (real hardware) and
/// Simulator, so the CLI/streaming code doesn't need to care which one is active.
class IDsoSource {
  public:
    virtual ~IDsoSource() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isSampling() const = 0;

    /// Sets the total record duration shown on screen (e.g. 10 divisions * time/div).
    virtual bool setTimebase(double secondsPerScreen) = 0;
    virtual bool setVoltsPerDiv(int channel, double voltsPerDiv) = 0;
    virtual bool setChannelEnabled(int channel, bool enabled) = 0;
    virtual bool setTriggerSlope(TriggerSlope slope) = 0;

    virtual std::string statusText() const = 0;

    /// Called from a background thread whenever a new capture is ready.
    std::function<void(const CaptureFrame &)> onFrame;
};
