// SPDX-License-Identifier: GPL-2.0+

#include "sample_server.h"

#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

// Appends a little-endian value to the buffer. All targets we care about (the
// dso5200basic host and the Python viewer) are little-endian x86_64, so no
// byte-swapping is performed; see docs/PROTOCOL.md.
template <class T> void appendLE(std::vector<uint8_t> &buffer, T value) {
    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&value);
    buffer.insert(buffer.end(), bytes, bytes + sizeof(T));
}

std::vector<uint8_t> serializeFrame(const CaptureFrame &frame) {
    std::vector<uint8_t> buffer;
    buffer.reserve(32 + frame.channels.size() * (16 + 2000 * 4));

    buffer.push_back('D');
    buffer.push_back('S');
    buffer.push_back('O');
    buffer.push_back('6');
    appendLE(buffer, frame.frameNumber);
    appendLE(buffer, frame.timestampNs);
    appendLE(buffer, frame.samplerate);
    buffer.push_back((uint8_t)frame.channels.size());

    for (const ChannelFrame &channel : frame.channels) {
        buffer.push_back(channel.channelId);
        appendLE(buffer, channel.voltsPerDiv);
        appendLE(buffer, (uint32_t)channel.samples.size());
        appendLE(buffer, channel.clippedSamples);
        appendLE(buffer, channel.adcMinVolts);
        appendLE(buffer, channel.adcMaxVolts);
        for (float sample : channel.samples) appendLE(buffer, sample);
    }

    return buffer;
}

bool writeAll(int fd, const uint8_t *data, size_t length) {
    size_t sent = 0;
    while (sent < length) {
        ssize_t n = send(fd, data + sent, length - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

} // namespace

SampleServer::SampleServer(uint16_t port) : port(port) {}

SampleServer::~SampleServer() { stop(); }

bool SampleServer::start(std::string &errorMessage) {
    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        errorMessage = "socket() failed";
        return false;
    }

    int reuse = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    if (bind(listenFd, (sockaddr *)&address, sizeof(address)) < 0) {
        errorMessage = "bind() to 127.0.0.1:" + std::to_string(port) + " failed (already in use?)";
        close(listenFd);
        listenFd = -1;
        return false;
    }

    if (listen(listenFd, 4) < 0) {
        errorMessage = "listen() failed";
        close(listenFd);
        listenFd = -1;
        return false;
    }

    running = true;
    acceptThread = std::thread(&SampleServer::acceptLoop, this);
    return true;
}

void SampleServer::stop() {
    if (!running.load()) return;
    running = false;
    if (listenFd >= 0) {
        shutdown(listenFd, SHUT_RDWR);
        close(listenFd);
        listenFd = -1;
    }
    if (acceptThread.joinable()) acceptThread.join();

    std::lock_guard<std::mutex> lock(clientsMutex);
    for (int fd : clientFds) close(fd);
    clientFds.clear();
}

void SampleServer::acceptLoop() {
    while (running.load()) {
        sockaddr_in clientAddress{};
        socklen_t clientLen = sizeof(clientAddress);
        int clientFd = accept(listenFd, (sockaddr *)&clientAddress, &clientLen);
        if (clientFd < 0) {
            if (!running.load()) break;
            continue;
        }
        int noDelay = 1;
        setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));

        std::lock_guard<std::mutex> lock(clientsMutex);
        clientFds.push_back(clientFd);
    }
}

void SampleServer::broadcast(const CaptureFrame &frame) {
    std::vector<uint8_t> buffer = serializeFrame(frame);

    std::lock_guard<std::mutex> lock(clientsMutex);
    for (auto it = clientFds.begin(); it != clientFds.end();) {
        if (writeAll(*it, buffer.data(), buffer.size())) {
            ++it;
        } else {
            close(*it);
            it = clientFds.erase(it);
        }
    }
}

unsigned SampleServer::connectedClients() const {
    std::lock_guard<std::mutex> lock(clientsMutex);
    return (unsigned)clientFds.size();
}
