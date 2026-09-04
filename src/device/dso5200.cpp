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
//
// A second, bigger deviation found later by testing against real hardware: even with the above
// fix, changing `timebase` had NO effect on the real captured window - verified empirically
// (100us and 40ms requests produced byte-for-byte identical raw samples). The root cause was
// OpenHantek's own continuously-computed SamplerateFast/SamplerateSlow divider - ported faithfully
// from upstream - being silently ignored by the DSO-5200 for anything but a few coincidental
// values. The DSO-5200 only understands a fixed table of "magic" 16-bit SamplerateSlow constants;
// see kSlowTimebaseTable and doSetRecordTime()/applyTimebaseTableEntry() below for the fix and the
// two independent, hardware-tested sources (sigrok, github.com/Blokkendoos/HantekDSO) that
// corroborate the exact values.

#include "dso5200.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>

#include "protocol/states.h"
#include "usb/usbdevice.h"

using namespace Hantek;

namespace {
struct TimebaseEntry {
    uint8_t fast;
    uint16_t slow;
};

// Register values for CSETTRIGGERORSAMPLERATE (BulkSetSamplerate5200, opcode 0x0c) covering the
// DSO-5200's ~246us..1.6s timebase steps, small-buffer (10240-sample) variant. This replaces a
// continuously-computed divider that was found (empirically, against real hardware) to have NO
// effect on the actual captured window: requesting duration=100us and duration=40ms produced
// byte-for-byte identical raw samples. These exact values were cross-checked against two
// independent, hardware-tested reverse-engineered drivers - sigrok's hantek-dso and
// github.com/Blokkendoos/HantekDSO - which report byte-identical tables, confirming the DSO-5200
// only understands this fixed set of "magic" constants rather than an arbitrary divider.
//
// Also confirmed while investigating this: OpenHantek's own protocol docs (bulkcode.h) say the
// *combined* SETTRIGGERANDSAMPLERATE bulk command (opcode 0x01, used by HantekDSO's
// dsoSetTriggerAndSampleRate()) is only valid for MODEL_DSO2090/MODEL_DSO2150 - the DSO-5200/5200A
// need the *split* CSETTRIGGERORSAMPLERATE (0x0c, this table) + ESETTRIGGERORSAMPLERATE (0x0e,
// triggerCommand) pair instead, which is what this code already sent. So the fix here is only
// the register *values*, not the command framing.
constexpr TimebaseEntry kSlowTimebaseTable[] = {
    {3, 0xFFFF}, {4, 0xFFFC}, {4, 0xFFF7}, {4, 0xFFE8}, {4, 0xFFCE}, {4, 0xFF9C},
    {4, 0xFF07}, {4, 0xFE0D}, {4, 0xFC19}, {4, 0xF63D}, {4, 0xEC79}, {4, 0xD8F1},
};
constexpr unsigned kSlowTimebaseTableSize = sizeof(kSlowTimebaseTable) / sizeof(kSlowTimebaseTable[0]);
}  // namespace

// Real bug found testing against hardware, root-caused via openhantek's own git history
// (OpenHantek6022 checkout, commit b935f4e "Mostly DSO-5200 bugfixes, thanks to Ash", 2010): this
// port's .index (scalerId, the value written to BulkSetGain) and kVoltageLimit (sampleRange, the
// decode divisor) columns had drifted from that commit's DSO-5200-specific ("commandVersion==1")
// mapping, which is *not* a simple repeating pattern - it's `gainId % 3` (1->368, 2->454,
// default->908) with a special-cased exception at gainId==0 (index=1, limit=184) - somewhere
// between that 2010 fix and the later modelDSO5200.cpp this port's kGain[].index/kVoltageLimit
// were derived from, the model-specific table seems to have been lost/regressed back toward the
// generic (8-bit-model) `gainId % 3` formula. Symptom: gainID 3 read a scaled-wrong-but-nonzero
// voltage and gainID 6 read a hard, permanent flat 0V - both now match the values below.
const Dso5200Control::GainStep Dso5200Control::kGain[9] = {
    {1, 0.16}, {1, 0.40}, {0, 0.80}, {0, 1.60}, {1, 4.00}, {0, 8.00}, {0, 16.0}, {1, 40.0}, {0, 80.0},
};

const unsigned short Dso5200Control::kVoltageLimit[9] = {184, 368, 454, 908, 368, 454, 908, 368, 454};

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

    if (debugLog) {
        std::cerr << "[dso5200] updateSamplerate(downsampler=" << downsamplerValue << ", fastRate=" << fastRate
                   << "): valueSlow=0x" << std::hex << (valueSlow == 0 ? 0 : (uint16_t)(0xffff - valueSlow))
                   << " valueFast=0x" << (int)(4 - valueFast) << std::dec
                   << " samplerateCurrent=" << samplerateCurrent << "\n";
    }

    doSetPretriggerPosition(trigger.position);
    return true;
}

bool Dso5200Control::applyTimebaseTableEntry(unsigned index, double *windowOut) {
    const TimebaseEntry &entry = kSlowTimebaseTable[index];
    const unsigned divider = 2u * (0x10000u - entry.slow) + 4u - entry.fast;
    const double sr = kSingleMax / divider;

    samplerateCommand.setSamplerateFast(entry.fast);
    samplerateCommand.setSamplerateSlow(entry.slow);
    triggerCommand.setFastRate(false);

    if (debugLog) {
        std::cerr << "[dso5200] applyTimebaseTableEntry(index=" << index << "): slow=0x" << std::hex
                   << entry.slow << " fast=0x" << (int)entry.fast << std::dec << " divider=" << divider
                   << " sr=" << sr << "\n";
    }

    bulkCommand(samplerateCommand);
    bulkCommand(triggerCommand);

    fastRateActive = false;
    downsampler = divider;
    samplerateCurrent = sr;
    if (windowOut) *windowOut = (double)kRecordLengthSingle / sr;

    doSetPretriggerPosition(trigger.position);
    return true;
}

bool Dso5200Control::doSetRecordTime(double duration) {
    if (duration <= 0.0) return false;

    unsigned bestIndex = 0;
    double bestWindow = 0.0, bestError = 1e300;
    for (unsigned i = 0; i < kSlowTimebaseTableSize; ++i) {
        const TimebaseEntry &entry = kSlowTimebaseTable[i];
        const unsigned divider = 2u * (0x10000u - entry.slow) + 4u - entry.fast;
        const double window = (double)kRecordLengthSingle * divider / kSingleMax;
        const double error = std::abs(std::log(window / duration));
        if (error < bestError) {
            bestError = error;
            bestIndex = i;
            bestWindow = window;
        }
    }

    // The table's fastest entry (index 0) tops out around ~246us of real window in normal
    // (non-fast-rate) mode. Shorter requests on a single active channel can still be served by
    // fast-rate mode (kMultiBase/kMultiMax, up to 250 MS/s single-channel) - untouched below.
    if (bestIndex == 0 && duration < bestWindow && usedChannels <= 1) {
        const double targetSamplerate = (double)kRecordLengthSingle / duration;
        unsigned downsamplerValue = 0;
        getBestSamplerate(targetSamplerate, true, false, &downsamplerValue);
        return updateSamplerate(downsamplerValue, true);
    }

    return applyTimebaseTableEntry(bestIndex, nullptr);
}

bool Dso5200Control::doSetGain(unsigned channel, double gain) {
    // Find the lowest gain step that's at least as high as the requested V/div.
    unsigned gainID;
    for (gainID = 0; gainID < 8; ++gainID)
        if (kGain[gainID].voltsPerDiv >= gain) break;

    gainCommand.setGain(channel, kGain[gainID].index);

    // Real bug found testing against hardware: any gain change that also flipped the below1V/
    // below100mV attenuator relay (e.g. requesting 1.6V/div or 16V/div, which round up from the
    // previous step and cross a relay boundary) left the channel reading a flat 0V, permanently
    // until a *different* gain step was requested - confirmed NOT a settling-time issue (still
    // flat after 6+ seconds) and NOT a command-ordering issue (reordering offset/relay changed
    // nothing). Root cause found in openhantek's own git history (OpenHantek6022 checkout,
    // commit b935f4e "Mostly DSO-5200 bugfixes, thanks to Ash", 2010): the DSO-5200/5200A are
    // "10 bit models" whose relay thresholds must use <= instead of < (the commit's own
    // CONTROL_SETRELAYS doc comment: "The limits are <= instead of < for the 10 bit models,
    // since those support voltages up to 10 V"), with the comparison boundary at GAIN_1V/
    // GAIN_100MV (indices 6 and 3 in that commit's 9-step Gain enum, the same 9-step table this
    // port's kGain[] uses) rather than at the plain gainID<3/gainID<6 used by the *generic*
    // (8-bit-model) formula this port had copied - which switches each relay one gain step too
    // early, exactly at gainID 3 and 6, exactly where the flat-0V readings were observed.
    relaysCommand.setBelow1V(channel, gainID <= 6);
    relaysCommand.setBelow100mV(channel, gainID <= 3);

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
    const unsigned recordLength = getRecordLength();

    // Real bug found testing against hardware: this used `position * samplerateCurrent`, treating
    // `position` as if it were already a time offset in seconds - but every caller in this port
    // passes the raw 0..1 screen-fraction (see trigger.position's declaration), never converted to
    // seconds. Upstream OpenHantek's GUI does that conversion itself before calling the equivalent
    // function (main.cpp: `position * horizontal.timebase * DIVS_TIME`), which is dimensionally
    // just `position * recordLength` once you cancel out timebase*DIVS_TIME/samplerate = recordLength
    // - so multiplying by recordLength directly is the correct, simpler equivalent for a 0..1
    // fraction input. With the old formula, the default position=0.0 happened to still produce
    // *a* value (0 either way) - which is why this was never caught - but it made TriggerPositionPre/
    // Post (0xd7ff/0xffff) diverge from what the real DSO-5200A software actually sends for its own
    // default, centered trigger position (0xebff/0xebff, confirmed byte-for-byte via USB capture of
    // the official Windows software - see docs/GUIA_ESTUDIO.md). Symptom: capture buffers reading
    // as one tiny transition edge plus stale/flat filler for the rest of the record.
    double positionSamples = position * recordLength;
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

    if (debugLog && !data.empty()) {
        const size_t halfLen = data.size() / 2;
        for (size_t parity = 0; parity < 2; ++parity) {
            unsigned char loMin = 255, loMax = 0;
            std::map<unsigned char, int> hist;
            for (size_t i = parity; i < halfLen; i += 2) {
                loMin = std::min(loMin, data[i]);
                loMax = std::max(loMax, data[i]);
                ++hist[data[i]];
            }
            std::vector<std::pair<unsigned char, int>> sorted(hist.begin(), hist.end());
            std::sort(sorted.begin(), sorted.end(), [](auto &a, auto &b) { return a.second > b.second; });
            std::cerr << "[dso5200] rawData parity=" << parity << ": min=" << (int)loMin << " max=" << (int)loMax
                      << " uniqueVals=" << hist.size() << " top8=[";
            for (size_t i = 0; i < sorted.size() && i < 8; ++i)
                std::cerr << "(" << (int)sorted[i].first << ":" << sorted[i].second << ")";
            std::cerr << "]\n";

            if (parity == 1) {
                const unsigned char threshold = (unsigned char)(loMin + (loMax - loMin) * 0.6);
                std::cerr << "[dso5200] parity=1 low-value positions (<" << (int)threshold << "): [";
                int printed = 0;
                for (size_t i = 1; i < halfLen && printed < 30; i += 2) {
                    if (data[i] < threshold) {
                        std::cerr << (i / 2) << ":" << (int)data[i] << " ";
                        ++printed;
                    }
                }
                std::cerr << "]\n";
            }
        }
    }

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
