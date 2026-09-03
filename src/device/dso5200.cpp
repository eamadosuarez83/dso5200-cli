// SPDX-License-Identifier: GPL-2.0+
//
// See dso5200.h. The samplerate/gain/trigger arithmetic and the 10-bit sample
// unpacking below are ported near-verbatim from
// openhantek/src/hantekdso/hantekdsocontrol.cpp (getBestSamplerate,
// updateSamplerate, setGain, setOffset, setTriggerLevel, setChannelUsed,
// setTriggerSlope, setPretriggerPosition, getCaptureState, getSamples,
// convertRawDataToSamples, run()), specialized for the DSO-5200's fixed
// CSETTRIGGERORSAMPLERATE/ESETTRIGGERORSAMPLERATE/DSETBUFFER command set
// instead of the generic multi-model switch statements.
//
// One deliberate deviation from upstream: OpenHantek's setRecordTime() never
// actually calls getBestSamplerate() with the requested duration (it always
// passes downsampler=0, which pins the rate at the device maximum) - a latent
// bug in the original GUI, presumably masked there because the GUI mostly
// drives the equivalent setSamplerate() path instead. Since this tool's whole
// point is a working `timebase` command, doSetRecordTime() here follows
// setSamplerate()'s (correct) pattern instead of setRecordTime()'s.

#include "dso5200.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>

#include "protocol/states.h"
#include "usb/usbdevice.h"

using namespace Hantek;

const Dso5200Control::GainStep Dso5200Control::kGain[9] = {
    {1, 0.16}, {0, 0.40}, {0, 0.80}, {1, 1.60}, {0, 4.00}, {0, 8.00}, {1, 16.0}, {0, 40.0}, {0, 80.0},
};

const unsigned short Dso5200Control::kVoltageLimit[9] = {368, 454, 908, 368, 454, 908, 368, 454, 908};

Dso5200Control::Dso5200Control(USBDevice *device, bool debugLog) : device(device), debugLog(debugLog) {
    retrieveChannelLevelData();

    // Fix for a real bug found testing against hardware: the DSO-5200's ETsrBits.triggerSource
    // field uses an inverted channel mapping (ported from HantekDsoControl::setTriggerSource(),
    // which computes `1 - hardwareID`). Left at its zero-initialized default, the trigger
    // comparator ends up watching CH2 instead of CH1 - it would never see a real edge on the
    // signal connected to CH1, so the tool always fell back to force-triggering (unstable
    // display). trigger.source is fixed to channel 0 (CH1) in this tool, so this only needs
    // setting once, here.
    triggerCommand.setTriggerSource((uint8_t)(1 - trigger.source));
    bulkCommand(triggerCommand);

    doSetChannelUsed(0, true);
    doSetChannelUsed(1, true);
    doSetGain(0, kGain[voltage[0].gain].voltsPerDiv);
    doSetGain(1, kGain[voltage[1].gain].voltsPerDiv);
    doSetTriggerSlopeInternal(0);

    bufferCommand.setUsedPre(DTriggerPositionUsed::ON);
    bufferCommand.setUsedPost(DTriggerPositionUsed::ON);
    bufferCommand.setRecordLength(1);
    bulkCommand(bufferCommand);

    doSetRecordTime(targetTimebaseSeconds);

    running = true;
    worker = std::thread(&Dso5200Control::threadMain, this);
}

Dso5200Control::~Dso5200Control() {
    running = false;
    if (worker.joinable()) worker.join();
}

void Dso5200Control::start() { sampling = true; }
void Dso5200Control::stop() { sampling = false; }
bool Dso5200Control::isSampling() const { return sampling.load(); }

void Dso5200Control::setLastError(const std::string &message) {
    std::lock_guard<std::mutex> lock(statusMutex);
    lastError = message;
}

// --- Public setters (called from the CLI thread) ---------------------------

bool Dso5200Control::setTimebase(double secondsPerScreen) {
    if (secondsPerScreen <= 0.0) return false;
    std::lock_guard<std::mutex> lock(ioMutex);
    targetTimebaseSeconds = secondsPerScreen;
    return doSetRecordTime(secondsPerScreen);
}

bool Dso5200Control::setVoltsPerDiv(int channel, double voltsPerDiv) {
    if (channel < 0 || channel >= (int)kChannels || voltsPerDiv <= 0.0) return false;
    std::lock_guard<std::mutex> lock(ioMutex);
    return doSetGain((unsigned)channel, voltsPerDiv);
}

bool Dso5200Control::setChannelEnabled(int channel, bool enabled) {
    if (channel < 0 || channel >= (int)kChannels) return false;
    std::lock_guard<std::mutex> lock(ioMutex);
    return doSetChannelUsed((unsigned)channel, enabled);
}

bool Dso5200Control::setTriggerSlope(TriggerSlope slope) {
    std::lock_guard<std::mutex> lock(ioMutex);
    return doSetTriggerSlopeInternal(slope == TriggerSlope::Rising ? 0 : 1);
}

bool Dso5200Control::setTriggerLevel(double volts) {
    std::lock_guard<std::mutex> lock(ioMutex);
    return doSetTriggerLevel(trigger.source, volts);
}

std::string Dso5200Control::statusText() const {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(3);
    oss << "sampling=" << (sampling.load() ? "on" : "off") << " samplerate=" << (samplerateCurrent / 1e6) << "MS/s"
        << " fastRate=" << (fastRateActive ? "yes" : "no") << " ch1=" << (voltage[0].used ? "on" : "off") << "@"
        << kGain[voltage[0].gain].voltsPerDiv << "V/div"
        << " ch2=" << (voltage[1].used ? "on" : "off") << "@" << kGain[voltage[1].gain].voltsPerDiv << "V/div"
        << " slope=" << (trigger.slope == 0 ? "rising" : "falling")
        << " triggerLevel=" << trigger.level[trigger.source] << "V(ch" << (trigger.source + 1) << ")";
    std::lock_guard<std::mutex> lock(statusMutex);
    if (!lastError.empty()) oss << " lastError=\"" << lastError << "\"";
    return oss.str();
}

// --- Ported device logic -----------------------------------------------------

bool Dso5200Control::isFastRate() const { return fastRateActive; }

unsigned Dso5200Control::getRecordLength() const { return fastRateActive ? kRecordLengthMulti : kRecordLengthSingle; }

unsigned Dso5200Control::getSampleCount() const {
    return isFastRate() ? getRecordLength() : getRecordLength() * kChannels;
}

double Dso5200Control::getBestSamplerate(double samplerate, bool fastRate, bool maximum,
                                         unsigned *downsamplerOut) const {
    if (samplerate <= 0.0) return 0.0;

    const double base = fastRate ? kMultiBase : kSingleBase;
    const double max = fastRate ? kMultiMax : kSingleMax;

    double bestDownsampler = base / kBufferDivider / samplerate;
    double bestSamplerate;

    if (bestDownsampler < 1.0 && (samplerate <= max / kBufferDivider || !maximum)) {
        bestDownsampler = 0.0;
        bestSamplerate = max / kBufferDivider;
    } else {
        // DSO-5200 may not support all downsampling factors, requires testing (same caveat as upstream).
        bestDownsampler = maximum ? std::ceil(bestDownsampler) : std::floor(bestDownsampler);
        if (bestDownsampler > kMaxDownsampler) bestDownsampler = kMaxDownsampler;
        bestSamplerate = base / bestDownsampler / kBufferDivider;
    }

    if (downsamplerOut) *downsamplerOut = (unsigned)bestDownsampler;
    return bestSamplerate;
}

bool Dso5200Control::updateSamplerate(unsigned downsamplerValue, bool fastRate) {
    const double base = fastRate ? kMultiBase : kSingleBase;
    const double max = fastRate ? kMultiMax : kSingleMax;

    // Split the resulting divider into the values understood by the device.
    const long int valueSlow = std::max(((long int)downsamplerValue - 3) / 2, (long int)0);
    const unsigned char valueFast = (unsigned char)((long int)downsamplerValue - valueSlow * 2);

    samplerateCommand.setSamplerateFast((uint8_t)(4 - valueFast));
    samplerateCommand.setSamplerateSlow(valueSlow == 0 ? 0 : (uint16_t)(0xffff - valueSlow));
    triggerCommand.setFastRate(fastRate);

    bulkCommand(samplerateCommand);
    bulkCommand(triggerCommand);

    fastRateActive = fastRate;
    downsampler = downsamplerValue;
    if (downsamplerValue)
        samplerateCurrent = base / kBufferDivider / downsamplerValue;
    else
        samplerateCurrent = max / kBufferDivider;

    doSetPretriggerPosition(trigger.position);
    return true;
}

bool Dso5200Control::doSetRecordTime(double duration) {
    if (duration <= 0.0) return false;

    const double targetSamplerate = (double)kRecordLengthSingle / duration;
    const bool fastRate = (usedChannels <= 1) && (targetSamplerate > kSingleMax / kBufferDivider);

    unsigned downsamplerValue = 0;
    getBestSamplerate(targetSamplerate, fastRate, false, &downsamplerValue);

    return updateSamplerate(downsamplerValue, fastRate);
}

bool Dso5200Control::doSetGain(unsigned channel, double gain) {
    // Find the lowest gain step that's at least as high as the requested V/div.
    unsigned gainID;
    for (gainID = 0; gainID < 8; ++gainID)
        if (kGain[gainID].voltsPerDiv >= gain) break;

    gainCommand.setGain(channel, kGain[gainID].index);
    relaysCommand.setBelow1V(channel, gainID < 3);
    relaysCommand.setBelow100mV(channel, gainID < 6);

    voltage[channel].gain = gainID;

    bulkCommand(gainCommand);
    device->controlWrite(&relaysCommand);

    return doSetOffset(channel, voltage[channel].offset);
}

bool Dso5200Control::doSetOffset(unsigned channel, double offset) {
    Offset &limit = offsetLimit[channel].step[voltage[channel].gain];
    const unsigned short minimum =
        ((unsigned short)*((unsigned char *)&limit.start) << 8) + *((unsigned char *)&limit.start + 1);
    const unsigned short maximum =
        ((unsigned short)*((unsigned char *)&limit.end) << 8) + *((unsigned char *)&limit.end + 1);
    const unsigned short offsetValue = (unsigned short)(offset * (maximum - minimum) + minimum + 0.5);
    const double offsetReal = (double)(offsetValue - minimum) / (maximum - minimum);

    offsetCommand.setChannel(channel, offsetValue);
    voltage[channel].offsetReal = offsetReal;
    voltage[channel].offset = offset;

    device->controlWrite(&offsetCommand);

    return doSetTriggerLevel(channel, trigger.level[channel]);
}

bool Dso5200Control::doSetTriggerLevel(unsigned channel, double level) {
    trigger.level[channel] = level;

    Offset &limit = offsetLimit[channel].step[voltage[channel].gain];
    const unsigned short minimum =
        ((unsigned short)*((unsigned char *)&limit.start) << 8) + *((unsigned char *)&limit.start + 1);
    const unsigned short maximum =
        ((unsigned short)*((unsigned char *)&limit.end) << 8) + *((unsigned char *)&limit.end + 1);

    const double offsetReal = voltage[channel].offsetReal;
    const double gainStep = kGain[voltage[channel].gain].voltsPerDiv;
    unsigned short levelValue =
        (unsigned short)(((offsetReal + level / gainStep) * (maximum - minimum) + 0.5) + minimum);
    levelValue = std::clamp(levelValue, minimum, maximum);

    if (debugLog) {
        std::cerr << "[dso5200] doSetTriggerLevel(ch=" << channel << ", level=" << level
                  << "V): gain=" << voltage[channel].gain << " gainStep=" << gainStep
                  << " offsetReal=" << offsetReal << " minimum=" << minimum << " maximum=" << maximum
                  << " -> levelValue=" << levelValue << " (trigger.source=" << trigger.source << ")\n";
    }

    if (channel == trigger.source) {
        offsetCommand.setTrigger(levelValue);
        int rc = device->controlWrite(&offsetCommand);
        if (debugLog) std::cerr << "[dso5200] controlWrite(offsetCommand) -> " << rc << "\n";
    }
    return true;
}

bool Dso5200Control::doSetTriggerSlopeInternal(uint8_t slope) {
    triggerCommand.setTriggerSlope(slope);
    trigger.slope = slope;
    bulkCommand(triggerCommand);
    return true;
}

bool Dso5200Control::doSetChannelUsed(unsigned channel, bool used) {
    voltage[channel].used = used;
    unsigned channelCount = (voltage[0].used ? 1u : 0u) + (voltage[1].used ? 1u : 0u);

    UsedChannels usedChannelsValue = UsedChannels::USED_CH1;
    if (voltage[1].used) usedChannelsValue = voltage[0].used ? UsedChannels::USED_CH1CH2 : UsedChannels::USED_CH2;

    triggerCommand.setUsedChannels((uint8_t)usedChannelsValue);
    bulkCommand(triggerCommand);

    const bool fastRateChanged = (usedChannels <= 1) != (channelCount <= 1);
    usedChannels = channelCount;

    // Fast rate availability changed, which affects the current samplerate mapping.
    if (fastRateChanged) doSetRecordTime(targetTimebaseSeconds);

    return true;
}

bool Dso5200Control::doSetPretriggerPosition(double position) {
    double positionSamples = position * samplerateCurrent;
    const unsigned recordLength = getRecordLength();
    if (isFastRate()) positionSamples /= kChannels;

    const unsigned positionPre = 0xffff - recordLength + (unsigned)positionSamples;
    const unsigned positionPost = 0xffff - (unsigned)positionSamples;

    bufferCommand.setTriggerPositionPre((uint16_t)positionPre);
    bufferCommand.setTriggerPositionPost((uint16_t)positionPost);
    bulkCommand(bufferCommand);

    trigger.position = position;
    return true;
}

bool Dso5200Control::retrieveChannelLevelData() {
    int errorCode = device->controlRead(&getLimitsCommand);
    if (errorCode < 0) {
        setLastError("Couldn't get channel level data from oscilloscope: " + libUsbErrorString(errorCode));
        return false;
    }
    std::memcpy(offsetLimit, getLimitsCommand.data(), sizeof(OffsetsPerGainStep) * kChannels);
    return true;
}

// Note: upstream OpenHantek decodes the raw GETCAPTURESTATE trigger point with a bit-inversion
// trick before using it (see calculateTriggerPoint() history for the exact algorithm; it's also
// used unmodified in the sigrok hantek-dso driver's dso_get_capturestate(), whose comment admits
// "No idea why the device reports the trigger point this way" - the origin of that decode is
// itself uncertain, empirically reverse-engineered against the DSO-2090/2150). Tested against a
// real DSO-5200A: the decode does NOT apply here - using the raw trigger point directly (see
// threadMain() below) locks the trigger position to within 1-2 samples across dozens of captures,
// while running it through that decode scattered it across the whole capture window. Whatever the
// decode compensates for on other models, the DSO-5200(A) doesn't need it.

int Dso5200Control::bulkCommand(const std::vector<uint8_t> &command, int attempts) {
    int errorCode = device->controlWrite(&beginCommand);
    if (errorCode < 0) return errorCode;
    return device->bulkWrite(command.data(), (unsigned)command.size(), attempts);
}

std::pair<int, unsigned> Dso5200Control::getCaptureState() {
    int errorCode = bulkCommand(BulkGetCaptureState(), 1);
    if (errorCode < 0) return {(int)CAPTURE_ERROR, 0};

    BulkResponseGetCaptureState response;
    errorCode = device->bulkRead(&response);
    if (errorCode < 0) return {(int)CAPTURE_ERROR, 0};

    return {(int)response.getCaptureState(), response.getTriggerPoint()};
}

std::vector<unsigned char> Dso5200Control::getSamples(unsigned &previousSampleCount) {
    int errorCode = bulkCommand(BulkGetData(), 1);
    if (errorCode < 0) {
        setLastError("Getting sample data failed: " + libUsbErrorString(errorCode));
        return {};
    }

    unsigned totalSampleCount = getSampleCount();
    if (totalSampleCount < previousSampleCount)
        std::swap(totalSampleCount, previousSampleCount);
    else
        previousSampleCount = totalSampleCount;

    const unsigned dataLength = totalSampleCount * 2; // kSampleSize (10) > 8

    std::vector<unsigned char> data(dataLength);
    int received = device->bulkReadMulti(data.data(), dataLength);
    if (received < 0) {
        setLastError("Getting sample data failed: " + libUsbErrorString(received));
        return {};
    }
    data.resize((size_t)received);
    return data;
}

CaptureFrame Dso5200Control::convertRawDataToSamples(const std::vector<unsigned char> &rawData) {
    CaptureFrame frame;
    frame.frameNumber = ++frameCounter;
    frame.timestampNs =
        (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    frame.samplerate = samplerateCurrent;

    if (rawData.size() < 2) return frame;
    const size_t totalSampleCount = rawData.size() / 2; // kSampleSize (10) > 8

    const unsigned extraBitsSize = kSampleSize - 8; // 2
    const unsigned short extraBitsMask = (0x00ff << extraBitsSize) & 0xff00;

    std::vector<std::vector<double>> data(kChannels);

    if (isFastRate()) {
        unsigned channel = 0;
        for (; channel < kChannels; ++channel)
            if (voltage[channel].used) break;
        if (channel >= kChannels) return frame;

        data[channel].resize(totalSampleCount);

        const unsigned gainID = voltage[channel].gain;
        const unsigned short limit = kVoltageLimit[gainID];
        const double offset = voltage[channel].offsetReal;
        const double gainStep = kGain[gainID].voltsPerDiv;

        unsigned bufferPosition = trigger.point * 2;
        for (unsigned pos = 0; pos < totalSampleCount; ++pos, ++bufferPosition) {
            if (bufferPosition >= totalSampleCount) bufferPosition %= totalSampleCount;

            const unsigned short low = rawData[bufferPosition];
            const unsigned extraBitsPosition = bufferPosition % kChannels;
            const unsigned shift = 8 - (kChannels - 1 - extraBitsPosition) * extraBitsSize;
            const unsigned short high =
                ((unsigned short)rawData[totalSampleCount + bufferPosition - extraBitsPosition] << shift) &
                extraBitsMask;

            data[channel][pos] = ((double)(low + high) / limit - offset) * gainStep;
        }
    } else {
        for (unsigned channel = 0; channel < kChannels; ++channel) {
            data[channel].resize(totalSampleCount / kChannels);

            const unsigned gainID = voltage[channel].gain;
            const unsigned short limit = kVoltageLimit[gainID];
            const double offset = voltage[channel].offsetReal;
            const double gainStep = kGain[gainID].voltsPerDiv;
            const unsigned extraBitsIndex = 8 - channel * 2;

            unsigned bufferPosition = trigger.point * 2;
            for (unsigned realPosition = 0; realPosition < data[channel].size();
                ++realPosition, bufferPosition += kChannels) {
                if (bufferPosition >= totalSampleCount) bufferPosition %= totalSampleCount;

                const unsigned short low = rawData[bufferPosition + kChannels - 1 - channel];
                const unsigned short high =
                    ((unsigned short)rawData[totalSampleCount + bufferPosition] << extraBitsIndex) & extraBitsMask;

                data[channel][realPosition] = ((double)(low + high) / limit - offset) * gainStep;
            }
        }
    }

    for (unsigned ch = 0; ch < kChannels; ++ch) {
        if (!voltage[ch].used || data[ch].empty()) continue;
        ChannelFrame cf;
        cf.channelId = (uint8_t)ch;
        cf.voltsPerDiv = (float)kGain[voltage[ch].gain].voltsPerDiv;
        cf.samples.reserve(data[ch].size());
        for (double v : data[ch]) cf.samples.push_back((float)v);
        frame.channels.push_back(std::move(cf));
    }

    return frame;
}

void Dso5200Control::threadMain() {
    while (running.load()) {
        int sleepMs = 100;

        if (sampling.load()) {
            std::lock_guard<std::mutex> lock(ioMutex);

            const int previousState = captureState;
            unsigned triggerPoint;
            std::tie(captureState, triggerPoint) = getCaptureState();
            trigger.point = triggerPoint; // raw value, no bit-decode (see the comment below retrieveChannelLevelData())

            if (debugLog && captureState == CAPTURE_READY5200) {
                std::cerr << "[dso5200] trigger.point=" << trigger.point << " (bufferPosition starts at "
                          << trigger.point * 2 << ")\n";
            }

            if (debugLog && captureState != previousState) {
                std::cerr << "[dso5200] captureState " << previousState << " -> " << captureState
                          << " (cycleCounter=" << cycleCounter << " startCycle=" << startCycle
                          << " cycleTimeMs=" << cycleTimeMs << ")\n";
            }

            if (captureState == (int)CAPTURE_ERROR) {
                setLastError("Getting capture state failed");
            } else {
                switch (captureState) {
                case CAPTURE_READY5200: {
                    std::vector<unsigned char> rawData = getSamples(expectedSampleCount);
                    if (debugLog)
                        std::cerr << "[dso5200] getSamples() -> " << rawData.size() << " bytes (expected "
                                  << expectedSampleCount << " samples, samplingStarted=" << samplingStarted << ")\n";
                    if (samplingStarted && !rawData.empty()) {
                        CaptureFrame frame = convertRawDataToSamples(rawData);
                        if (debugLog)
                            std::cerr << "[dso5200] frame #" << frame.frameNumber << " with "
                                      << frame.channels.size() << " channel(s)\n";
                        if (onFrame) onFrame(frame);
                    }
                    samplingStarted = false;

                    if (!sampling.load()) break;
                    [[fallthrough]];
                }
                case CAPTURE_WAITING:
                case CAPTURE_SAMPLING:
                    // Deliberate deviation from upstream OpenHantek: the original only runs this
                    // arm/force-trigger/restart logic while captureState == CAPTURE_WAITING, on the
                    // assumption the device keeps cycling back through WAITING if no trigger fires.
                    // Confirmed against real hardware (see docs/GUIA_ESTUDIO.md, Bloque 4/5): with no
                    // signal on the trigger source, the DSO-5200A instead gets stuck in
                    // CAPTURE_SAMPLING indefinitely once armed, and upstream's logic then never gets
                    // another chance to send FORCETRIGGER or to restart the capture - the tool would
                    // hang forever. Sharing this case with CAPTURE_WAITING lets the same
                    // arm/force/restart-on-timeout logic run regardless of which of the two states
                    // polling finds it in.
                    expectedSampleCount = getSampleCount();

                    if (samplingStarted) {
                        ++cycleCounter;

                        if (cycleCounter == startCycle) {
                            bulkCommand(BulkTriggerEnabled());
                        } else if (cycleCounter >= 8 + startCycle) {
                            // No trigger mode is exposed by this tool (always "auto"): force a
                            // trigger periodically so the display keeps updating even without a
                            // matching edge.
                            bulkCommand(BulkForceTrigger());
                        }

                        if (cycleCounter < 20 || cycleCounter < 4000 / cycleTimeMs) break;
                    }

                    // Either never started, or stuck for too long (timeout above) - (re)start.
                    bulkCommand(BulkCaptureStart());
                    samplingStarted = true;
                    cycleCounter = 0;
                    startCycle = (int)(trigger.position * 1000.0 / cycleTimeMs + 1.0);
                    break;

                default:
                    break;
                }
            }

            cycleTimeMs = std::clamp((int)((double)getRecordLength() / samplerateCurrent * 250), 10, 1000);
            sleepMs = cycleTimeMs;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }
}
