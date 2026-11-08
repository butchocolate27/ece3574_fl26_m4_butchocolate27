#include "aiws/gui/context_widget.hpp"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace aiws::gui {

ContextWidget::ContextWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QGroupBox* box = new QGroupBox("Context", this);
    QVBoxLayout* box_layout = new QVBoxLayout(box);

    // Create budget controls
    QHBoxLayout* controls = new QHBoxLayout;
    controls->addWidget(new QLabel("Token budget:", box));

    context_budget_slider_ = new QSlider(Qt::Horizontal, box);
    context_budget_slider_->setRange(50, 1000);
    context_budget_slider_->setSingleStep(50);
    context_budget_slider_->setPageStep(50);
    controls->addWidget(context_budget_slider_, 1);

    budget_value_label_ = new QLabel(box);
    controls->addWidget(budget_value_label_);

    build_context_button_ = new QPushButton("Build Context", box);
    controls->addWidget(build_context_button_);
    box_layout->addLayout(controls);

    // Create context display
    context_text_ = new QPlainTextEdit(box);
    context_text_->setReadOnly(true);
    context_text_->setPlaceholderText("Built context will appear here.");
    box_layout->addWidget(context_text_, 1);
    layout->addWidget(box);

    // Connect local events
    connect(context_budget_slider_, &QSlider::valueChanged,
            this, &ContextWidget::budget_changed);

    connect(build_context_button_, &QPushButton::clicked,
            this, &ContextWidget::request_context);

    reset();
}

int ContextWidget::token_budget() const {
    return context_budget_slider_->value();
}

void ContextWidget::set_build_context_enabled(bool enabled) {
    build_context_button_->setEnabled(enabled);
}

void ContextWidget::set_context(const std::vector<ContextItem>& items) {
    // Show context items
    QString text;
    for (const ContextItem& item : items) {
        text += "Document: " + QString::fromStdString(item.document_id);
        text += "\nChunk: " + QString::number(item.chunk_sequence);
        text += "\nTokens: " + QString::number(item.token_count);
        text += "\nScore: " + QString::number(item.score);
        text += "\nTruncated: ";
        if (item.truncated) {
            text += "Yes";
        } else {
            text += "No";
        }
        text += "\nText: " + QString::fromStdString(item.text) + "\n\n";
    }
    context_text_->setPlainText(text);
}

void ContextWidget::clear_context() {
    context_text_->clear();
}

void ContextWidget::reset() {
    // Restore initial state
    context_budget_slider_->setValue(300);
    budget_changed(300);
    clear_context();
    set_build_context_enabled(false);
}

void ContextWidget::request_context() {
    emit build_context_requested(token_budget());
}

void ContextWidget::budget_changed(int value) {
    budget_value_label_->setText(QString::number(value));
}

}  // namespace aiws::gui
