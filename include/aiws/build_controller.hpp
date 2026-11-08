#pragma once

#include "aiws/processing_core.hpp"

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <thread>

namespace aiws {

struct BuildOutcome {
    std::uint64_t workspace_version{};
    std::unique_ptr<ProcessingCore> corpus;
    std::string error;
    bool cancelled{false};
};

// Public functions run on the owning thread. The worker gets a private
// Workspace; only the atomic cancellation flag is shared mutable data.
class BuildController final {
public:
    using BuildTask = std::unique_ptr<ProcessingCore> (*)(
        const Workspace&, int, const std::atomic<bool>&);

    explicit BuildController(BuildTask task = nullptr);
    ~BuildController();
    BuildController(const BuildController&) = delete;
    BuildController& operator=(const BuildController&) = delete;

    bool start(Workspace snapshot, std::uint64_t version, int delay_ms);
    void request_cancel() noexcept;
    bool busy() const noexcept;
    bool ready() const;
    bool poll(BuildOutcome& outcome);

private:
    void run(Workspace snapshot, std::uint64_t version, int delay_ms,
             std::promise<BuildOutcome> promise);

    const BuildTask task_;
    std::atomic<bool> cancel_{false};
    std::thread worker_;
    std::future<BuildOutcome> result_;
};

}  // namespace aiws
