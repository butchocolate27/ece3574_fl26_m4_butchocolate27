#include "aiws/gui/main_window.hpp"
#include "aiws/gui/resettable.hpp"
#include "corpus_state.hpp"

#include "aiws/gui/workspace_widget.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/context_widget.hpp"

#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <utility>

namespace aiws::gui {

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(std::make_unique<BuildController>(), parent) {}

MainWindow::MainWindow(std::unique_ptr<BuildController> builds, QWidget* parent)
    : QMainWindow(parent), builds_(std::move(builds)), ipc_(std::make_unique<IpcServer>(this)),
      state_(CorpusState::initial()) {
    if (!builds_) {
        throw std::invalid_argument("A build controller is required.");
    }
    connect(builds_.get(), &BuildController::finished, this, &MainWindow::build_finished,
            Qt::QueuedConnection);
    setWindowTitle("AI Workspace");
    resize(900, 900);

    // Create main layout
    QWidget* central = new QWidget(this);
    setCentralWidget(central);

    QVBoxLayout* layout = new QVBoxLayout(central);
    layout->setSpacing(10);

    // Add window header
    QLabel* header = new QLabel("AI Workspace", central);
    header->setMargin(12);
    header->setStyleSheet(
        "background-color: #6b2032; color: white; font-size: 24px;"
    );
    layout->addWidget(header);

    // Add workspace area
    workspace_widget_ = new WorkspaceWidget(central);
    layout->addWidget(workspace_widget_, 3);

    // Add search area
    search_widget_ = new SearchWidget(central);
    layout->addWidget(search_widget_, 4);

    // Add bottom areas
    term_inspector_widget_ = new TermInspectorWidget(central);
    context_widget_ = new ContextWidget(central);

    QHBoxLayout* bottom = new QHBoxLayout;
    bottom->addWidget(term_inspector_widget_, 1);
    bottom->addWidget(context_widget_, 1);
    layout->addLayout(bottom, 2);

    // Add status row
    QHBoxLayout* footer = new QHBoxLayout;
    footer->addWidget(new QLabel("Status:", central));

    status_label_ = new QLabel("Ready", central);
    status_label_->setObjectName("applicationStatus");
    footer->addWidget(status_label_, 1);

    reset_button_ = new QPushButton("Reset", central);
    reset_button_->setStyleSheet(
        "background-color: #d6a43c; color: black; padding: 6px 20px;"
    );
    footer->addWidget(reset_button_);

    exit_button_ = new QPushButton("Exit", central);
    exit_button_->setStyleSheet(
        "background-color: #3566a2; color: white; padding: 6px 20px;"
    );
    footer->addWidget(exit_button_);

    layout->addLayout(footer);

    // Connect workspace events
    connect(workspace_widget_, &WorkspaceWidget::add_document_requested,
            this, &MainWindow::add_document);

    connect(workspace_widget_, &WorkspaceWidget::build_corpus_requested,
            this, &MainWindow::build_corpus);

    connect(workspace_widget_, &WorkspaceWidget::document_selected,
            this, &MainWindow::document_selected);

    // Connect search events
    connect(search_widget_, &SearchWidget::search_requested,
            this, &MainWindow::search_requested);

    connect(search_widget_, &SearchWidget::query_changed,
            this, &MainWindow::query_changed);

    connect(search_widget_, &SearchWidget::source_document_requested,
            this, &MainWindow::source_document_requested); //前信号，后处理函数

    connect(search_widget_, &SearchWidget::selected_chunk_changed,
            term_inspector_widget_, &TermInspectorWidget::set_selected_chunk);
            // Update inspector selection

    // Connect inspector events
    connect(term_inspector_widget_, &TermInspectorWidget::term_inspection_requested,
            this, &MainWindow::term_inspection_requested);

    // Connect context events发出请求，带上token budget
    connect(context_widget_, &ContextWidget::build_context_requested,
            this, &MainWindow::build_context_requested);

    // Connect footer buttons把信号传到主窗口
    connect(reset_button_, &QPushButton::clicked,
            this, &MainWindow::reset_application);

    connect(exit_button_, &QPushButton::clicked,
            this, &MainWindow::exit_application);

    update_action_states();
}

void MainWindow::set_exit_callback(ExitCallback callback) noexcept {
    exit_callback_ = callback;
}

MainWindow::~MainWindow() {
    stop_workers();
}

void MainWindow::stop_workers() {
    builds_->shutdown();
    ipc_->stop();
}

void MainWindow::set_build_delay_ms(int ms_per_document) {
    build_delay_ms_ = std::max(0, ms_per_document);
}

bool MainWindow::start_ipc_server(const QString& path, QString& error) {
    std::string reason;
    const bool started = ipc_->start(path.toStdString(), reason);
    error = QString::fromStdString(reason);
    return started;
}

void MainWindow::add_document() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Add Document", QString(),
        "Text Files (*.txt);;All Files (*)"
    );
    if (!path.isEmpty() && !add_document_file(path)) {
        status_label_->setText("Could not read file.");
    }
}

bool MainWindow::add_document_file(const QString& path) {
    if (!state_->accepts_documents()) {
        return false;
    }
    const QFileInfo info(path);
    const QString id = info.absoluteFilePath();
    QFile file(id);
    if (!info.isFile() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return false;
    }
    add_document_text(id, QString::fromUtf8(bytes));
    return true;
}

void MainWindow::add_document_text(const QString& path, const QString& text) {
    if (!state_->accepts_documents()) {
        return;
    }
    const QFileInfo info(path);
    const QString id = info.absoluteFilePath();
    workspace_.add_document(Document(id.toStdString(), info.fileName().toStdString(),
                                     text.toStdString()));
    ++workspace_version_;
    change_state(state_->document_added(*this));
    workspace_widget_->add_document(info.fileName());
    status_label_->setText("Document added: " + info.fileName());
}

void MainWindow::build_corpus() {
    change_state(state_->build_requested(*this));
}

void MainWindow::build_finished(aiws::BuildOutcome outcome) {
    change_state(state_->build_finished(*this, outcome));
}

void MainWindow::change_state(std::unique_ptr<CorpusState> next) {
    if (next) {
        state_ = std::move(next);
        update_action_states();
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    change_state(state_->close(*this));
    event->accept();
}

void MainWindow::document_selected(int row) {
    const std::vector<Document>& documents = workspace_.documents();

    if (row < 0 || static_cast<std::size_t>(row) >= documents.size()) {
        workspace_widget_->clear_selected_document();
        return;
    }

    // Show selected document
    const Document& document = documents[row];
    workspace_widget_->show_selected_document(
        QString::fromStdString(document.title()),
        QString::fromStdString(document.text())
    );
}

void MainWindow::search_requested(const QString& query, int k) {
    if (!state_->has_corpus() || query.trimmed().isEmpty()) {
        return;
    } //query去掉首尾空白后为空

    // Run requested search
    try {
        std::vector<SearchResult> results = processing_core_->search(query.toStdString(), k);

        search_widget_->set_results(results);
        term_inspector_widget_->clear_statistics();
        context_widget_->clear_context();
        status_label_->setText("Search complete.");
    } catch (const std::exception& error) {
        clear_processing_outputs();
        status_label_->setText("Search failed: " + QString::fromUtf8(error.what()));
    }

    update_action_states();
}

void MainWindow::query_changed(const QString& query) {
    // Update action states
    (void)query;
    update_action_states();
}

void MainWindow::build_context_requested(int token_budget) {
    QString query = search_widget_->query();
    if (!state_->has_corpus() || query.trimmed().isEmpty()) {
        return;
    }

    // Build current context
    try {
        std::vector<ContextItem> items = processing_core_->build_context(
            query.toStdString(),
            search_widget_->result_count(),
            static_cast<std::size_t>(token_budget)
        );
        context_widget_->set_context(items);
        status_label_->setText("Context built.");
    } catch (const std::exception& error) {
        context_widget_->clear_context();
        status_label_->setText("Context failed: " + QString::fromUtf8(error.what()));
    }
}

void MainWindow::source_document_requested(const QString& document_id) {
    // Find source document
    const std::vector<Document>& documents = workspace_.documents();
    std::string id = document_id.toStdString();

    for (std::size_t row = 0; row < documents.size(); ++row) {
        if (documents[row].id() == id) {
            workspace_widget_->select_document(static_cast<int>(row));
            status_label_->setText("Source document shown.");
            return;
        }
    }

    status_label_->setText("Source document not found.");
}

void MainWindow::term_inspection_requested(const QString& term,
                                          const QString& chunk_id) {
    if (!state_->has_corpus() || term.trimmed().isEmpty()) {
        return;
    }

    // Read term statistics
    try {
        std::size_t df = processing_core_->document_frequency(term.toStdString());
        bool has_chunk = !chunk_id.isEmpty();
        std::size_t tf = 0;

        if (has_chunk) {
            tf = processing_core_->term_frequency(term.toStdString(), chunk_id.toStdString());
        }

        term_inspector_widget_->set_statistics(df, has_chunk, tf);
        status_label_->setText("Term inspected.");
    } catch (const std::exception& error) {
        term_inspector_widget_->clear_statistics();
        status_label_->setText("Inspection failed: " + QString::fromUtf8(error.what()));
    }
}

void MainWindow::reset_application() {
    change_state(state_->reset(*this));
}

void MainWindow::clear_workspace() {
    // Clear workspace data
    ++workspace_version_;
    workspace_.clear();
    invalidate_corpus();

    // Reset all components
    Resettable* components[] = {
        workspace_widget_,
        search_widget_,
        term_inspector_widget_,
        context_widget_
    };

    for (Resettable* component : components) {
        component->reset(); //workspace->clear context; seach->x query; term-> x term; context-> x output
    }

    status_label_->setText("Application reset.");
}

void MainWindow::exit_application() {
    if (exit_callback_ != nullptr) {
        exit_callback_(this);
    }
} // use callback

void MainWindow::update_action_states() {
    workspace_widget_->set_build_corpus_enabled(state_->can_build());

    bool can_search = state_->has_corpus() && search_widget_->has_query();
    search_widget_->set_search_enabled(can_search);
    context_widget_->set_build_context_enabled(can_search);
    term_inspector_widget_->set_inspection_enabled(state_->has_corpus());
}

void MainWindow::invalidate_corpus() {
    // Invalidate old corpus
    processing_core_.reset();
    workspace_widget_->set_corpus_status("Corpus: Not built");
    clear_processing_outputs();
}

void MainWindow::clear_processing_outputs() {
    // Clear old output
    search_widget_->clear_results();
    term_inspector_widget_->set_selected_chunk(QString());
    term_inspector_widget_->clear_statistics();
    context_widget_->clear_context();
}

}  // namespace aiws::gui
