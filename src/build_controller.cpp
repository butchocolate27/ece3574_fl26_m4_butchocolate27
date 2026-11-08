#include "aiws/build_controller.hpp"

#include "aiws/chunker.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/retrieval_engine.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace aiws {
namespace {

struct BuildCancelled {};

class BuildChunker final : public ChunkingStrategy {
public:
    BuildChunker(int delay_ms, const std::atomic<bool>& cancel)
        : delay_ms_(delay_ms), cancel_(&cancel) {}

    void finish_build() noexcept {
        delay_ms_ = 0;
        cancel_ = nullptr;
    }

    std::vector<Chunk> chunk(const Document& document,
                             std::size_t document_order) const override {
        check_cancelled();
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(delay_ms_);
        while (std::chrono::steady_clock::now() < deadline) {
            check_cancelled();
            std::this_thread::sleep_until(std::min(
                deadline, std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(5)));
        }
        check_cancelled();
        auto chunks = chunker_.chunk(document, document_order);
        check_cancelled();
        return chunks;
    }

private:
    void check_cancelled() const {
        if (cancel_ && cancel_->load()) {
            throw BuildCancelled{};
        }
    }

    int delay_ms_;
    const std::atomic<bool>* cancel_;
    Chunker chunker_;
};

std::unique_ptr<ProcessingCore> build_snapshot(
    const Workspace& workspace, int delay_ms, const std::atomic<bool>& cancel) {
    auto chunker = std::make_unique<BuildChunker>(delay_ms, cancel);
    auto* chunker_observer = chunker.get();
    auto corpus = std::make_unique<ProcessingCore>(
        std::move(chunker),
        std::make_unique<RetrievalEngine>(),
        std::make_unique<ContextBuilder>());
    corpus->rebuild(workspace);
    chunker_observer->finish_build();
    return corpus;
}

}  // namespace

BuildController::BuildController(BuildTask task)
    : task_(task ? task : &build_snapshot) {}

BuildController::~BuildController() {
    request_cancel();
    // Normal window closing drains the ready result before destruction. This
    // also safely handles direct destruction by a caller outside the GUI flow.
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool BuildController::start(Workspace snapshot, std::uint64_t version, int delay_ms) {
    if (busy()) {
        return false;
    }
    cancel_.store(false);
    std::promise<BuildOutcome> promise;
    result_ = promise.get_future();
    worker_ = std::thread(&BuildController::run, this, std::move(snapshot),
                          version, std::max(0, delay_ms), std::move(promise));
    return true;
}

void BuildController::run(Workspace snapshot, std::uint64_t version, int delay_ms,
                          std::promise<BuildOutcome> promise) {
    BuildOutcome outcome;
    outcome.workspace_version = version;
    try {
        outcome.corpus = task_(snapshot, delay_ms, cancel_);
        if (!outcome.corpus) {
            throw std::runtime_error("Build produced no corpus.");
        }
    } catch (const BuildCancelled&) {
        outcome.cancelled = true;
    } catch (const std::exception& error) {
        outcome.error = error.what();
        if (outcome.error.empty()) {
            outcome.error = "Build failed without an error message.";
        }
    } catch (...) {
        outcome.error = "Unknown build failure.";
    }
    // The future becomes ready after the worker exits. The GUI can then join
    // it without waiting for an unfinished build.
    promise.set_value_at_thread_exit(std::move(outcome));
}

void BuildController::request_cancel() noexcept {
    cancel_.store(true);
}

bool BuildController::busy() const noexcept {
    return worker_.joinable();
}

bool BuildController::ready() const {
    return busy() && result_.valid()
        && result_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}

bool BuildController::poll(BuildOutcome& outcome) {
    if (!ready()) {
        return false;
    }
    outcome = result_.get();
#ifdef AIWS_FAULT_LOSE_FAILURE
    // Drop the completed failure and leave the application in Building.
    if (!outcome.error.empty()) {
        return false;
    }
#endif
    worker_.join();
    return true;
}

}  // namespace aiws
