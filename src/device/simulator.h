// SPDX-License-Identifier: GPL-2.0+
//
// Synthetic signal source with the exact same interface as Dso5200Control, so
// the CLI, streaming server and Python viewer can all be exercised end-to-end
// without the oscilloscope connected (selected via --simulate).

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "idsosource.h"

class Simulator : public IDsoSource {
  public:
    Simulator();
    ~Simulator() override;

    void start() override;
    void stop() override;
    bool isSampling() const override;

    bool setTimebase(double secondsPerScreen) override;
    bool setVoltsPerDiv(int channel, double voltsPerDiv) override;
    bool setChannelEnabled(int channel, bool enabled) override;
    bool setTriggerSlope(TriggerSlope slope) override;
    bool setTriggerLevel(double volts) override;

    std::string statusText() const override;

  private:
    void threadMain();

    static constexpr unsigned kChannels = 2;
    static constexpr unsigned kSamplesPerChannel = 2000;

    mutable std::mutex mutex;
    double secondsPerScreen = 0.001;
    double voltsPerDiv[kChannels] = {0.5, 0.5};
    bool channelEnabled[kChannels] = {true, true};
    TriggerSlope slope = TriggerSlope::Rising;
    double triggerLevel = 0.0;

    std::atomic<bool> sampling{false};
    std::atomic<bool> running{false};
    std::thread worker;
    uint32_t frameCounter = 0;
    double phaseAccumulator = 0.0;
};
