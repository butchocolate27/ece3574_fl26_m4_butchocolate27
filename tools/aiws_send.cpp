#include "aiws/ipc_protocol.hpp"

#include <QFileInfo>
#include <QString>

#include <chrono>
#include <cstdio>
#include <string>

int main(int argc, char* argv[]) {
    std::string socket_path = aiws::ipc::default_socket_path();
    int argument = 1;
    if (argument < argc && std::string(argv[argument]) == "--socket") {
        if (argument + 1 >= argc) {
            std::fprintf(stderr, "--socket requires a path.\n");
            return 1;
        }
        socket_path = argv[argument + 1];
        argument += 2;
    }
    if (argc - argument != 2 || std::string(argv[argument]) != "add") {
        std::fprintf(stderr, "Usage: aiws_send [--socket PATH] add FILE\n");
        return 1;
    }
    const QString file = QFileInfo(QString::fromUtf8(argv[argument + 1])).absoluteFilePath();
    std::string error;
    aiws::ipc::Socket socket(aiws::ipc::connect_socket(
        socket_path, std::chrono::steady_clock::now() + std::chrono::seconds(2), error));
    if (socket.get() < 0) {
        std::fprintf(stderr, "Could not reach AI Workspace: %s\n", error.c_str());
        return 2;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::string reply;
    if (!aiws::ipc::send_message(socket.get(), file.toStdString(), deadline, nullptr, error)
        || !aiws::ipc::receive_message(socket.get(), reply, deadline, nullptr, error)) {
        std::fprintf(stderr, "Communication failed: %s\n", error.c_str());
        return 2;
    }
    if (reply == "0") {
        return 0;
    }
    if (!reply.empty() && reply.front() == '1') {
        std::fprintf(stderr, "%s\n", reply.substr(1).c_str());
        return 1;
    }
    std::fprintf(stderr, "Invalid reply from AI Workspace.\n");
    return 2;
}
