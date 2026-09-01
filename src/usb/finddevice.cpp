// SPDX-License-Identifier: GPL-2.0+

#include "finddevice.h"

namespace Dso5200 {

// From openhantek/src/hantekdso/models/modelDSO5200.cpp
static constexpr long VID_FIRMWARE = 0x04b5;
static constexpr long PID_5200_FIRMWARE = 0x5200;
static constexpr long PID_5200A_FIRMWARE = 0x520a;
static constexpr long VID_NO_FIRMWARE = 0x04b4;
static constexpr long PID_NO_FIRMWARE = 0x5200; // shared by both variants before firmware upload

libusb_device *findDevice(libusb_context *context, bool preferModelA, ModelIdentity &identity, bool &needsFirmware) {
    libusb_device **deviceList;
    ssize_t deviceCount = libusb_get_device_list(context, &deviceList);
    if (deviceCount < 0) return nullptr;

    libusb_device *found = nullptr;
    needsFirmware = false;

    for (ssize_t i = 0; i < deviceCount; ++i) {
        libusb_device *device = deviceList[i];
        struct libusb_device_descriptor descriptor;
        libusb_get_device_descriptor(device, &descriptor);

        if (descriptor.idVendor == VID_FIRMWARE &&
            (descriptor.idProduct == PID_5200_FIRMWARE || descriptor.idProduct == PID_5200A_FIRMWARE)) {
            found = device;
            needsFirmware = false;
            bool isModelA = descriptor.idProduct == PID_5200A_FIRMWARE;
            identity = {VID_FIRMWARE, isModelA ? PID_5200A_FIRMWARE : PID_5200_FIRMWARE,
                       isModelA ? "dso5200ax86" : "dso5200x86", isModelA ? "DSO-5200A" : "DSO-5200"};
            break;
        }

        if (descriptor.idVendor == VID_NO_FIRMWARE && descriptor.idProduct == PID_NO_FIRMWARE) {
            found = device;
            needsFirmware = true;
            identity = {VID_FIRMWARE, preferModelA ? PID_5200A_FIRMWARE : PID_5200_FIRMWARE,
                       preferModelA ? "dso5200ax86" : "dso5200x86", preferModelA ? "DSO-5200A" : "DSO-5200"};
            // Keep scanning in case a firmware-loaded device also appears later in the list.
        }
    }

    // Take our own reference before freeing the list (which drops the list's references
    // and would otherwise leave `found` dangling). Caller must libusb_unref_device(found)
    // once it no longer needs it (USBDevice's constructor takes its own separate reference).
    if (found) libusb_ref_device(found);
    libusb_free_device_list(deviceList, true);
    return found;
}

} // namespace Dso5200
