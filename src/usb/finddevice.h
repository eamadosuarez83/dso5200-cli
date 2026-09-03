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
    long productID;     ///< USB product ID (0x5200 or 0x520a) — identifies the variant in both states
    std::string firmwareToken; ///< "dso5200x86" or "dso5200ax86"
    std::string name;          ///< "DSO-5200" or "DSO-5200A"
};

/// Searches the USB bus for a DSO-5200/5200A, with or without firmware loaded.
///
/// Confirmed against real hardware: the product ID (0x5200 vs 0x520a) already
/// identifies the variant *before* firmware is loaded — only the vendor ID
/// changes (0x04b4 -> 0x04b5) once firmware is running. So, unlike an earlier
/// (incorrect) assumption in this codebase, the two variants do NOT share a
/// single ambiguous pre-firmware identity.
///
/// \param identity Filled with the matching device's identity (variant + firmware token).
/// \param needsFirmware Set to true if the device found still needs firmware upload.
/// \return The matching libusb_device (caller owns a reference, see finddevice.cpp), or nullptr if none found.
libusb_device *findDevice(libusb_context *context, ModelIdentity &identity, bool &needsFirmware);

} // namespace Dso5200
