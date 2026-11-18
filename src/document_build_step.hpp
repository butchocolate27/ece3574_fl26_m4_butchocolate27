#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace aiws {

struct BuildCancelled {};

// Per-document hook
class DocumentBuildStep {
public:
    DocumentBuildStep(int delay_ms, const std::atomic<bool>& cancelled,
                      std::mutex& mutex, std::condition_variable& changed)
        : delay_ms_(delay_ms), cancelled_(cancelled), mutex_(mutex),
          changed_(changed), previous_(current_) {
        current_ = this;
    }

    ~DocumentBuildStep() { current_ = previous_; }
    DocumentBuildStep(const DocumentBuildStep&) = delete;
    DocumentBuildStep& operator=(const DocumentBuildStep&) = delete;

    static void before_document() {
        if (current_ && current_->delay_ms_ > 0) {
            std::unique_lock<std::mutex> lock(current_->mutex_);
            current_->changed_.wait_for(lock, std::chrono::milliseconds(current_->delay_ms_),
                                        [] { return current_->cancelled_.load(); });
        }
        check_cancelled();
    }

    static void check_cancelled() {
        if (current_ && current_->cancelled_.load()) {
            throw BuildCancelled{};
        }
    }

private:
    int delay_ms_;
    const std::atomic<bool>& cancelled_;
    std::mutex& mutex_;
    std::condition_variable& changed_;
    const DocumentBuildStep* previous_;
    inline static thread_local const DocumentBuildStep* current_ = nullptr;
};

}  // namespace aiws
