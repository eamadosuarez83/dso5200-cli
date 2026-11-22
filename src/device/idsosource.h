// SPDX-License-Identifier: GPL-2.0+
#pragma once

#include <functional>
#include <string>

#include "capture_frame.h"

enum class TriggerSlope { Rising, Falling };

/// Acoplamiento de entrada. DC y AC son el relé de acoplamiento real de cada canal (bit de
/// CONTROL_SETRELAYS). GND no existe en el hardware del DSO-5200 (no hay relé de masa): se deja el
/// relé en DC y el canal se entrega como 0 V, que es lo que se vería con la entrada a masa.
enum class Coupling { DC, AC, GND };

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
    virtual bool setCoupling(int channel, Coupling coupling) = 0;
    virtual bool setTriggerSlope(TriggerSlope slope) = 0;
    /// Sets the trigger level, in volts, on the (fixed) trigger source channel.
    virtual bool setTriggerLevel(double volts) = 0;

    virtual std::string statusText() const = 0;

    /// Called from a background thread whenever a new capture is ready.
    std::function<void(const CaptureFrame &)> onFrame;
};
