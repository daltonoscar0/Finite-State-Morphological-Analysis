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
