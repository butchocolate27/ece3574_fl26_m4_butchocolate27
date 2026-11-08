#include "aiws/gui/workspace_widget.hpp"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>

namespace aiws::gui {

WorkspaceWidget::WorkspaceWidget(QWidget* parent) : QWidget(parent) {
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Create workspace panel
    QGroupBox* workspace_box = new QGroupBox("Workspace", this);
    QVBoxLayout* workspace_layout = new QVBoxLayout(workspace_box);

    add_document_button_ = new QPushButton("Add Document", workspace_box);
    build_corpus_button_ = new QPushButton("Build Corpus", workspace_box);

    QHBoxLayout* buttons = new QHBoxLayout;
    buttons->addWidget(add_document_button_);
    buttons->addWidget(build_corpus_button_);
    buttons->addStretch();
    workspace_layout->addLayout(buttons);

    document_list_ = new QListWidget(workspace_box);
    workspace_layout->addWidget(document_list_, 1);

    corpus_status_label_ = new QLabel(workspace_box);
    workspace_layout->addWidget(corpus_status_label_);

    // Create document panel
    QGroupBox* document_box = new QGroupBox("Selected Document", this);
    QVBoxLayout* document_layout = new QVBoxLayout(document_box);

    document_title_label_ = new QLabel(document_box);
    document_title_label_->setTextFormat(Qt::PlainText);
    document_title_label_->setWordWrap(true);
    document_layout->addWidget(document_title_label_);

    document_text_ = new QPlainTextEdit(document_box);
    document_text_->setReadOnly(true);
    document_text_->setPlaceholderText(
        "Select a document to view its contents."
    );
    document_layout->addWidget(document_text_, 1);

    layout->addWidget(workspace_box, 1);
    layout->addWidget(document_box, 1);

    // Connect local events
    connect(add_document_button_, &QPushButton::clicked,
            this, &WorkspaceWidget::add_document_requested);

    connect(build_corpus_button_, &QPushButton::clicked,
            this, &WorkspaceWidget::build_corpus_requested);

    connect(document_list_, &QListWidget::currentRowChanged,
            this, &WorkspaceWidget::document_selected);

    reset();
}

void WorkspaceWidget::add_document(const QString& title) {
    // Add document title
    document_list_->addItem(title);
    document_list_->setCurrentRow(document_list_->count() - 1);
}

void WorkspaceWidget::select_document(int row) {
    if (row >= 0 && row < document_list_->count()) {
        document_list_->setCurrentRow(row);
    }
}

void WorkspaceWidget::show_selected_document(const QString& title,
                                              const QString& text) {
    document_title_label_->setText(title);
    document_text_->setPlainText(text);
}

void WorkspaceWidget::clear_selected_document() {
    document_title_label_->clear();
    document_text_->clear();
}

void WorkspaceWidget::set_corpus_status(const QString& text) {
    corpus_status_label_->setText(text);
}

void WorkspaceWidget::set_build_corpus_enabled(bool enabled) {
    build_corpus_button_->setEnabled(enabled);
}

void WorkspaceWidget::reset() {
    // Restore initial state
    document_list_->clear();
    clear_selected_document();
    set_corpus_status("Corpus: Not built");
    set_build_corpus_enabled(false);
    add_document_button_->setEnabled(true);
}

}  // namespace aiws::gui
