#include "aiws/gui/term_inspector_widget.hpp"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aiws::gui {

TermInspectorWidget::TermInspectorWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QGroupBox* box = new QGroupBox("Term Inspector", this);
    QVBoxLayout* box_layout = new QVBoxLayout(box);

    // Create term controls
    QHBoxLayout* controls = new QHBoxLayout;
    controls->addWidget(new QLabel("Term:", box));

    term_edit_ = new QLineEdit(box);
    term_edit_->setPlaceholderText("Enter a term");
    controls->addWidget(term_edit_, 1);

    inspect_button_ = new QPushButton("Inspect", box);
    controls->addWidget(inspect_button_);
    box_layout->addLayout(controls);

    // Create statistics rows
    QHBoxLayout* document_row = new QHBoxLayout;
    document_row->addWidget(new QLabel("Document Frequency:", box));
    document_frequency_value_ = new QLabel(box);
    document_row->addWidget(document_frequency_value_, 1);
    box_layout->addLayout(document_row);

    QHBoxLayout* chunk_row = new QHBoxLayout;
    chunk_row->addWidget(new QLabel("Selected Chunk TF:", box));
    chunk_frequency_value_ = new QLabel(box);
    chunk_row->addWidget(chunk_frequency_value_, 1);
    box_layout->addLayout(chunk_row);
    box_layout->addStretch();
    layout->addWidget(box);

    // Connect inspect button
    connect(inspect_button_, &QPushButton::clicked,
            this, &TermInspectorWidget::request_inspection);

    reset();
}

void TermInspectorWidget::set_inspection_enabled(bool enabled) {
    inspect_button_->setEnabled(enabled);
}

void TermInspectorWidget::set_selected_chunk(const QString& chunk_id) {
    if (selected_chunk_id_ != chunk_id) {
        selected_chunk_id_ = chunk_id;
        clear_statistics();
    }
}

void TermInspectorWidget::set_statistics(std::size_t document_frequency,
                                        bool has_chunk_frequency,
                                        std::size_t chunk_frequency) {
    // Show term statistics
    document_frequency_value_->setText(QString::number(document_frequency));

    if (has_chunk_frequency) {
        chunk_frequency_value_->setText(QString::number(chunk_frequency));
    } else {
        chunk_frequency_value_->setText("-");
    }
}

void TermInspectorWidget::clear_statistics() {
    document_frequency_value_->setText("-");
    chunk_frequency_value_->setText("-");
}

void TermInspectorWidget::reset() {
    // Restore initial state
    term_edit_->clear();
    selected_chunk_id_.clear();
    clear_statistics();
    set_inspection_enabled(false);
}

void TermInspectorWidget::request_inspection() {
    // Request term inspection
    QString term = term_edit_->text().trimmed();
    if (term.isEmpty()) {
        return;
    }

    emit term_inspection_requested(term, selected_chunk_id_);
}

}  // namespace aiws::gui
