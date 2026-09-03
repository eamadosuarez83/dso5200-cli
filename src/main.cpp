// SPDX-License-Identifier: GPL-2.0+
//
// dso5200basic - minimal Qt-free CLI tool for the Hantek DSO-5200/5200A.
// See README.md for usage.

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <libusb-1.0/libusb.h>
#include <memory>
#include <string>
#include <thread>

#include "cli/command_parser.h"
#include "device/dso5200.h"
#include "device/simulator.h"
#include "streaming/sample_server.h"
#include "usb/finddevice.h"
#include "usb/uploadfirmware.h"
#include "usb/usbdevice.h"

namespace {

struct Options {
    bool simulate = false;
    bool debug = false;
    uint16_t port = 5200;
    std::string firmwareDir;
};

std::string defaultFirmwareDir(const char *argv0) {
    std::filesystem::path exePath = std::filesystem::absolute(argv0);
    return (exePath.parent_path() / "firmware").string();
}

void printUsage(const char *argv0) {
    std::cout << "Uso: " << argv0 << " [opciones]\n"
              << "  --simulate         No usa hardware real, genera una senal sintetica\n"
              << "  --debug            Traza transiciones de captureState y conteo de muestras a stderr\n"
              << "  --port <n>         Puerto TCP para el visor (default 5200)\n"
              << "  --firmware-dir <d> Directorio con los .hex de firmware (default: junto al binario)\n"
              << "  --help             Muestra esta ayuda\n";
}

bool parseArgs(int argc, char **argv, Options &options) {
    options.firmwareDir = defaultFirmwareDir(argv[0]);
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--simulate") {
            options.simulate = true;
        } else if (arg == "--debug") {
            options.debug = true;
        } else if (arg == "--port" && i + 1 < argc) {
            options.port = (uint16_t)std::stoi(argv[++i]);
        } else if (arg == "--firmware-dir" && i + 1 < argc) {
            options.firmwareDir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        } else {
            std::cerr << "Opcion desconocida: " << arg << "\n";
            printUsage(argv[0]);
            std::exit(1);
        }
    }
    return true;
}

/// Finds and connects to a DSO-5200/5200A, uploading firmware and re-scanning if needed.
/// Returns nullptr (with a message on stderr) if no device could be reached.
std::unique_ptr<USBDevice> openRealDevice(libusb_context *ctx, const Options &options) {
    Dso5200::ModelIdentity identity;
    bool needsFirmware = false;
    libusb_device *raw = Dso5200::findDevice(ctx, identity, needsFirmware);
    if (!raw) {
        std::cerr << "No se encontro ningun DSO-5200/5200A conectado.\n";
        return nullptr;
    }

    if (needsFirmware) {
        std::cout << "Dispositivo sin firmware detectado, subiendo firmware " << identity.name << "...\n";
        USBDevice temp(raw, identity.vendorID, identity.productID, identity.name);
        std::string errorMessage;
        bool ok = UploadFirmware::startUpload(&temp, options.firmwareDir, identity.firmwareToken, errorMessage);
        libusb_unref_device(raw);
        if (!ok) {
            std::cerr << "Error subiendo firmware: " << errorMessage << "\n";
            return nullptr;
        }

        std::cout << "Firmware cargado, esperando a que el dispositivo se reconecte...\n";
        std::this_thread::sleep_for(std::chrono::seconds(2));

        raw = Dso5200::findDevice(ctx, identity, needsFirmware);
        if (!raw || needsFirmware) {
            if (raw) libusb_unref_device(raw);
            std::cerr << "El dispositivo no reaparecio con firmware tras la carga.\n";
            return nullptr;
        }
    }

    auto device = std::make_unique<USBDevice>(raw, identity.vendorID, identity.productID, identity.name);
    libusb_unref_device(raw);

    std::string errorMessage;
    if (!device->connectDevice(errorMessage)) {
        std::cerr << "No se pudo conectar al dispositivo: " << errorMessage << "\n";
        return nullptr;
    }

    std::cout << "Conectado a " << identity.name << ".\n";
    return device;
}

} // namespace

int main(int argc, char **argv) {
    Options options;
    parseArgs(argc, argv, options);

    std::unique_ptr<USBDevice> usbDevice;
    std::unique_ptr<IDsoSource> source;
    libusb_context *ctx = nullptr;

    if (options.simulate) {
        std::cout << "Modo simulado: no se usara hardware real.\n";
        source = std::make_unique<Simulator>();
    } else {
        if (libusb_init(&ctx) != 0) {
            std::cerr << "No se pudo inicializar libusb.\n";
            return 1;
        }
        usbDevice = openRealDevice(ctx, options);
        if (!usbDevice) {
            libusb_exit(ctx);
            return 1;
        }
        source = std::make_unique<Dso5200Control>(usbDevice.get(), options.debug);
    }

    SampleServer server(options.port);
    std::string serverError;
    if (!server.start(serverError)) {
        std::cerr << "No se pudo iniciar el servidor de streaming: " << serverError << "\n";
        return 1;
    }
    std::cout << "Streaming de muestras en 127.0.0.1:" << options.port << " (viewer/dso_viewer.py)\n";

    source->onFrame = [&server](const CaptureFrame &frame) { server.broadcast(frame); };

    Cli::printHelp();
    std::cout << "\n> " << std::flush;

    bool quit = false;
    std::string line;
    while (!quit && std::getline(std::cin, line)) {
        Cli::handleLine(line, *source, quit);
        if (!quit) std::cout << "> " << std::flush;
    }

    server.stop();
    source.reset();
    usbDevice.reset();
    if (ctx) libusb_exit(ctx);

    return 0;
}
