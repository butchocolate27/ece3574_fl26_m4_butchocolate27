#pragma once

#include <atomic>
#include <chrono>
#include <string>

namespace aiws::ipc {

using Deadline = std::chrono::steady_clock::time_point;

// Owns one socket descriptor. A negative descriptor means no open socket.
class Socket {
public:
    explicit Socket(int descriptor = -1) : descriptor_(descriptor) {}
    ~Socket();
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    int get() const { return descriptor_; }

private:
    int descriptor_;
};

std::string default_socket_path();
bool set_nonblocking(int descriptor, std::string& error);
int connect_socket(const std::string& path, Deadline deadline, std::string& error);

// Each message is a four-byte big-endian length followed by that many bytes.
// The optional flag lets the receiver stop during a partial message.
bool send_message(int descriptor, const std::string& message, Deadline deadline,
                  const std::atomic<bool>* stop, std::string& error);
bool receive_message(int descriptor, std::string& message, Deadline deadline,
                     const std::atomic<bool>* stop, std::string& error);

}  // namespace aiws::ipc
