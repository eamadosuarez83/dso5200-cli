// SPDX-License-Identifier: GPL-2.0+

#include "finddevice.h"

namespace Dso5200 {

// From openhantek/src/hantekdso/models/modelDSO5200.cpp
static constexpr long VID_FIRMWARE = 0x04b5;
static constexpr long VID_NO_FIRMWARE = 0x04b4;
static constexpr long PID_5200 = 0x5200;
static constexpr long PID_5200A = 0x520a;

libusb_device *findDevice(libusb_context *context, ModelIdentity &identity, bool &needsFirmware) {
    libusb_device **deviceList;
    ssize_t deviceCount = libusb_get_device_list(context, &deviceList);
    if (deviceCount < 0) return nullptr;

    libusb_device *found = nullptr;
    needsFirmware = false;

    for (ssize_t i = 0; i < deviceCount; ++i) {
        libusb_device *device = deviceList[i];
        struct libusb_device_descriptor descriptor;
        libusb_get_device_descriptor(device, &descriptor);

        const bool isModelA = descriptor.idProduct == PID_5200A;
        const bool isModel5200 = descriptor.idProduct == PID_5200;
        if (!isModelA && !isModel5200) continue;
        if (descriptor.idVendor != VID_FIRMWARE && descriptor.idVendor != VID_NO_FIRMWARE) continue;

        found = device;
        needsFirmware = descriptor.idVendor == VID_NO_FIRMWARE;
        identity = {VID_FIRMWARE, isModelA ? PID_5200A : PID_5200, isModelA ? "dso5200ax86" : "dso5200x86",
                   isModelA ? "DSO-5200A" : "DSO-5200"};

        // The product ID already tells us the variant in either state, so the first match is
        // conclusive - no need to keep scanning for a "better" one.
        break;
    }

    // Take our own reference before freeing the list (which drops the list's references
    // and would otherwise leave `found` dangling). Caller must libusb_unref_device(found)
    // once it no longer needs it (USBDevice's constructor takes its own separate reference).
    if (found) libusb_ref_device(found);
    libusb_free_device_list(deviceList, true);
    return found;
}

} // namespace Dso5200
