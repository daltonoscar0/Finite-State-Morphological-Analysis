/**
 * main.cpp: Entry point for the Finite-State Morphological Analyzer
 *
 * Pipeline architecture:
 *
 *   Raw text
 *     └─► PIPELINE/text_pipeline  (Scalpel FSM tokenizer + sentence segmenter)
 *           └─► WORD tokens, grouped by sentence
 *                 └─► Analyzer  (lexicon FSA + morphological rule FSTs)
 *                       └─► Analysis results  (stem + POS + features)
 *
 * Scalpel (Phase 1) handles text segmentation:
 *   - Character-level FSM tokenizer (no regex, no heap alloc per char)
 *   - Distinguishes WORD, NUMBER, PUNCT, ABBREVIATION, CONTRACTION, etc.
 *   - Sentence segmenter respects abbreviations (Dr., Jan.) to avoid
 *     false sentence boundaries
 *
 * The morphological analyzer (Phase 2) handles word-level analysis:
 *   - Trie-based lexicon FSA for O(n) stem lookup
 *   - Rule FSTs encode morphological alternations (y->i, sibilant +es, etc.)
 *   - Produces: stem +POS +FEATURE... notation
 *
 * Compilation:
 *   make analyzer
 *
 * Usage:
 *   ./analyzer                    # Interactive mode, type text, see analyses
 *   ./analyzer cats dogs flies    # Batch mode, analyze specific words
 */

#include "PIPELINE/text_pipeline.h"        // Scalpel bridge (no Tokenizer name clash)
#include "SYMBOLS/symbol.h"
#include "LEXICON/lexicon_fsa.h"
#include "ANALYSIS/analyzer.h"
#include "OUTPUT/pretty_print.h"
#include "LANGUAGES/english_configuration.h"

#include <iostream>
#include <string>
#include <vector>

// ── Banner ─────────────────────────────────────────────────────────────────

void print_banner() {
    std::cout << "\n============================================" << std::endl;
    std::cout << "  Finite-State Morphological Analyzer" << std::endl;
    std::cout << "  Tokenizer:  Scalpel FSM (Phase 1)" << std::endl;
    std::cout << "  Morphology: FST + Lexicon FSA (Phase 2)" << std::endl;
    std::cout << "  Language:   English" << std::endl;
    std::cout << "============================================\n" << std::endl;
}

// ── Core analysis ──────────────────────────────────────────────────────────

/**
 * analyze_text: Run a raw text string through the full two-phase pipeline
 *
 * Phase 1, Scalpel tokenizer + sentence segmenter:
 *   Converts text to sentence-grouped WORD token lists.
 *
 * Phase 2, Morphological analyzer:
 *   For each WORD token, produces stem + POS + feature analyses.
 *
 * Output format:
 *   [Sentence 1]
 *   cats -> cat +N +PL
 *   flies ->
 *     fly +N +PL
 *     fly +V +3SG
 *
 * @param text      Raw input (may contain multiple sentences)
 * @param analyzer  The morphological analysis engine
 */
void analyze_text(const std::string& text, Analyzer& analyzer) {
    // Phase 1: Scalpel tokenizes and segments
    auto sentences = segment_into_sentences(text);

    if (sentences.empty()) {
        std::cout << "(no words found)" << std::endl;
        return;
    }

    for (size_t i = 0; i < sentences.size(); i++) {
        const auto& words = sentences[i];
        std::cout << "[Sentence " << (i + 1) << "]" << std::endl;

        // Phase 2: morphological analysis for each word
        for (const auto& word : words) {
            auto analyses = analyzer.analyze(word);
            PrettyPrinter::print_analyses(word, analyses);
        }
        std::cout << std::endl;
    }
}

// ── Interactive mode ───────────────────────────────────────────────────────

/**
 * interactive_mode: REPL, read a line of text, analyze, repeat
 *
 * Each line is treated as a complete input text: Scalpel tokenizes it
 * and identifies sentence boundaries, then the morphological analyzer
 * processes each WORD token.
 *
 * Commands:
 *   :quit / :q / :exit, Exit
 *   :help / :h, Show commands
 *   :stats, Lexicon statistics
 *   <any other text>, Analyze with Scalpel + morphology
 *
 * Example session:
 *   > The cats and dogs walked.
 *   [Sentence 1]
 *   The -> (no analysis)
 *   cats -> cat +N +PL
 *   and -> (no analysis)
 *   dogs -> dog +N +PL
 *   walked -> walk +V +PAST
 */
void interactive_mode(Analyzer& analyzer, const LexiconFSA& lexicon) {
    print_banner();
    std::cout << "Enter text to tokenize (Scalpel) and analyze. :help for commands.\n"
              << std::endl;

    std::string input;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, input)) break;  // EOF

        // Trim leading/trailing whitespace
        auto first = input.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        auto last = input.find_last_not_of(" \t\r\n");
        input = input.substr(first, last - first + 1);

        // Handle commands
        if (input == ":quit" || input == ":q" || input == ":exit") {
            std::cout << "Goodbye!" << std::endl;
            break;
        }
        if (input == ":help" || input == ":h") {
            std::cout << "  <text>   Tokenize with Scalpel, then analyze morphology\n"
                      << "  :stats   Show lexicon statistics\n"
                      << "  :quit    Exit\n" << std::endl;
            continue;
        }
        if (input == ":stats") {
            std::cout << "Lexemes:     " << lexicon.num_lexemes()     << std::endl;
            std::cout << "FSA states:  " << lexicon.num_states()      << std::endl;
            std::cout << "Transitions: " << lexicon.num_transitions()  << std::endl;
            std::cout << std::endl;
            continue;
        }

        // Analyze text through both pipeline stages
        analyze_text(input, analyzer);
    }
}

// ── Batch mode ─────────────────────────────────────────────────────────────

/**
 * batch_mode: Analyze words supplied as command-line arguments
 *
 * Each argument is treated as a single word and analyzed directly
 * (no Scalpel tokenization needed, input is already word-level).
 *
 * Example:
 *   ./analyzer cats dogs flies
 *   cats -> cat +N +PL
 *   dogs -> dog +N +PL
 *   flies ->
 *     fly +N +PL
 *     fly +V +3SG
 */
void batch_mode(Analyzer& analyzer, const std::vector<std::string>& words) {
    for (const auto& word : words) {
        auto analyses = analyzer.analyze(word);
        PrettyPrinter::print_analyses(word, analyses);
    }
}

// ── Entry point ────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Initialization order matters: SymbolTable must outlive everything else
    SymbolTable symbols;
    LexiconFSA lexicon(&symbols);
    Analyzer   analyzer(&symbols, &lexicon);

    std::cout << "Loading English lexicon and rules..." << std::endl;
    EnglishConfig::initialize(symbols, lexicon, analyzer);
    std::cout << "Loaded " << lexicon.num_lexemes() << " lexemes.\n" << std::endl;

    if (argc == 1) {
        interactive_mode(analyzer, lexicon);
    } else {
        std::vector<std::string> words;
        for (int i = 1; i < argc; i++) {
            words.push_back(argv[i]);
        }
        batch_mode(analyzer, words);
    }

    return 0;
}
