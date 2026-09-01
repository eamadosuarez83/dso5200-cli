// SPDX-License-Identifier: GPL-2.0+
// Ported from openhantek/src/usb/usbdevice.cpp: Qt removed, libusb calls kept identical.

#include "usbdevice.h"

#include <algorithm>

std::string libUsbErrorString(int error) {
    switch (error) {
    case LIBUSB_SUCCESS: return "Success (no error)";
    case LIBUSB_ERROR_IO: return "Input/output error";
    case LIBUSB_ERROR_INVALID_PARAM: return "Invalid parameter";
    case LIBUSB_ERROR_ACCESS: return "Access denied (insufficient permissions)";
    case LIBUSB_ERROR_NO_DEVICE: return "No such device (it may have been disconnected)";
    case LIBUSB_ERROR_NOT_FOUND: return "Entity not found";
    case LIBUSB_ERROR_BUSY: return "Resource busy";
    case LIBUSB_ERROR_TIMEOUT: return "Operation timed out";
    case LIBUSB_ERROR_OVERFLOW: return "Overflow";
    case LIBUSB_ERROR_PIPE: return "Pipe error";
    case LIBUSB_ERROR_INTERRUPTED: return "System call interrupted (perhaps due to signal)";
    case LIBUSB_ERROR_NO_MEM: return "Insufficient memory";
    case LIBUSB_ERROR_NOT_SUPPORTED: return "Operation not supported or unimplemented on this platform";
    default: return "Other error";
    }
}

UniqueUSBid USBDevice::computeUSBdeviceID(libusb_device *device) {
    UniqueUSBid v = 0;
    libusb_get_port_numbers(device, (uint8_t *)&v, sizeof(v));
    return v;
}

USBDevice::USBDevice(libusb_device *device, long vendorID, long productID, std::string name, unsigned findIteration)
    : vendorID(vendorID), productID(productID), name(std::move(name)), device(device), findIteration(findIteration),
      uniqueUSBdeviceID(computeUSBdeviceID(device)) {
    libusb_ref_device(device);
    libusb_get_device_descriptor(device, &descriptor);
}

bool USBDevice::connectDevice(std::string &errorMessage) {
    if (needsFirmware()) return false;
    if (isConnected()) return true;

    int errorCode = libusb_open(device, &handle);
    if (errorCode != LIBUSB_SUCCESS) {
        handle = nullptr;
        errorMessage = "Couldn't open device: " + libUsbErrorString(errorCode);
        return false;
    }

    errorCode = LIBUSB_ERROR_NOT_FOUND;
    libusb_config_descriptor *configDescriptor;
    libusb_get_config_descriptor(device, 0, &configDescriptor);
    for (int interfaceIndex = 0; interfaceIndex < (int)configDescriptor->bNumInterfaces; ++interfaceIndex) {
        const libusb_interface *iface = &configDescriptor->interface[interfaceIndex];
        if (iface->num_altsetting < 1) continue;

        const libusb_interface_descriptor *interfaceDescriptor = &iface->altsetting[0];
        if (interfaceDescriptor->bInterfaceClass == LIBUSB_CLASS_VENDOR_SPEC &&
            interfaceDescriptor->bInterfaceSubClass == 0 && interfaceDescriptor->bInterfaceProtocol == 0 &&
            interfaceDescriptor->bNumEndpoints == 2) {
            errorCode = claimInterface(interfaceDescriptor, HANTEK_EP_OUT, HANTEK_EP_IN);
            break;
        }
    }

    libusb_free_config_descriptor(configDescriptor);

    if (errorCode != LIBUSB_SUCCESS) {
        errorMessage = libUsbErrorString(errorCode) + " (" + std::to_string(libusb_get_bus_number(device)) + ":" +
                       std::to_string(libusb_get_device_address(device)) + ")";
        return false;
    }

    return true;
}

USBDevice::~USBDevice() {
    disconnectFromDevice();
#if defined(_WIN32) || defined(_WIN64)
    if (device != nullptr) libusb_unref_device(device);
    device = nullptr;
#endif
}

int USBDevice::claimInterface(const libusb_interface_descriptor *interfaceDescriptor, int endpointOut,
                              int endPointIn) {
    int errorCode = libusb_claim_interface(this->handle, interfaceDescriptor->bInterfaceNumber);
    if (errorCode < 0) { return errorCode; }

    interface = interfaceDescriptor->bInterfaceNumber;

    this->outPacketLength = 0;
    this->inPacketLength = 0;
    for (int endpoint = 0; endpoint < interfaceDescriptor->bNumEndpoints; ++endpoint) {
        const libusb_endpoint_descriptor *endpointDescriptor = &(interfaceDescriptor->endpoint[endpoint]);
        if (endpointDescriptor->bEndpointAddress == endpointOut) {
            this->outPacketLength = endpointDescriptor->wMaxPacketSize;
        } else if (endpointDescriptor->bEndpointAddress == endPointIn) {
            this->inPacketLength = endpointDescriptor->wMaxPacketSize;
        }
    }
    return LIBUSB_SUCCESS;
}

void USBDevice::disconnectFromDevice() {
    if (!device) return;

    if (this->handle) {
        if (this->interface != -1) libusb_release_interface(this->handle, this->interface);
        this->interface = -1;
        libusb_close(this->handle);
    }
    this->handle = nullptr;

#if !defined(_WIN32) || !defined(_WIN64)
    libusb_unref_device(device);
#endif

    if (onDisconnected) onDisconnected();
}

bool USBDevice::isConnected() { return this->handle != 0; }

bool USBDevice::needsFirmware() {
    return this->descriptor.idProduct != productID || this->descriptor.idVendor != vendorID;
}

int USBDevice::bulkTransfer(unsigned char endpoint, const unsigned char *data, unsigned int length, int attempts,
                            unsigned int timeout) {
    if (!this->handle) return LIBUSB_ERROR_NO_DEVICE;

    int errorCode = LIBUSB_ERROR_TIMEOUT;
    int transferred = 0;
    for (int attempt = 0; (attempt < attempts || attempts == -1) && errorCode == LIBUSB_ERROR_TIMEOUT; ++attempt)
        errorCode =
            libusb_bulk_transfer(this->handle, endpoint, (unsigned char *)data, (int)length, &transferred, timeout);

    if (errorCode == LIBUSB_ERROR_NO_DEVICE) disconnectFromDevice();
    if (errorCode < 0)
        return errorCode;
    else
        return transferred;
}

int USBDevice::bulkReadMulti(unsigned char *data, unsigned length, int attempts) {
    if (!this->handle) return LIBUSB_ERROR_NO_DEVICE;

    int errorCode = this->inPacketLength;
    unsigned int packet, received = 0;
    for (packet = 0; received < length && errorCode == this->inPacketLength; ++packet) {
        errorCode = this->bulkTransfer(HANTEK_EP_IN, data + packet * this->inPacketLength,
                                       std::min(length - received, (unsigned int)this->inPacketLength), attempts,
                                       HANTEK_TIMEOUT_MULTI);
        if (errorCode > 0) received += (unsigned)errorCode;
    }

    if (received > 0)
        return (int)received;
    else
        return errorCode;
}

int USBDevice::controlTransfer(unsigned char type, unsigned char request, unsigned char *data, unsigned int length,
                               int value, int index, int attempts) {
    if (!this->handle) return LIBUSB_ERROR_NO_DEVICE;

    int errorCode = LIBUSB_ERROR_TIMEOUT;
    for (int attempt = 0; (attempt < attempts || attempts == -1) && errorCode == LIBUSB_ERROR_TIMEOUT; ++attempt)
        errorCode = libusb_control_transfer(this->handle, type, request, value, index, data, length, HANTEK_TIMEOUT);

    if (errorCode == LIBUSB_ERROR_NO_DEVICE) disconnectFromDevice();
    return errorCode;
}
