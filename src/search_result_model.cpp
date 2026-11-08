#include "aiws/gui/search_result_model.hpp"

#include <QString>
#include <QVariant>
#include <utility>

namespace aiws::gui {

SearchResultModel::SearchResultModel(QObject* parent)
    : QAbstractTableModel(parent) {
}

int SearchResultModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(results_.size()); // table view size
}

int SearchResultModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return 5;
}

QVariant SearchResultModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || role != Qt::DisplayRole) {
        return {};
    } // specific data of each empty; 告诉 View 某个格子显示什么

    const SearchResult* result = result_at(index.row());
    if (result == nullptr) {
        return {};
    }

    // Return cell values
    switch (index.column()) {
    case 0:
        return index.row() + 1;
    case 1:
        return QString::fromStdString(result->document_id);
    case 2:
        return static_cast<qulonglong>(result->chunk_sequence);
    case 3:
        return result->score;
    case 4:
        return QString::fromStdString(result->text);
    default:
        return {};
    }
}

QVariant SearchResultModel::headerData(int section,
                                     Qt::Orientation orientation,
                                     int role) const {
    if (role != Qt::DisplayRole || section < 0) {
        return {};
    }

    if (orientation == Qt::Horizontal && section < 5) {
        const QString headers[] = {"Rank", "Document", "Chunk", "Score", "Text"};
        return headers[section];
    } // vertical headers= row + 1

    if (orientation == Qt::Vertical && section < rowCount()) {
        return section + 1;
    }

    return {};
}

void SearchResultModel::set_results(std::vector<SearchResult> results) {
    // Replace stored results
    beginResetModel();
    results_ = std::move(results);
    endResetModel();
}

void SearchResultModel::clear() {
    beginResetModel();
    results_.clear();
    endResetModel();
}

const SearchResult* SearchResultModel::result_at(int row) const noexcept {
    // Check row bounds
    if (row < 0 || static_cast<std::size_t>(row) >= results_.size()) {
        return nullptr;
    }
    return &results_[row];
}

}  // namespace aiws::gui
