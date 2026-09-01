// SPDX-License-Identifier: GPL-2.0+

#include "simulator.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <sstream>

Simulator::Simulator() {
    running = true;
    worker = std::thread(&Simulator::threadMain, this);
}

Simulator::~Simulator() {
    running = false;
    if (worker.joinable()) worker.join();
}

void Simulator::start() { sampling = true; }
void Simulator::stop() { sampling = false; }
bool Simulator::isSampling() const { return sampling.load(); }

bool Simulator::setTimebase(double secondsPerScreenValue) {
    if (secondsPerScreenValue <= 0.0) return false;
    std::lock_guard<std::mutex> lock(mutex);
    secondsPerScreen = secondsPerScreenValue;
    return true;
}

bool Simulator::setVoltsPerDiv(int channel, double voltsPerDivValue) {
    if (channel < 0 || channel >= (int)kChannels || voltsPerDivValue <= 0.0) return false;
    std::lock_guard<std::mutex> lock(mutex);
    voltsPerDiv[channel] = voltsPerDivValue;
    return true;
}

bool Simulator::setChannelEnabled(int channel, bool enabled) {
    if (channel < 0 || channel >= (int)kChannels) return false;
    std::lock_guard<std::mutex> lock(mutex);
    channelEnabled[channel] = enabled;
    return true;
}

bool Simulator::setTriggerSlope(TriggerSlope slopeValue) {
    std::lock_guard<std::mutex> lock(mutex);
    slope = slopeValue;
    return true;
}

std::string Simulator::statusText() const {
    std::lock_guard<std::mutex> lock(mutex);
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(6);
    oss << "[SIMULATED] sampling=" << (sampling.load() ? "on" : "off") << " timebase=" << secondsPerScreen << "s"
        << " ch1=" << (channelEnabled[0] ? "on" : "off") << "@" << voltsPerDiv[0] << "V/div"
        << " ch2=" << (channelEnabled[1] ? "on" : "off") << "@" << voltsPerDiv[1] << "V/div"
        << " slope=" << (slope == TriggerSlope::Rising ? "rising" : "falling");
    return oss.str();
}

void Simulator::threadMain() {
    while (running.load()) {
        if (sampling.load()) {
            double screenSeconds, chVoltsPerDiv[kChannels];
            bool chEnabled[kChannels];
            TriggerSlope currentSlope;
            {
                std::lock_guard<std::mutex> lock(mutex);
                screenSeconds = secondsPerScreen;
                for (unsigned i = 0; i < kChannels; ++i) {
                    chVoltsPerDiv[i] = voltsPerDiv[i];
                    chEnabled[i] = channelEnabled[i];
                }
                currentSlope = slope;
            }

            const double samplerate = (double)kSamplesPerChannel / screenSeconds;
            const double cyclesAcrossScreen = 3.0; // keep a few visible periods regardless of timebase
            const double signalFreq = cyclesAcrossScreen / screenSeconds;
            const double slopeSign = currentSlope == TriggerSlope::Rising ? 1.0 : -1.0;

            CaptureFrame frame;
            frame.frameNumber = ++frameCounter;
            frame.timestampNs = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
            frame.samplerate = samplerate;

            for (unsigned ch = 0; ch < kChannels; ++ch) {
                if (!chEnabled[ch]) continue;
                ChannelFrame cf;
                cf.channelId = (uint8_t)ch;
                cf.voltsPerDiv = (float)chVoltsPerDiv[ch];
                cf.samples.resize(kSamplesPerChannel);

                // 3 divisions of amplitude, CH2 at double frequency and 90 degrees out of phase,
                // so the two channels are visibly distinguishable in the viewer.
                const double amplitude = 3.0 * chVoltsPerDiv[ch];
                const double freq = signalFreq * (ch == 0 ? 1.0 : 2.0);
                const double phase = ch == 0 ? 0.0 : M_PI / 2.0;

                for (unsigned i = 0; i < kSamplesPerChannel; ++i) {
                    const double t = (double)i / samplerate;
                    const double noise = ((double)std::rand() / RAND_MAX - 0.5) * 0.05 * amplitude;
                    cf.samples[i] =
                        (float)(slopeSign * amplitude * std::sin(2.0 * M_PI * freq * t + phase) + noise);
                }
                frame.channels.push_back(std::move(cf));
            }

            if (onFrame) onFrame(frame);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
