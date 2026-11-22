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
    bool setCoupling(int channel, Coupling coupling) override;
    bool setTriggerSlope(TriggerSlope slope) override;
    bool setTriggerLevel(double volts) override;

    std::string statusText() const override;

    // TEMP TEST: investigating a CH2-specific flat-zero bug at 4V/div and 40V/div.
    bool debugSetOffsetFraction(unsigned channel, double offset) {
        std::lock_guard<std::mutex> lock(ioMutex);
        return doSetOffset(channel, offset);
    }

    // DEPURACIÓN: escribe un valor crudo (0..3) en ETsrBits.triggerSource, sin tocar trigger.source
    // ni el nivel (comando `rawsource`). MEDIDO 22-nov-2026 con la CAL en CH2 y el DAC de disparo
    // barrido en todo su rango (44..4095): solo 1 (CH1) dispara; 0, 2 y 3 nunca. El "0 = CH2" del
    // mapeo `1 - canal` heredado de OpenHantek no funciona en este aparato. Ver README §"Disparo".
    bool debugSetRawTriggerSource(uint8_t value) {
        std::lock_guard<std::mutex> lock(ioMutex);
        triggerCommand.setTriggerSource(value & 0x03);
        bulkCommand(triggerCommand);
        return true;
    }

    // TEMP TEST: reconstructing the real V/div table (GainBits + relay decade), bypassing kGain.
    bool debugSetRawGain(unsigned channel, uint8_t gainBits, bool below1V, bool below100mV);

    // CALIBRACIÓN: activa el volcado de estadísticas del ADC crudo (cuentas puras, sin
    // conversión a volts) para el canal indicado, impreso a stderr en cada frame.
    // CALIBRACIÓN DE CERO: con la entrada del canal a 0 V (GND o punta en corto), toma el
    // promedio crudo de la última captura como el cero del grupo actual. Devuelve false si
    // todavía no hay captura. `correctionOut` recibe la corrección aplicada (cuentas).
    bool calibrateZero(unsigned channel, double *correctionOut = nullptr);

    void debugSetRawStats(bool enabled, unsigned channel) {
        rawStatsEnabled = enabled;
        rawStatsChannel = channel;
    }

  private:
    struct VoltageSettings {
        // Posición de los 0 V como fracción 0..1 del rango del ADC (0.5 = centro). Ver doSetOffset().
        double offset = 0.5;
        uint16_t offsetDac = 0; // valor enviado al DAC de offset (12 bits)
        unsigned gain = 4; // índice en kGain, por omisión 0.5 V/div
        bool used = true;
        Coupling coupling = Coupling::DC;
    };
    struct TriggerSettings {
        double level[2] = {0.0, 0.0};
        double position = 0.5; // pretrigger position, 0..1 (0.5 = centered, matches real hardware/software default - see doSetPretriggerPosition())
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
    bool applyTimebaseTableEntry(unsigned index, double *windowOut);
    bool doSetGain(unsigned channel, double gain);
    bool doSetOffset(unsigned channel, double offset);
    bool doSetTriggerLevel(unsigned channel, double level);
    bool doSetTriggerSlopeInternal(uint8_t slope);
    bool doSetChannelUsed(unsigned channel, bool used);
    bool doSetCoupling(unsigned channel, Coupling coupling);
    bool doSetPretriggerPosition(double position);
    bool retrieveChannelLevelData();
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

    // Grupo analógico físico (relés). Solo hay 3; los 9 V/div se reparten de a 3 por grupo y
    // dentro de un grupo la escala es puramente matemática. Ver kHwGroup en dso5200.cpp.
    struct HwGroup {
        bool below1V;
        bool below100mV;
    };
    static const HwGroup kHwGroup[3];
    static const double kCountsPerVolt[2][3]; // [canal][grupo], medido
    static const double kDacSlope[2], kDacIntercept[2]; // ley DAC de offset -> cuenta ADC, medida
    static const double kTrigGain, kTrigOffsetCounts; // corrección del comparador de disparo, medida
    double rawFromDac(unsigned channel, double dac) const;
    uint16_t dacFromRaw(unsigned channel, double raw) const;
    double countsPerVolt(unsigned channel) const { return kCountsPerVolt[channel][hwGroupOf(voltage[channel].gain)]; }
    static constexpr double kAdcMax = 1023.0; // ADC de 10 bits
    static unsigned hwGroupOf(unsigned gainID) { return gainID / 3; }
    // Cuenta del ADC que corresponde a 0 V en la entrada, con el offset y el grupo actuales.
    double rawZero(unsigned channel) const;
    // Corrección aditiva (en cuentas) de rawZero por canal y grupo, fijada con el comando `zero`.
    double zeroCorrection[2][3] = {{0, 0, 0}, {0, 0, 0}};
    // Promedio crudo de la última captura por canal (lo usa calibrateZero()).
    double lastRawMean[2] = {-1.0, -1.0};
    // Muestras recortadas (cuenta 0 o 1023) en la última captura, por canal (para `status`).
    std::atomic<uint32_t> lastClipped[2] = {0, 0};

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
    bool rawStatsEnabled = false;
    unsigned rawStatsChannel = 0;
};
