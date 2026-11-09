#include "aiws/build_controller.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace aiws {
namespace {

struct BuildCancelled {};

void check_cancelled(const std::atomic<bool>& cancel) {
    if (cancel.load()) {
        throw BuildCancelled{};
    }
}

std::unique_ptr<ProcessingCore> build_snapshot(
    const Workspace& workspace, int delay_ms, const std::atomic<bool>& cancel) {
    check_cancelled(cancel);
    // Use the same backend and default strategies as the M3 build.
    auto corpus = std::make_unique<ProcessingCore>();
    for (std::size_t i = 0; i < workspace.documents().size(); ++i) {
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(delay_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            check_cancelled(cancel);
            std::this_thread::sleep_until(std::min(
                deadline, std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(5)));
        }
    }
    check_cancelled(cancel);
    corpus->rebuild(workspace);
    check_cancelled(cancel);
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
