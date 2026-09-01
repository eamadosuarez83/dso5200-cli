// SPDX-License-Identifier: GPL-2.0+
// Ported from openhantek/src/usb/uploadFirmware.cpp: Qt removed. The original
// extracted the firmware from Qt resources into a QTemporaryFile because
// ezusb_load_ram() needs a filesystem path; here the .hex files already live on
// disk (shipped in firmware/), so we pass their paths directly.

#include "uploadfirmware.h"

#include <libusb-1.0/libusb.h>

#include "ezusb.h"
#include "usbdevice.h"

namespace UploadFirmware {

bool startUpload(USBDevice *device, const std::string &firmwareDir, const std::string &firmwareToken,
                 std::string &errorMessage) {
    if (device->isConnected() || !device->needsFirmware()) return false;

    libusb_device_handle *handle;
    int errorCode = libusb_open(device->getRawDevice(), &handle);
    if (errorCode != LIBUSB_SUCCESS) {
        errorMessage = "Couldn't open device: " + libUsbErrorString(errorCode);
        return false;
    }

    const std::string loaderPath = firmwareDir + "/" + firmwareToken + "-loader.hex";
    const std::string firmwarePath = firmwareDir + "/" + firmwareToken + "-firmware.hex";

    libusb_set_auto_detach_kernel_driver(handle, 1);
    int status = libusb_claim_interface(handle, 0);
    if (status != LIBUSB_SUCCESS) {
        errorMessage = std::string("libusb_claim_interface() failed: ") + libusb_error_name(status);
        libusb_close(handle);
        return false;
    }

    status = ezusb_load_ram(handle, loaderPath.c_str(), FX_TYPE_FX2, 0);
    if (status != LIBUSB_SUCCESS) {
        errorMessage = std::string("Writing the loader firmware failed: ") + libusb_error_name(status);
        libusb_release_interface(handle, 0);
        libusb_close(handle);
        return false;
    }

    status = ezusb_load_ram(handle, firmwarePath.c_str(), FX_TYPE_FX2, 1);
    if (status != LIBUSB_SUCCESS) {
        errorMessage = std::string("Writing the main firmware failed: ") + libusb_error_name(status);
        libusb_release_interface(handle, 0);
        libusb_close(handle);
        return false;
    }

    libusb_release_interface(handle, 0);
    libusb_close(handle);

    return status == LIBUSB_SUCCESS;
}

} // namespace UploadFirmware
