// SPDX-License-Identifier: GPL-2.0+
#pragma once

#include <cstdint>
#include <vector>

/// One channel's worth of samples for a single capture, already converted to volts.
struct ChannelFrame {
    uint8_t channelId = 0; // 0 = CH1, 1 = CH2
    float voltsPerDiv = 1.0f;
    std::vector<float> samples;
};

/// A single capture from the oscilloscope (or the simulator), ready to stream to the viewer.
struct CaptureFrame {
    uint32_t frameNumber = 0;
    uint64_t timestampNs = 0;
    double samplerate = 0.0;
    std::vector<ChannelFrame> channels;
};
