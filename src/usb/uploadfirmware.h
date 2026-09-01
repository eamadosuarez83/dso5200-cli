// SPDX-License-Identifier: GPL-2.0+
// Ported from openhantek/src/usb/uploadFirmware.h: Qt removed. Firmware .hex files
// are shipped as real files on disk (see firmware/) instead of Qt resources, so no
// temp-file extraction step is needed.

#pragma once

#include <string>

class USBDevice;

namespace UploadFirmware {

/// \param device The device that needs firmware (must currently be in the "no firmware" USB ID state).
/// \param firmwareDir Directory containing "<firmwareToken>-loader.hex" and "<firmwareToken>-firmware.hex".
/// \param firmwareToken Firmware file prefix, e.g. "dso5200x86" or "dso5200ax86".
bool startUpload(USBDevice *device, const std::string &firmwareDir, const std::string &firmwareToken,
                 std::string &errorMessage);

} // namespace UploadFirmware
