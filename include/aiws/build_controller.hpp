#pragma once

#include "aiws/processing_core.hpp"

#include <QObject>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace aiws {

struct BuildOutcome {
    std::uint64_t workspace_version{};
    std::shared_ptr<const ProcessingCore> corpus;
    std::string error;
    bool cancelled{false};
};

class BuildController final : public QObject {
    Q_OBJECT

public:
    using BuildTask = std::unique_ptr<ProcessingCore> (*)(
        const Workspace&, int, const std::atomic<bool>&);

    explicit BuildController(BuildTask task = nullptr);
    ~BuildController() override;

    void start(Workspace snapshot, std::uint64_t version, int delay_ms);
    void request_cancel();
    void shutdown();

signals:
    void started(quint64 version);
    void finished(aiws::BuildOutcome outcome);

private:
    struct Request {
        Workspace snapshot;
        std::uint64_t version;
        int delay_ms;
    };

    void run();
    BuildOutcome build(const Request& request);
    void cancel_locked();

    const BuildTask task_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::optional<Request> pending_;
    bool stopping_{false};
    std::atomic<bool> cancelled_{false};
    std::thread worker_;
};

}  // namespace aiws

Q_DECLARE_METATYPE(aiws::BuildOutcome)
