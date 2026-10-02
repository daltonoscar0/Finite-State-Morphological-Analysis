#pragma once

#include <string>
#include <vector>

/**
 * text_pipeline: Scalpel-powered text segmentation interface
 *
 * This module sits between raw user text and the morphological analyzer.
 * It uses Scalpel (an FSM-based tokenizer and sentence segmenter) to
 * convert a raw string into structured, sentence-grouped word lists.
 *
 * Pipeline:
 *   Raw text
 *     └─► Scalpel Tokenizer   (FSM, char-level -> typed Token stream)
 *           └─► Scalpel SentenceSegmenter (groups tokens into sentences)
 *                 └─► Filter for WORD tokens
 *                       └─► vector<vector<string>> (sentences of words)
 *
 * Isolation:
 *   By hiding all Scalpel types behind this header, the rest of the
 *   morphology project never sees Scalpel's Tokenizer class, which
 *   would otherwise conflict with the morphology project's own
 *   internal Tokenizer (ANALYSIS/tokenizer.h).
 *
 * @param text  Raw input text (may contain multiple sentences)
 * @return      Outer vector = sentences, inner vector = WORD tokens
 *              Only TokenType::WORD tokens are included; punctuation,
 *              numbers, abbreviations, etc. are discarded.
 */
std::vector<std::vector<std::string>> segment_into_sentences(const std::string& text);

/**
 * TextToken: One Scalpel token, located in the line it came from
 *
 * segment_into_sentences above keeps only WORD tokens and discards where they
 * were, which is all the lexical output needs. The surface segmentation text
 * mode needs every token, its class, and its exact position, so that the
 * whitespace between tokens can be reported and the input line reproduced
 * character for character.
 *
 * `start` is a byte offset into the line and is verified before being
 * returned: the token's text is known to occur at that offset. Tokens whose
 * text cannot be located at all are dropped, and the caller recovers the
 * uncovered span from the gap it leaves behind.
 */
struct TextToken {
    std::string text;
    std::string kind;   // WORD, NUMBER, PUNCT, ABBREVIATION, CONTRACTION, HYPHENATED
    size_t start = 0;   // byte offset of text within the line
};

/**
 * tokenize_line: Scalpel's token stream for one line, with positions
 *
 * Tokens come back in order of appearance and never overlap. The spans
 * between them, and any span before the first or after the last, are left for
 * the caller: they hold the whitespace, plus any character Scalpel did not
 * emit a token for.
 *
 * @param line  One line of raw text, with no line terminator
 * @return      Located tokens, in order
 */
std::vector<TextToken> tokenize_line(const std::string& line);
