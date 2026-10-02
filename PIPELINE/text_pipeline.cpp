/**
 * text_pipeline.cpp: Scalpel tokenizer and sentence segmenter integration
 *
 * This translation unit is intentionally isolated: it includes Scalpel
 * headers (which define their own Tokenizer class) but does NOT include
 * any morphology headers that define a conflicting Tokenizer class.
 *
 * The public interface (segment_into_sentences) is declared in
 * text_pipeline.h using only standard-library types, so callers
 * never need to see Scalpel's internal types.
 */

#include "text_pipeline.h"
#include <cctype>
#include <string>

// Scalpel FSM tokenizer and sentence segmenter
// Path: morphology/PIPELINE/ -> ../../Scalpel/
#include "../../Scalpel/tokenizer.h"
#include "../../Scalpel/sentence_segmenter.h"

/**
 * segment_into_sentences: Convert raw text to sentence-grouped word lists
 *
 * Steps:
 *   1. Tokenize, Scalpel FSM walks the text character-by-character,
 *                  emitting typed Token objects (WORD, PUNCT, ABBREVIATION…)
 *   2. Segment, SentenceSegmenter groups the token stream into sentences
 *                  using punctuation and SENTENCE_END markers
 *   3. Filter, Only TokenType::WORD tokens are forwarded; numbers,
 *                  punctuation, abbreviations, etc. are dropped so the
 *                  morphological analyzer only sees alphabetic words
 *
 * @param text  Any raw English text
 * @return      One inner vector per sentence, containing the text of each
 *              WORD token in that sentence (original casing preserved)
 */
std::vector<std::vector<std::string>> segment_into_sentences(const std::string& text) {
    // ── Phase 1: Tokenize ────────────────────────────────────────────────
    // Scalpel's Tokenizer is an FSM; it runs in a single linear pass with
    // O(1) lookahead, no regex engine, no heap allocation per character.
    Tokenizer scalpel_tokenizer;
    std::vector<Token> tokens = scalpel_tokenizer.tokenize(text);

    // ── Phase 2: Sentence segmentation ──────────────────────────────────
    // SentenceSegmenter groups tokens into sentences using terminal
    // punctuation (. ! ?) while respecting ABBREVIATION tokens so that
    // "Dr. Smith arrived." is one sentence, not two.
    SentenceSegmenter segmenter;
    std::vector<std::vector<Token>> sentences = segmenter.segment(tokens);

    // ── Phase 3: Filter to WORD tokens ──────────────────────────────────
    // The morphological analyzer expects plain alphabetic words.
    // Discard numbers, punctuation, contractions, hyphenated forms, etc.
    std::vector<std::vector<std::string>> result;
    result.reserve(sentences.size());

    for (const auto& sentence : sentences) {
        std::vector<std::string> words;
        for (const auto& token : sentence) {
            // Accept WORD tokens that contain only letters.
            // Note: Scalpel's tokenizer flushes any remaining buffer as WORD
            // at end-of-input, so a terminal punctuation mark (e.g. ".") can
            // arrive mislabeled as WORD.  The alphabetic guard rejects it.
            // Accept WORD tokens that contain only letters.
            // Note: Scalpel's tokenizer flushes any remaining buffer as WORD
            // at end-of-input, so a terminal punctuation mark (e.g. ".") can
            // arrive mislabeled as WORD.  The alphabetic guard rejects it.
            if (token.type != TokenType::WORD || token.text.empty()) continue;
            bool all_alpha = true;
            for (unsigned char ch : token.text) {
                if (!std::isalpha(ch)) { all_alpha = false; break; }
            }
            if (all_alpha) {
                words.push_back(token.text);
            }
        }
        if (!words.empty()) {
            result.push_back(std::move(words));
        }
    }

    return result;
}

// ── Located tokens, for surface segmentation text mode ──────────────────────

/**
 * token_kind_name: Scalpel's TokenType as the string the output format uses
 *
 * SENTENCE_END is a marker the sentence segmenter consumes rather than a class
 * of surface text; if the tokenizer ever emits one it is reported as PUNCT,
 * which is what it is made of.
 */
static std::string token_kind_name(TokenType type) {
    switch (type) {
        case TokenType::WORD:          return "WORD";
        case TokenType::NUMBER:        return "NUMBER";
        case TokenType::PUNCT:         return "PUNCT";
        case TokenType::ABBREVIATION:  return "ABBREVIATION";
        case TokenType::CONTRACTION:   return "CONTRACTION";
        case TokenType::HYPHENATED:    return "HYPHENATED";
        case TokenType::SENTENCE_END:  return "PUNCT";
        default:                       return "PUNCT";
    }
}

std::vector<TextToken> tokenize_line(const std::string& line) {
    Tokenizer scalpel_tokenizer;
    std::vector<Token> tokens = scalpel_tokenizer.tokenize(line);

    std::vector<TextToken> located;
    located.reserve(tokens.size());

    // Tokens are consumed strictly left to right. `cursor` is the first byte
    // not yet covered, which keeps repeated tokens ("a a") from both matching
    // the same occurrence.
    size_t cursor = 0;

    for (const auto& token : tokens) {
        if (token.text.empty()) continue;

        size_t at = std::string::npos;

        // Prefer Scalpel's own offset, but only once it is confirmed to point
        // at this token's text. Fall back to a forward search otherwise.
        if (token.start_index >= 0) {
            const size_t claimed = static_cast<size_t>(token.start_index);
            if (claimed >= cursor &&
                claimed + token.text.size() <= line.size() &&
                line.compare(claimed, token.text.size(), token.text) == 0) {
                at = claimed;
            }
        }
        if (at == std::string::npos) {
            at = line.find(token.text, cursor);
        }
        if (at == std::string::npos) {
            // The token's text is not in the line at or after the cursor, so
            // there is no honest position to report. Dropping it leaves the
            // span in the gap, where the caller recovers it verbatim.
            continue;
        }

        TextToken out;
        out.text  = token.text;
        out.kind  = token_kind_name(token.type);
        out.start = at;
        located.push_back(out);

        cursor = at + token.text.size();
    }

    return located;
}
