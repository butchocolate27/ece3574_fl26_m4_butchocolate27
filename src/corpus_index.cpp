#include "aiws/corpus_index.hpp"
#include "aiws/text_processor.hpp"

#include <stdexcept>
#include <unordered_map>

namespace aiws {

CorpusIndex::CorpusIndex(const std::vector<Chunk>& chunks) {
    build(chunks);
}

void CorpusIndex::build(const std::vector<Chunk>& chunks) {
    postings_.clear();
    chunk_by_id_.clear();

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const Chunk& chunk = chunks[i];

        if (!chunk_by_id_.emplace(chunk.id, i).second) {
            throw std::invalid_argument("duplicate chunk id");
        }

        std::unordered_map<std::string, std::size_t> frequencies;

        for (const auto& term : TextProcessor::terms(chunk.text)) {
            ++frequencies[term];
        }

        for (const auto& entry : frequencies) {
            postings_[entry.first].push_back(
                Posting{i, entry.second});
        }
    }
}

std::size_t CorpusIndex::document_frequency(
    const std::string& normalized_term) const noexcept {

    const auto it = postings_.find(normalized_term);

    if (it == postings_.end()) {
        return 0;
    }

    return it->second.size();
}

std::size_t CorpusIndex::term_frequency(
    const std::string& normalized_term,
    const std::string& chunk_id) const noexcept {

    const auto chunk_it = chunk_by_id_.find(chunk_id);

    if (chunk_it == chunk_by_id_.end()) {
        return 0;
    }

    const auto posting_it = postings_.find(normalized_term);

    if (posting_it == postings_.end()) {
        return 0;
    }

    const std::size_t wanted_index = chunk_it->second;

    for (const auto& posting : posting_it->second) {
        if (posting.chunk_index == wanted_index) {
            return posting.frequency;
        }
    }

    return 0;
}

const std::vector<CorpusIndex::Posting>* CorpusIndex::postings(
    const std::string& normalized_term) const noexcept {

    const auto it = postings_.find(normalized_term);

    if (it == postings_.end()) {
        return nullptr;
    }

    return &it->second;
}

const Chunk* CorpusIndex::find_chunk(
    const std::vector<Chunk>& chunks,
    const std::string& chunk_id) const noexcept {

    const auto it = chunk_by_id_.find(chunk_id);

    if (it == chunk_by_id_.end() || it->second >= chunks.size()) {
        return nullptr;
    }

    return &chunks[it->second];
}

std::size_t CorpusIndex::chunk_index(
    const std::string& chunk_id) const {

    const auto it = chunk_by_id_.find(chunk_id);

    if (it == chunk_by_id_.end()) {
        throw std::out_of_range("unknown chunk id");
    }

    return it->second;
}

}  // namespace aiws
