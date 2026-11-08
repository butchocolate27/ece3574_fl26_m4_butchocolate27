#include "aiws/text_processor.hpp"

namespace aiws {

namespace {

bool is_ascii_alnum(unsigned char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9');
}

char ascii_lower(unsigned char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return static_cast<char>(c);
}

}  // namespace

std::vector<TokenInfo> TextProcessor::tokenize(const std::string& text) {
    std::vector<TokenInfo> tokens;

    std::size_t i = 0;
    std::size_t paragraph = 0;
    bool paragraph_break = false;

    while (i < text.size()) {
        if (!is_ascii_alnum(static_cast<unsigned char>(text[i]))) {
            std::size_t newline_count = 0;

            while (i < text.size() &&
                   !is_ascii_alnum(static_cast<unsigned char>(text[i]))) {
                if (text[i] == '\n') {
                    ++newline_count;
                }
                ++i;
            }

            if (newline_count >= 2 && !tokens.empty()) {
                paragraph_break = true;
            }

            continue;
        }

        if (paragraph_break) {
            ++paragraph;
            paragraph_break = false;
        }

        const std::size_t begin = i;
        std::string token;

        while (i < text.size() &&
               is_ascii_alnum(static_cast<unsigned char>(text[i]))) {
            token.push_back(
                ascii_lower(static_cast<unsigned char>(text[i])));
            ++i;
        }

        tokens.push_back(TokenInfo{token, begin, i, paragraph});
    }

    return tokens;
}

std::vector<std::string> TextProcessor::terms(const std::string& text) {
    const auto tokens = tokenize(text);

    std::vector<std::string> result;
    result.reserve(tokens.size());

    for (const auto& token : tokens) {
        result.push_back(token.token);
    }

    return result;
}

std::string TextProcessor::normalize(const std::string& text) {
    const auto tokens = terms(text);
    return join(tokens, 0, tokens.size());
}

std::string TextProcessor::join(const std::vector<TokenInfo>& tokens,
                                std::size_t begin,
                                std::size_t end) {
    if (begin >= end || begin >= tokens.size()) {
        return {};
    }

    if (end > tokens.size()) {
        end = tokens.size();
    }

    std::string result;

    for (std::size_t i = begin; i < end; ++i) {
        if (!result.empty()) {
            result += ' ';
        }
        result += tokens[i].token;
    }

    return result;
}

std::string TextProcessor::join(const std::vector<std::string>& tokens,
                                std::size_t begin,
                                std::size_t end) {
    if (begin >= end || begin >= tokens.size()) {
        return {};
    }

    if (end > tokens.size()) {
        end = tokens.size();
    }

    std::string result;

    for (std::size_t i = begin; i < end; ++i) {
        if (!result.empty()) {
            result += ' ';
        }
        result += tokens[i];
    }

    return result;
}

}  // namespace aiws
