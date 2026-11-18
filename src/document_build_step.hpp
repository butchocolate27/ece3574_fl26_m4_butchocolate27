#pragma once

#include <functional>
#include <utility>

namespace aiws {

// Per-document hook
class DocumentBuildStep {
public:
    explicit DocumentBuildStep(std::function<void()> step)
        : step_(std::move(step)), previous_(current_) {
        current_ = this;
    }

    ~DocumentBuildStep() { current_ = previous_; }

    DocumentBuildStep(const DocumentBuildStep&) = delete;
    DocumentBuildStep& operator=(const DocumentBuildStep&) = delete;

    static void before_document() {
        if (current_) {
            current_->step_();
        }
    }

private:
    const std::function<void()> step_;
    const DocumentBuildStep* previous_;
    inline static thread_local const DocumentBuildStep* current_ = nullptr;
};

}  // namespace aiws
