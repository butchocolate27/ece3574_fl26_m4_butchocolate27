#include "aiws/build_controller.hpp"
#include "document_build_step.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace aiws {

BuildController::BuildController(BuildTask task) : task_(task) {
    qRegisterMetaType<BuildOutcome>();
}

BuildController::~BuildController() {
    shutdown();
}

void BuildController::start(Workspace snapshot, std::uint64_t version, int delay_ms) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            throw std::logic_error("The build worker has stopped.");
        }
        if (!worker_.joinable()) {
            worker_ = std::thread(&BuildController::run, this);
        }
        cancel_locked();
        pending_ = Request{std::move(snapshot), version, std::max(0, delay_ms)};
    }
    changed_.notify_one();
}

void BuildController::cancel_locked() {
    pending_.reset();
#ifndef AIWS_FAULT_NO_CANCEL
    cancelled_.store(true);
#endif
}

void BuildController::request_cancel() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cancel_locked();
    }
    changed_.notify_one();
}

void BuildController::run() {
    for (;;) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            changed_.wait(lock, [this] {
#ifdef AIWS_FAULT_SILENT_SHUTDOWN
                return pending_.has_value();
#else
                return stopping_ || pending_.has_value();
#endif
            });
            if (stopping_) {
                return;
            }
            request = std::move(*pending_);
            pending_.reset();
            cancelled_.store(false);
        }

        emit started(request.version);
        auto outcome = build(request);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                return;
            }
        }
        emit finished(std::move(outcome));
    }
}

BuildOutcome BuildController::build(const Request& request) {
    BuildOutcome outcome;
    outcome.workspace_version = request.version;
    try {
        std::unique_ptr<ProcessingCore> corpus;
        if (task_) {
            corpus = task_(request.snapshot, request.delay_ms, cancelled_);
        } else {
            corpus = std::make_unique<ProcessingCore>();
            DocumentBuildStep step(request.delay_ms, cancelled_, mutex_, changed_);
            corpus->rebuild(request.snapshot);
        }
        if (cancelled_.load()) {
            throw BuildCancelled{};
        }
        if (!corpus) {
            throw std::runtime_error("Build produced no corpus.");
        }
        outcome.corpus = std::move(corpus);
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
    return outcome;
}

void BuildController::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        cancelled_.store(true);
        pending_.reset();
    }
    changed_.notify_one();
    if (worker_.joinable()) {
        worker_.join();
    }
}

}  // namespace aiws
