#pragma once

#include <atomic>
#include <future>
#include <string>
#include <thread>

class QObject;

namespace aiws {

class IpcServer final {
public:
    explicit IpcServer(QObject* receiver);
    ~IpcServer();
    IpcServer(const IpcServer&) = delete;
    IpcServer& operator=(const IpcServer&) = delete;

    bool start(const std::string& path, std::string& error);
    void request_stop();
    bool running() const;
    void poll_stop();

private:
    void run(std::promise<void> finished);
    void handle_client(int descriptor);

    QObject* const receiver_;  // The window outlives this server and its thread.
    std::string path_;
    int listener_{-1};
    std::atomic<bool> stop_{false};
    std::thread worker_;
    std::future<void> finished_;
};

}  // namespace aiws
