#include "aiws/chunker.hpp"
#include "aiws/text_processor.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace aiws {

Chunker::Chunker(ChunkingPolicy policy) : policy_(policy) {
    if (policy_.max_tokens == 0 ||
        policy_.overlap >= policy_.max_tokens ||
        policy_.paragraph_window > policy_.max_tokens) {
        throw std::invalid_argument("invalid chunking policy");
    }
}

std::vector<Chunk> Chunker::chunk(const Document& document,
                                  std::size_t document_order) const {
    std::vector<Chunk> chunks;
    const auto tokens = TextProcessor::tokenize(document.text());

    if (tokens.empty()) {
        return chunks;
    }

    std::size_t start = 0;
    std::size_t sequence = 0;

    while (start < tokens.size()) {
        std::size_t end =
            std::min(start + policy_.max_tokens, tokens.size());

        if (end < tokens.size()) {
            std::size_t window_start =
                (end > policy_.paragraph_window)
                    ? end - policy_.paragraph_window
                    : start;

            window_start = std::max(window_start, start);

            for (std::size_t i = end; i >= window_start; --i) {
                if (i > start &&
                    tokens[i - 1].paragraph != tokens[i].paragraph) {
                    end = i;
                    break;
                }

                if (i == window_start) {
                    break;
                }
            }
        }

        Chunk chunk;
        chunk.id = document.id() + "#" + std::to_string(sequence);
        chunk.document_id = document.id();
        chunk.document_order = document_order;
        chunk.sequence = sequence;
        chunk.text = TextProcessor::join(tokens, start, end);
        chunk.token_count = end - start;
        chunk.source_begin = tokens[start].begin;
        chunk.source_end = tokens[end - 1].end;

        chunks.push_back(std::move(chunk));

        if (end == tokens.size()) {
            break;
        }

        start = end - policy_.overlap;
        ++sequence;
    }

    return chunks;
}

}  // namespace aiws
