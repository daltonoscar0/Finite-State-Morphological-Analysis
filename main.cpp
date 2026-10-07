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
 *   ./analyzer --segment-jsonl    # Surface-piece mode, one word per stdin line
 *   ./analyzer --segment-text     # Surface-piece mode, raw text lines
 */

#include "PIPELINE/text_pipeline.h"        // Scalpel bridge (no Tokenizer name clash)
#include "SYMBOLS/symbol.h"
#include "LEXICON/lexicon_fsa.h"
#include "ANALYSIS/analyzer.h"
#include "ANALYSIS/segment_driver.h"
#include "ANALYSIS/text_segmenter.h"
#include "OUTPUT/pretty_print.h"
#include "OUTPUT/jsonl_print.h"
#include "LANGUAGES/english_configuration.h"

#include <cstdlib>
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

// ── Surface segmentation mode ──────────────────────────────────────────────

/**
 * segment_jsonl_mode: Emit one JSON record per stdin line
 *
 * Reads one word per line and writes exactly one JSON object per input line,
 * in order, flushing after each one so that a long-lived parent process can
 * drive this line by line without deadlocking on a buffered pipe.
 *
 * A blank line produces a record with an empty word and no pieces.
 *
 * A trailing carriage return is stripped so that CRLF input does not leave a
 * control character inside the word.
 *
 * @param segmenter  Word to ranked surface segmentations
 * @param nbest      How many analyses to report; >1 adds "alternatives"
 */
void segment_jsonl_mode(const WordSegmenter& segmenter, int nbest) {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        auto ranked = segmenter.segment_word(line);

        std::vector<Segmentation> alternatives(ranked.begin() + 1, ranked.end());
        if (static_cast<int>(alternatives.size()) > nbest - 1) {
            alternatives.resize(nbest - 1);
        }

        JsonlPrinter::write(std::cout, ranked[0], alternatives, nbest);
        std::cout << std::endl;  // newline plus flush, one record per line
    }
}

/**
 * segment_text_mode: Emit one JSON record per token of raw text
 *
 * Reads raw text lines, tokenizes each with Scalpel, and writes one record per
 * token with the same fields as word mode plus `kind` and `ws_before`.
 *
 * Concatenating ws_before + the pieces over a line's records reproduces the
 * line exactly. Whitespace at the end of a line, and the line terminator
 * itself, are carried into the ws_before of the next token, so concatenating
 * every record in the stream reproduces the whole input. Whatever is still
 * pending at end of input becomes one final record with an empty word.
 *
 * @param lines  Line to token records
 * @param nbest  How many analyses to report per word token
 */
void segment_text_mode(const LineSegmenter& lines, int nbest) {
    std::string line;
    std::string pending;

    while (std::getline(std::cin, line)) {
        // getline sets eofbit only when it stopped at end of input rather
        // than at a newline, which is how an unterminated last line is told
        // apart from a terminated one.
        const bool terminated = !std::cin.eof();

        for (const auto& record : lines.segment_line(line, pending)) {
            JsonlPrinter::write_token(std::cout, record.seg, record.alternatives,
                                      nbest, record.kind, record.ws_before);
            std::cout << std::endl;
        }

        if (terminated) pending += "\n";
    }

    if (!pending.empty()) {
        const TokenRecord record = lines.trailing_record(pending);
        JsonlPrinter::write_token(std::cout, record.seg, record.alternatives,
                                  nbest, record.kind, record.ws_before);
        std::cout << std::endl;
    }
}

// ── Entry point ────────────────────────────────────────────────────────────

void print_usage() {
    std::cerr
        << "Usage:\n"
        << "  analyzer                     Interactive mode\n"
        << "  analyzer WORD...             Analyze the given words\n"
        << "  analyzer --segment-jsonl     Surface pieces, one word per stdin line\n"
        << "  analyzer --segment-text      Surface pieces, raw text lines via Scalpel\n"
        << "  analyzer --nbest K           Report K analyses per word (default 1)\n";
}

int main(int argc, char* argv[]) {
    bool segment_jsonl = false;
    bool segment_text  = false;
    int  nbest = 1;
    std::vector<std::string> words;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];

        if (arg == "--segment-jsonl") {
            segment_jsonl = true;
        } else if (arg == "--segment-text") {
            segment_text = true;
        } else if (arg == "--nbest") {
            if (i + 1 >= argc) {
                std::cerr << "error: --nbest needs a value" << std::endl;
                return 2;
            }
            nbest = std::atoi(argv[++i]);
        } else if (arg.rfind("--nbest=", 0) == 0) {
            nbest = std::atoi(arg.c_str() + 8);
        } else if (arg.rfind("--", 0) == 0) {
            std::cerr << "error: unknown option " << arg << std::endl;
            print_usage();
            return 2;
        } else {
            words.push_back(arg);
        }
    }

    if (nbest < 1) nbest = 1;

    if (segment_jsonl && segment_text) {
        std::cerr << "error: --segment-jsonl and --segment-text are exclusive"
                  << std::endl;
        return 2;
    }
    const bool segmenting = segment_jsonl || segment_text;

    // Initialization order matters: SymbolTable must outlive everything else
    SymbolTable symbols;
    LexiconFSA lexicon(&symbols);
    Analyzer   analyzer(&symbols, &lexicon);

    // In segmentation mode stdout carries nothing but JSON records, so the
    // startup notices go to stderr instead.
    std::ostream& log = segmenting ? std::cerr : std::cout;

    log << "Loading English lexicon and rules..." << std::endl;
    EnglishConfig::initialize(symbols, lexicon, analyzer);
    log << "Loaded " << lexicon.num_lexemes() << " lexemes.\n" << std::endl;

    if (segmenting) {
        if (!words.empty()) {
            std::cerr << "note: segmentation mode reads stdin, ignoring "
                      << words.size() << " command-line word(s)" << std::endl;
        }
        WordSegmenter segmenter(&analyzer, &lexicon);
        if (segment_jsonl) {
            segment_jsonl_mode(segmenter, nbest);
        } else {
            LineSegmenter lines(&segmenter, nbest);
            segment_text_mode(lines, nbest);
        }
        return 0;
    }

    if (words.empty()) {
        interactive_mode(analyzer, lexicon);
    } else {
        batch_mode(analyzer, words);
    }

    return 0;
}
