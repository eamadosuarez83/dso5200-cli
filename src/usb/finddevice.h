// SPDX-License-Identifier: GPL-2.0+
// Minimal single-model device discovery, replacing OpenHantek's generic
// FindDevices/ModelRegistry machinery (not needed when only one model is supported).

#pragma once

#include <libusb-1.0/libusb.h>
#include <string>

namespace Dso5200 {

/// Which DSO-5200 variant was found and its firmware naming.
struct ModelIdentity {
    long vendorID;      ///< USB vendor ID once firmware is loaded (0x04b5 for both variants)
    long productID;     ///< USB product ID once firmware is loaded (0x5200 or 0x520a)
    std::string firmwareToken; ///< "dso5200x86" or "dso5200ax86"
    std::string name;          ///< "DSO-5200" or "DSO-5200A"
};

/// Searches the USB bus for a DSO-5200/5200A, with or without firmware loaded.
/// \param preferModelA If a device without firmware is found (both variants share the
/// same pre-firmware VID/PID and can't be told apart), pick the 5200A firmware set
/// when true, the plain 5200 firmware set otherwise.
/// \param identity Filled with the (potential) post-firmware identity.
/// \param needsFirmware Set to true if the device found still needs firmware upload.
/// \return The matching libusb_device (caller does not own a new reference), or nullptr if none found.
libusb_device *findDevice(libusb_context *context, bool preferModelA, ModelIdentity &identity, bool &needsFirmware);

} // namespace Dso5200
