// SPDX-License-Identifier: GPL-2.0+
//
// Specialized, Qt-free control class for the Hantek DSO-5200/5200A.
//
// This is a focused port of the relevant parts of
// openhantek/src/hantekdso/hantekdsocontrol.cpp, specialized for exactly one
// fixed device specification (see modelDSO5200.cpp) instead of the generic
// multi-model dispatch OpenHantek uses. Only "standard" (non-roll-mode) capture
// is supported, matching the default record length used here; OpenHantek's
// roll-mode scrolling display is out of scope for this tool.

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "idsosource.h"
#include "protocol/bulkStructs.h"
#include "protocol/controlStructs.h"
#include "protocol/definitions.h"

class USBDevice;

class Dso5200Control : public IDsoSource {
  public:
    /// \param device Must already be connected (USBDevice::connectDevice()). Not owned.
    /// \param debugLog When true, traces capture-state transitions and sample counts to stderr.
    explicit Dso5200Control(USBDevice *device, bool debugLog = false);
    ~Dso5200Control() override;

    void start() override;
    void stop() override;
    bool isSampling() const override;

    bool setTimebase(double secondsPerScreen) override;
    bool setVoltsPerDiv(int channel, double voltsPerDiv) override;
    bool setChannelEnabled(int channel, bool enabled) override;
    bool setTriggerSlope(TriggerSlope slope) override;

    std::string statusText() const override;

  private:
    struct VoltageSettings {
        double offset = 0.0;
        double offsetReal = 0.0;
        unsigned gain = 4; // index into kGain, defaults to the 4.00 V/div step
        bool used = true;
    };
    struct TriggerSettings {
        double level[2] = {0.0, 0.0};
        double position = 0.0; // pretrigger position, 0..1
        unsigned point = 0;
        Hantek::DTriggerPositionUsed used = Hantek::DTriggerPositionUsed::ON;
        uint8_t slope = 0; // 0 = rising, 1 = falling
        unsigned source = 0; // 0 = CH1, 1 = CH2
    };

    // --- Ported from HantekDsoControl, specialized for the fixed DSO-5200 spec ---
    bool isFastRate() const;
    unsigned getRecordLength() const;
    unsigned getSampleCount() const;
    double getBestSamplerate(double samplerate, bool fastRate, bool maximum, unsigned *downsampler) const;
    bool updateSamplerate(unsigned downsampler, bool fastRate);
    bool doSetRecordTime(double duration);
    bool doSetGain(unsigned channel, double gain);
    bool doSetOffset(unsigned channel, double offset);
    bool doSetTriggerLevel(unsigned channel, double level);
    bool doSetTriggerSlopeInternal(uint8_t slope);
    bool doSetChannelUsed(unsigned channel, bool used);
    bool doSetPretriggerPosition(double position);
    bool retrieveChannelLevelData();
    static unsigned calculateTriggerPoint(unsigned value);
    std::pair<int, unsigned> getCaptureState();
    std::vector<unsigned char> getSamples(unsigned &previousSampleCount);
    CaptureFrame convertRawDataToSamples(const std::vector<unsigned char> &rawData);
    int bulkCommand(const std::vector<uint8_t> &command, int attempts = 3);
    void setLastError(const std::string &message);

    void threadMain();

    USBDevice *device;
    mutable std::mutex ioMutex; // serializes USB access between CLI thread and the polling thread

    // Fixed DSO-5200 specification (openhantek/src/hantekdso/models/modelDSO5200.cpp)
    static constexpr unsigned kChannels = 2;
    static constexpr unsigned char kSampleSize = 10;
    static constexpr double kSingleBase = 100e6, kSingleMax = 125e6;
    static constexpr double kMultiBase = 200e6, kMultiMax = 250e6;
    static constexpr unsigned kMaxDownsampler = 131072;
    static constexpr unsigned kBufferDivider = 1; // recordLengthId is fixed to 1 (divider 1, not roll mode)
    static constexpr unsigned kRecordLengthSingle = 10240;
    static constexpr unsigned kRecordLengthMulti = 20480;
    struct GainStep {
        unsigned char index;
        double voltsPerDiv;
    };
    static const GainStep kGain[9];
    static const unsigned short kVoltageLimit[9];

    // Runtime state (mirrors Dso::ControlSettings, trimmed to what this tool exposes)
    VoltageSettings voltage[kChannels];
    TriggerSettings trigger;
    unsigned usedChannels = kChannels;
    unsigned downsampler = 1;
    double samplerateCurrent = 1e8;
    bool fastRateActive = false;
    Hantek::OffsetsPerGainStep offsetLimit[kChannels];

    // Persistent, incrementally-modified command buffers (mirrors HantekDsoControl's
    // command[] registry, but as plain members since we only ever target one model).
    Hantek::ControlBeginCommand beginCommand;
    Hantek::ControlGetLimits getLimitsCommand{kChannels};
    Hantek::BulkSetSamplerate5200 samplerateCommand;
    Hantek::BulkSetBuffer5200 bufferCommand;
    Hantek::BulkSetTrigger5200 triggerCommand;
    Hantek::BulkSetGain gainCommand;
    Hantek::ControlSetOffset offsetCommand;
    Hantek::ControlSetRelays relaysCommand;

    std::atomic<bool> sampling{false};
    std::atomic<bool> running{false};
    std::thread worker;

    unsigned expectedSampleCount = 0;
    int captureState = 0; // Hantek::CAPTURE_WAITING
    bool samplingStarted = false;
    int cycleCounter = 0;
    int startCycle = 0;
    int cycleTimeMs = 100;
    uint32_t frameCounter = 0;
    double targetTimebaseSeconds = 0.001; // remembered so channel toggles can restore it (like restoreTargets())

    mutable std::mutex statusMutex;
    std::string lastError;
    bool debugLog;
};
