#include "aiws/retrieval_engine.hpp"
#include "aiws/text_processor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace aiws {

double RetrievalEngine::canonical_score(double value) {
    return std::round(value * 1e12) / 1e12;
}

std::vector<SearchResult> RetrievalEngine::search(
    const std::string& query,
    int k,
    const std::vector<Chunk>& chunks,
    const CorpusIndex& index) const {

    if (k < 0) {
        throw std::invalid_argument("k must not be negative");
    }

    if (k == 0 || chunks.empty()) {
        return {};
    }

    // Normalize query and keep each distinct query term once.
    const auto raw_terms = TextProcessor::terms(query);

    std::vector<std::string> query_terms;
    std::unordered_set<std::string> seen;

    for (const auto& term : raw_terms) {
        if (seen.insert(term).second) {
            query_terms.push_back(term);
        }
    }

    if (query_terms.empty()) {
        return {};
    }

    // chunk index -> accumulated TF-IDF score
    std::unordered_map<std::size_t, double> scores;

    // chunk index -> number of distinct query terms matched
    std::unordered_map<std::size_t, std::size_t> matched;

    const double N = static_cast<double>(chunks.size());

    for (const auto& term : query_terms) {
        const auto* term_postings = index.postings(term);

        if (term_postings == nullptr) {
            continue;
        }

        const double df =
            static_cast<double>(index.document_frequency(term));

        const double idf =
            std::log((N + 1.0) / (df + 1.0)) + 1.0;

        for (const auto& posting : *term_postings) {
            const double tf =
                1.0 + std::log(
                    static_cast<double>(posting.frequency));

            scores[posting.chunk_index] += tf * idf;
            ++matched[posting.chunk_index];
        }
    }

    std::vector<SearchResult> results;
    results.reserve(scores.size());

    const double query_term_count =
        static_cast<double>(query_terms.size());

    for (const auto& entry : scores) {
        const std::size_t chunk_index = entry.first;
        const Chunk& chunk = chunks[chunk_index];

        const std::size_t matched_terms = matched[chunk_index];

        const double coverage =
            1.0 +
            0.10 *
                (static_cast<double>(matched_terms) /
                 query_term_count);

        const double score =
            canonical_score(entry.second * coverage);

        results.push_back(SearchResult{
            chunk.id,
            chunk.document_id,
            chunk.sequence,
            chunk.text,
            score,
            matched_terms
        });
    }

    std::sort(
        results.begin(),
        results.end(),
        [&index](const SearchResult& a, const SearchResult& b) {
            if (a.score != b.score) {
                return a.score > b.score;
            }

            const std::size_t ai = index.chunk_index(a.chunk_id);
            const std::size_t bi = index.chunk_index(b.chunk_id);

            // Chunk vector follows document insertion order and
            // sequence order, so this provides deterministic ties.
            return ai < bi;
        });

    if (results.size() > static_cast<std::size_t>(k)) {
        results.resize(static_cast<std::size_t>(k));
    }

    return results;
}

}  // namespace aiws
