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
