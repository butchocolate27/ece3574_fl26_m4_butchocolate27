#include "aiws/processing_core.hpp"

#include "aiws/chunker.hpp"
#include "aiws/context_builder.hpp"
#include "aiws/corpus_index.hpp"
#include "aiws/retrieval_engine.hpp"
#include "aiws/text_processor.hpp"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace aiws {

struct ProcessingCore::Impl {
    Impl(std::unique_ptr<ChunkingStrategy> selected_chunking,
         std::unique_ptr<RetrievalStrategy> selected_retrieval,
         std::unique_ptr<ContextStrategy> selected_context)
        : chunking(std::move(selected_chunking)),
          retrieval(std::move(selected_retrieval)),
          context(std::move(selected_context)) {}

    std::vector<Chunk> chunks;
    CorpusIndex index;

    std::unique_ptr<ChunkingStrategy> chunking;
    std::unique_ptr<RetrievalStrategy> retrieval;
    std::unique_ptr<ContextStrategy> context;
};

// M1 defaults
ProcessingCore::ProcessingCore()
    : ProcessingCore(
          std::make_unique<Chunker>(ChunkingPolicy{
              kMaxChunkTokens,
              kChunkOverlap,
              kParagraphPreferenceWindow
          }),
          std::make_unique<RetrievalEngine>(),
          std::make_unique<ContextBuilder>()) {}

ProcessingCore::ProcessingCore(
    std::unique_ptr<ChunkingStrategy> chunking,
    std::unique_ptr<RetrievalStrategy> retrieval,
    std::unique_ptr<ContextStrategy> context) {

    if (!chunking || !retrieval || !context) {
        throw std::invalid_argument("all three strategies are required");
    }

    impl_ = std::make_unique<Impl>(
        std::move(chunking),
        std::move(retrieval),
        std::move(context));
}

ProcessingCore::~ProcessingCore() = default;

ProcessingCore::ProcessingCore(ProcessingCore&&) noexcept = default;

ProcessingCore& ProcessingCore::operator=(ProcessingCore&&) noexcept = default;

std::string ProcessingCore::normalize(const std::string& text) {
    return TextProcessor::normalize(text);
}

void ProcessingCore::rebuild(const Workspace& workspace) {
    // Build everything in temporary state first.
    // If validation/build fails, the old corpus remains unchanged.
    std::vector<Chunk> new_chunks;
    std::unordered_set<std::string> document_ids;

    const auto& documents = workspace.documents();

    for (std::size_t order = 0; order < documents.size(); ++order) {
        const auto& document = documents[order];

        if (!document_ids.insert(document.id()).second) {
            throw std::invalid_argument("duplicate document id");
        }

        auto document_chunks =
            impl_->chunking->chunk(document, order);

        for (auto& chunk : document_chunks) {
            new_chunks.push_back(std::move(chunk));
        }
    }

    CorpusIndex new_index(new_chunks);

    // Commit only after the entire rebuild succeeds.
    impl_->chunks = std::move(new_chunks);
    impl_->index = std::move(new_index);
}

const std::vector<Chunk>& ProcessingCore::chunks() const noexcept {
    return impl_->chunks;
}

std::size_t ProcessingCore::chunk_count() const noexcept {
    return impl_->chunks.size();
}

std::size_t ProcessingCore::document_frequency(
    const std::string& term) const {

    const auto normalized_terms =
        TextProcessor::terms(term);

    if (normalized_terms.empty()) {
        return 0;
    }

    if (normalized_terms.size() != 1) {
        throw std::invalid_argument(
            "term must normalize to exactly one token");
    }

    return impl_->index.document_frequency(
        normalized_terms.front());
}

std::size_t ProcessingCore::term_frequency(
    const std::string& term,
    const std::string& chunk_id) const {

    const auto normalized_terms =
        TextProcessor::terms(term);

    if (normalized_terms.empty()) {
        return 0;
    }

    if (normalized_terms.size() != 1) {
        throw std::invalid_argument(
            "term must normalize to exactly one token");
    }

    return impl_->index.term_frequency(
        normalized_terms.front(),
        chunk_id);
}

std::vector<SearchResult> ProcessingCore::search(
    const std::string& query,
    int k) const {

    return impl_->retrieval->search(
        query,
        k,
        impl_->chunks,
        impl_->index);
}

std::vector<ContextItem> ProcessingCore::build_context(
    const std::string& query,
    int k,
    std::size_t token_budget) const {

    if (k < 0) {
        throw std::invalid_argument("k must be non-negative");
    }

    const auto ranked = search(query, k);

    return impl_->context->build(
        ranked,
        token_budget);
}

}  // namespace aiws
