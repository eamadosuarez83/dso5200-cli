// SPDX-License-Identifier: GPL-2.0+
#pragma once

#include <cstdint>
#include <limits>
#include <vector>

/// One channel's worth of samples for a single capture, already converted to volts.
struct ChannelFrame {
    uint8_t channelId = 0; // 0 = CH1, 1 = CH2
    float voltsPerDiv = 1.0f;
    // Muestras que tocaron el tope del ADC (cuenta 0 o 1023): su valor en volts no es real.
    uint32_t clippedSamples = 0;
    // Volts que corresponden a la cuenta 0 y a la 1023 del ADC con la configuración actual.
    // NaN si la fuente no los conoce (simulador).
    float adcMinVolts = std::numeric_limits<float>::quiet_NaN();
    float adcMaxVolts = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> samples;
};

/// A single capture from the oscilloscope (or the simulator), ready to stream to the viewer.
struct CaptureFrame {
    uint32_t frameNumber = 0;
    uint64_t timestampNs = 0;
    double samplerate = 0.0;
    std::vector<ChannelFrame> channels;
};
