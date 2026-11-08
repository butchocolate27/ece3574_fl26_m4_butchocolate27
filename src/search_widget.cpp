#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/search_result_model.hpp"

#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QVBoxLayout>
#include <utility>

namespace aiws::gui {

SearchWidget::SearchWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QGroupBox* box = new QGroupBox("Query and Search Results", this);
    QVBoxLayout* box_layout = new QVBoxLayout(box);

    // Create query controls
    QHBoxLayout* controls = new QHBoxLayout;
    controls->addWidget(new QLabel("Query:", box));

    query_edit_ = new QLineEdit(box);
    query_edit_->setPlaceholderText("Search the current corpus");
    controls->addWidget(query_edit_, 1);

    controls->addWidget(new QLabel("Results:", box));
    result_count_spin_ = new QSpinBox(box);
    result_count_spin_->setRange(1, 100);
    controls->addWidget(result_count_spin_);

    search_button_ = new QPushButton("Search", box);
    controls->addWidget(search_button_);
    box_layout->addLayout(controls);

    // Create results table
    result_model_ = new SearchResultModel(this);
    results_view_ = new QTableView(box);
    results_view_->setModel(result_model_);
    results_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    results_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results_view_->setWordWrap(false);
    results_view_->verticalHeader()->hide();
    results_view_->horizontalHeader()->setStretchLastSection(true);
    box_layout->addWidget(results_view_, 2);

    // Create result details
    QHBoxLayout* details_row = new QHBoxLayout;
    details_row->addWidget(new QLabel("Selected Result:", box));
    details_row->addStretch();

    view_source_button_ = new QPushButton("View Source", box);
    details_row->addWidget(view_source_button_);
    box_layout->addLayout(details_row);

    selected_result_text_ = new QPlainTextEdit(box);
    selected_result_text_->setReadOnly(true);
    selected_result_text_->setPlaceholderText("Select a result to view its details.");
    box_layout->addWidget(selected_result_text_, 1);
    layout->addWidget(box);

    // Connect local events
    connect(search_button_, &QPushButton::clicked,
            this, &SearchWidget::request_search);

    connect(query_edit_, &QLineEdit::textChanged,
            this, &SearchWidget::query_changed);

    connect(results_view_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SearchWidget::show_selected_result);

    connect(view_source_button_, &QPushButton::clicked,
            this, &SearchWidget::request_source_document);

    reset();
}

QString SearchWidget::query() const {
    return query_edit_->text();
}

int SearchWidget::result_count() const {
    return result_count_spin_->value();
}

bool SearchWidget::has_query() const {
    return !query().trimmed().isEmpty();
}

void SearchWidget::set_search_enabled(bool enabled) {
    search_button_->setEnabled(enabled);
}

void SearchWidget::set_results(std::vector<SearchResult> results) {
    result_model_->set_results(std::move(results));
    results_view_->clearSelection();
    results_view_->setCurrentIndex(QModelIndex());
    show_selected_result();
}

void SearchWidget::clear_results() {
    result_model_->clear();
    results_view_->clearSelection();
    results_view_->setCurrentIndex(QModelIndex());
    show_selected_result();
}

void SearchWidget::reset() {
    // Restore initial state
    query_edit_->clear();
    result_count_spin_->setValue(3);
    clear_results();
    set_search_enabled(false);
}

void SearchWidget::request_search() {
    if (has_query()) {
        emit search_requested(query(), result_count());
    }
}

void SearchWidget::show_selected_result() {
    QModelIndexList rows = results_view_->selectionModel()->selectedRows();
    const SearchResult* result = nullptr;

    if (!rows.isEmpty()) {
        result = result_model_->result_at(rows.first().row());
    }

    if (result == nullptr) {
        selected_result_text_->clear();
        view_source_button_->setEnabled(false);
        emit selected_chunk_changed(QString());
        return;
    }

    // Show selected result
    QString details = "Document: " + QString::fromStdString(result->document_id);
    details += "\nChunk: " + QString::number(result->chunk_sequence);
    details += "\nChunk ID: " + QString::fromStdString(result->chunk_id);
    details += "\nScore: " + QString::number(result->score);
    details += "\nMatched terms: " + QString::number(result->matched_terms);
    details += "\nText: " + QString::fromStdString(result->text);

    selected_result_text_->setPlainText(details);
    view_source_button_->setEnabled(true);
    emit selected_chunk_changed(QString::fromStdString(result->chunk_id));
}

void SearchWidget::request_source_document() {
    // Request source document
    QModelIndexList rows = results_view_->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return;
    }

    const SearchResult* result = result_model_->result_at(rows.first().row());
    if (result != nullptr) {
        emit source_document_requested(QString::fromStdString(result->document_id));
    }
}

}  // namespace aiws::gui
