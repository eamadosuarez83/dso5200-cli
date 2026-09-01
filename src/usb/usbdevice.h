// SPDX-License-Identifier: GPL-2.0+
// Ported from openhantek/src/usb/usbdevice.h: Qt (QObject/QString) removed,
// libusb transfer logic kept identical.

#pragma once

#include <functional>
#include <libusb-1.0/libusb.h>
#include <string>

#include "usbdevicedefinitions.h"

typedef unsigned long UniqueUSBid;

/// \brief Returns string representation for libusb errors.
std::string libUsbErrorString(int error);

/// \brief Handles the USB communication with a device that has one in and one out endpoint.
class USBDevice {
  public:
    /// \param vendorID/productID USB IDs once firmware is loaded.
    /// \param name Human readable device name, used only for logging.
    USBDevice(libusb_device *device, long vendorID, long productID, std::string name, unsigned findIteration = 0);
    USBDevice(const USBDevice &) = delete;
    ~USBDevice();

    bool connectDevice(std::string &errorMessage);
    void disconnectFromDevice();

    /// \brief Check if the oscilloscope is connected.
    bool isConnected();

    /// \brief Return true if this device needs a firmware first.
    bool needsFirmware();

    inline void setFindIteration(unsigned iteration) { findIteration = iteration; }
    inline unsigned getFindIteration() const { return findIteration; }

    /// \brief Called when the device disconnects (USB unplug or fatal error).
    std::function<void()> onDisconnected;

    int bulkTransfer(unsigned char endpoint, const unsigned char *data, unsigned int length,
                     int attempts = HANTEK_ATTEMPTS, unsigned int timeout = HANTEK_TIMEOUT);

    inline int bulkWrite(const unsigned char *data, unsigned int length, int attempts = HANTEK_ATTEMPTS) {
        return bulkTransfer(HANTEK_EP_OUT, data, length, attempts);
    }

    template <class T> inline int bulkRead(const T *command, int attempts = HANTEK_ATTEMPTS) {
        return bulkTransfer(HANTEK_EP_IN, command->data(), (unsigned)command->size(), attempts);
    }

    int bulkReadMulti(unsigned char *data, unsigned length, int attempts = HANTEK_ATTEMPTS_MULTI);

    int controlTransfer(unsigned char type, unsigned char request, unsigned char *data, unsigned int length,
                        int value, int index, int attempts = HANTEK_ATTEMPTS);

    template <class T> inline int controlWrite(const T *command) {
        return controlTransfer(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT, (uint8_t)command->code,
                               (unsigned char *)command->data(), (unsigned)command->size(), command->value, 0,
                               HANTEK_ATTEMPTS);
    }

    template <class T> inline int controlRead(const T *command) {
        return controlTransfer(LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN, (uint8_t)command->code,
                               (unsigned char *)command->data(), (unsigned)command->size(), command->value, 0,
                               HANTEK_ATTEMPTS);
    }

    inline libusb_device *getRawDevice() const { return device; }
    inline unsigned long getUniqueUSBDeviceID() const { return uniqueUSBdeviceID; }
    static UniqueUSBid computeUSBdeviceID(libusb_device *device);

    inline const std::string &getName() const { return name; }
    inline void overwriteInPacketLength(int len) { inPacketLength = len; }

  protected:
    int claimInterface(const libusb_interface_descriptor *interfaceDescriptor, int endpointOut, int endPointIn);

    long vendorID;
    long productID;
    std::string name;

    struct libusb_device_descriptor descriptor;
    libusb_device *device;
    libusb_device_handle *handle = nullptr;
    unsigned findIteration;
    const unsigned long uniqueUSBdeviceID;
    int interface = -1;
    int outPacketLength = 0;
    int inPacketLength = 0;
};
