// SPDX-License-Identifier: GPL-2.0+
//
// Minimal localhost TCP server that broadcasts CaptureFrame data to any
// connected viewer (see viewer/dso_viewer.py and docs/PROTOCOL.md for the
// wire format). POSIX sockets only, no external dependencies.

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "device/capture_frame.h"

class SampleServer {
  public:
    explicit SampleServer(uint16_t port);
    ~SampleServer();

    /// Starts listening and accepting client connections in a background thread.
    /// \return false if the listening socket could not be created/bound.
    bool start(std::string &errorMessage);
    void stop();

    /// Sends a frame to every currently connected viewer. Safe to call from any thread.
    void broadcast(const CaptureFrame &frame);

    unsigned connectedClients() const;

  private:
    void acceptLoop();

    uint16_t port;
    int listenFd = -1;
    std::atomic<bool> running{false};
    std::thread acceptThread;

    mutable std::mutex clientsMutex;
    std::vector<int> clientFds;
};
