#include "aiws/ipc_server.hpp"
#include "aiws/ipc_protocol.hpp"

#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QObject>
#include <QString>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <exception>
#include <utility>

namespace aiws {

IpcServer::IpcServer(QObject* receiver) : receiver_(receiver) {}

IpcServer::~IpcServer() {
    request_stop();
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool IpcServer::start(const std::string& path, std::string& error) {
    if (running()) {
        error = "The IPC server is already running.";
        return false;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.empty() || path.size() >= sizeof(address.sun_path)
        || path.find('\0') != std::string::npos) {
        error = "Invalid socket path.";
        return false;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    listener_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener_ < 0) {
        error = std::strerror(errno);
        return false;
    }
    if (!ipc::set_nonblocking(listener_, error)) {
        ::close(listener_);
        listener_ = -1;
        return false;
    }
    // Do not remove an existing path: it could belong to another running app.
    if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        error = std::strerror(errno);
        ::close(listener_);
        listener_ = -1;
        return false;
    }
    if (::listen(listener_, 16) < 0) {
        error = std::strerror(errno);
        ::close(listener_);
        listener_ = -1;
        ::unlink(path.c_str());
        return false;
    }
    path_ = path;
    stop_.store(false);
    std::promise<void> finished;
    finished_ = finished.get_future();
    try {
        worker_ = std::thread(&IpcServer::run, this, std::move(finished));
    } catch (const std::exception& failure) {
        error = failure.what();
        ::close(listener_);
        listener_ = -1;
        ::unlink(path.c_str());
        return false;
    }
    return true;
}

void IpcServer::request_stop() {
    stop_.store(true);
}

bool IpcServer::running() const {
    return worker_.joinable();
}

void IpcServer::poll_stop() {
    if (running() && finished_.wait_for(std::chrono::milliseconds(0))
                         == std::future_status::ready) {
        finished_.get();
        worker_.join();
        listener_ = -1;
    }
}

void IpcServer::run(std::promise<void> finished) {
    while (!stop_.load()) {
        pollfd listener_poll{listener_, POLLIN, 0};
        const int result = ::poll(&listener_poll, 1, 50);
        if (stop_.load()) {
            break;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0 || (result > 0 && !(listener_poll.revents & POLLIN))) {
            break;
        }
        if (result == 0) {
            continue;
        }
        ipc::Socket client(::accept(listener_, nullptr, nullptr));
        if (client.get() >= 0) {
            handle_client(client.get());
        }
    }
    ::close(listener_);
    ::unlink(path_.c_str());
    finished.set_value_at_thread_exit();
}

void IpcServer::handle_client(int descriptor) {
    std::string error;
    if (!ipc::set_nonblocking(descriptor, error)) {
        return;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::string path_bytes;
    std::string reply;
    QByteArray contents;
    if (!ipc::receive_message(descriptor, path_bytes, deadline, &stop_, error)) {
        reply = "1" + error;
    } else if (path_bytes.empty() || path_bytes.front() != '/'
               || path_bytes.find('\0') != std::string::npos) {
        reply = "1An absolute file path is required.";
    } else {
        const QString path = QString::fromUtf8(path_bytes.data(), path_bytes.size());
        QFile file(path);
        if (!QFileInfo(path).isFile() || !file.open(QIODevice::ReadOnly)) {
            reply = "1Could not open file.";
        } else {
            while (!file.atEnd() && !stop_.load()) {
                contents.append(file.read(64 * 1024));
                if (file.error() != QFileDevice::NoError) {
                    break;
                }
            }
            if (stop_.load()) {
                return;
            }
            if (file.error() != QFileDevice::NoError) {
                reply = "1Could not read file.";
            } else {
                const QString text = QString::fromUtf8(contents);
                const bool queued = QMetaObject::invokeMethod(
                    receiver_, "add_document_text", Qt::QueuedConnection,
                    Q_ARG(QString, path), Q_ARG(QString, text));
                reply = queued ? "0" : "1Could not deliver the document.";
            }
        }
    }
    // This thread replies immediately; it never waits for the GUI slot to run.
    ipc::send_message(descriptor, reply, deadline, &stop_, error);
}

}  // namespace aiws
