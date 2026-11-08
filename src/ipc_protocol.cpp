#include "aiws/ipc_protocol.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace aiws::ipc {
namespace {

constexpr std::uint32_t max_message_size = 1024 * 1024;

bool wait_ready(int descriptor, short events, Deadline deadline,
                const std::atomic<bool>* stop, std::string& error) {
    while (!stop || !stop->load()) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            error = "Communication timed out.";
            return false;
        }
        pollfd socket_poll{descriptor, events, 0};
        const int result = ::poll(&socket_poll, 1, static_cast<int>(
            std::min<long long>(remaining, 50)));
        if (result < 0 && errno != EINTR) {
            error = std::strerror(errno);
            return false;
        }
        if (result > 0) {
            if (socket_poll.revents & events) {
                return true;
            }
            error = "Connection closed.";
            return false;
        }
    }
    error = "Server is stopping.";
    return false;
}

bool read_exact(int descriptor, char* data, std::size_t size, Deadline deadline,
                const std::atomic<bool>* stop, std::string& error) {
    std::size_t offset = 0;
    while (offset < size) {
        if (!wait_ready(descriptor, POLLIN, deadline, stop, error)) {
            return false;
        }
        const auto count = ::recv(descriptor, data + offset, size - offset, 0);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count == 0) {
            error = "Connection closed before the message was complete.";
            return false;
        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            error = std::strerror(errno);
            return false;
        }
    }
    return true;
}

bool write_exact(int descriptor, const char* data, std::size_t size, Deadline deadline,
                 const std::atomic<bool>* stop, std::string& error) {
    std::size_t offset = 0;
    while (offset < size) {
        if (!wait_ready(descriptor, POLLOUT, deadline, stop, error)) {
            return false;
        }
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags = MSG_NOSIGNAL;
#endif
        const auto count = ::send(descriptor, data + offset, size - offset, flags);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
        } else if (count == 0) {
            error = "Connection closed.";
            return false;
        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            error = std::strerror(errno);
            return false;
        }
    }
    return true;
}

}  // namespace

Socket::~Socket() {
    if (descriptor_ >= 0) {
        ::close(descriptor_);
    }
}

std::string default_socket_path() {
    const char* user = std::getenv("USER");
    const std::string name = user && *user ? user : std::to_string(::getuid());
    return "/tmp/aiws-" + name + ".sock";
}

bool set_nonblocking(int descriptor, std::string& error) {
    const int flags = ::fcntl(descriptor, F_GETFL, 0);
    const int descriptor_flags = ::fcntl(descriptor, F_GETFD, 0);
    if (flags < 0 || descriptor_flags < 0
        || ::fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) < 0
        || ::fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
        error = std::strerror(errno);
        return false;
    }
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    ::setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
    return true;
}

int connect_socket(const std::string& path, Deadline deadline, std::string& error) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.empty() || path.size() >= sizeof(address.sun_path)
        || path.find('\0') != std::string::npos) {
        error = "Invalid socket path.";
        return -1;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    const int descriptor = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (descriptor < 0) {
        error = std::strerror(errno);
        return -1;
    }
    if (!set_nonblocking(descriptor, error)) {
        ::close(descriptor);
        return -1;
    }
    if (::connect(descriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        if ((errno != EINPROGRESS && errno != EAGAIN)
            || !wait_ready(descriptor, POLLOUT, deadline, nullptr, error)) {
            if (error.empty()) {
                error = std::strerror(errno);
            }
            ::close(descriptor);
            return -1;
        }
        int socket_error = 0;
        socklen_t length = sizeof(socket_error);
        if (::getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &socket_error, &length) < 0
            || socket_error != 0) {
            error = std::strerror(socket_error ? socket_error : errno);
            ::close(descriptor);
            return -1;
        }
    }
    return descriptor;
}

bool send_message(int descriptor, const std::string& message, Deadline deadline,
                  const std::atomic<bool>* stop, std::string& error) {
    if (message.size() > max_message_size) {
        error = "Message is too large.";
        return false;
    }
    const auto length = htonl(static_cast<std::uint32_t>(message.size()));
    return write_exact(descriptor, reinterpret_cast<const char*>(&length), sizeof(length),
                       deadline, stop, error)
        && write_exact(descriptor, message.data(), message.size(), deadline, stop, error);
}

bool receive_message(int descriptor, std::string& message, Deadline deadline,
                     const std::atomic<bool>* stop, std::string& error) {
    std::uint32_t length = 0;
    if (!read_exact(descriptor, reinterpret_cast<char*>(&length), sizeof(length),
                    deadline, stop, error)) {
        return false;
    }
    length = ntohl(length);
    if (length > max_message_size) {
        error = "Message is too large.";
        return false;
    }
    message.resize(length);
    return read_exact(descriptor, message.data(), message.size(), deadline, stop, error);
}

}  // namespace aiws::ipc
