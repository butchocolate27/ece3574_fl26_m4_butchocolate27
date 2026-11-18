#include "corpus_state.hpp"

#include "aiws/gui/workspace_widget.hpp"

#include <QLabel>

#include <exception>
#include <utility>

namespace aiws::gui {

class MainWindow::CorpusState::Empty final : public CorpusState {};

class MainWindow::CorpusState::Dirty final : public CorpusState {
public:
    bool can_build() const override { return true; }
    Next build_requested(MainWindow& window) const override { return start_build(window); }
};

class MainWindow::CorpusState::Building final : public CorpusState {
public:
    Next document_added(MainWindow& window) const override {
        window.builds_->request_cancel();
        return CorpusState::document_added(window);
    }

    Next reset(MainWindow& window) const override {
        window.builds_->request_cancel();
        return CorpusState::reset(window);
    }

    Next build_finished(MainWindow& window, BuildOutcome& outcome) const override;
};

class MainWindow::CorpusState::Current final : public CorpusState {
public:
    bool has_corpus() const override { return true; }
    bool can_build() const override { return true; }
    Next build_requested(MainWindow& window) const override { return start_build(window); }
};

class MainWindow::CorpusState::Closing final : public CorpusState {
public:
    bool accepts_documents() const override { return false; }
    Next reset(MainWindow&) const override { return {}; }
    Next close(MainWindow&) const override { return {}; }
};

MainWindow::CorpusState::Next MainWindow::CorpusState::initial() {
    return std::make_unique<Empty>();
}

MainWindow::CorpusState::Next MainWindow::CorpusState::document_added(MainWindow& window) const {
    window.invalidate_corpus();
    return std::make_unique<Dirty>();
}

MainWindow::CorpusState::Next MainWindow::CorpusState::reset(MainWindow& window) const {
    window.clear_workspace();
    return std::make_unique<Empty>();
}

MainWindow::CorpusState::Next MainWindow::CorpusState::close(MainWindow& window) const {
    window.stop_workers();
    window.invalidate_corpus();
    window.centralWidget()->setEnabled(false);
    return std::make_unique<Closing>();
}

MainWindow::CorpusState::Next MainWindow::CorpusState::start_build(MainWindow& window) {
    window.invalidate_corpus();
    try {
        window.builds_->start(window.workspace_, window.workspace_version_, window.build_delay_ms_);
    } catch (const std::exception& error) {
        window.status_label_->setText("Build failed: " + QString::fromUtf8(error.what()));
        return std::make_unique<Dirty>();
    }
    window.workspace_widget_->set_corpus_status("Corpus: Building...");
    window.status_label_->setText("Building corpus...");
    return std::make_unique<Building>();
}

MainWindow::CorpusState::Next MainWindow::CorpusState::Building::build_finished(
    MainWindow& window, BuildOutcome& outcome) const {
    if (outcome.workspace_version != window.workspace_version_) {
        return {};
    }
    if (outcome.cancelled || !outcome.corpus || !outcome.error.empty()) {
        window.workspace_widget_->set_corpus_status("Corpus: Not built");
        window.status_label_->setText(outcome.cancelled ? "Build cancelled."
            : "Build failed: " + QString::fromStdString(outcome.error));
        return std::make_unique<Dirty>();
    }
    window.processing_core_ = std::move(outcome.corpus);
    window.workspace_widget_->set_corpus_status(
        "Corpus: " + QString::number(window.processing_core_->chunk_count()) + " chunks");
    window.status_label_->setText("Corpus built.");
    return std::make_unique<Current>();
}

}  // namespace aiws::gui
