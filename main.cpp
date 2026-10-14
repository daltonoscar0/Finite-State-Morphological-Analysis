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
 *   ./analyzer --lexicon FILE     # Load an extra lexicon TSV, repeatable
 *   ./analyzer --selfcheck        # Forms the rules generate but cannot read back
 *   ./analyzer --derived-first    # Rank a decomposition above a stored word
 *   ./analyzer --closed-class F   # Words --derived-first never splits
 *
 * See docs/SEGMENTATION.md for the surface-piece format and docs/LEXICON.md
 * for the lexicon flags and the evaluation.
 */

#include "PIPELINE/text_pipeline.h"        // Scalpel bridge (no Tokenizer name clash)
#include "SYMBOLS/symbol.h"
#include "LEXICON/lexicon_fsa.h"
#include "ANALYSIS/analyzer.h"
#include "ANALYSIS/segment_driver.h"
#include "ANALYSIS/text_segmenter.h"
#include "OUTPUT/pretty_print.h"
#include "OUTPUT/jsonl_print.h"
#include "RULES/form_generator.h"
#include "LANGUAGES/english_configuration.h"

#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
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

// ── Self-check mode ────────────────────────────────────────────────────────

/**
 * selfcheck_mode: Can the analyzer read back everything it can generate?
 *
 * Expands every lexeme in the loaded lexicon into its surface forms (see
 * RULES/form_generator.h), runs each one back through analysis, and reports
 * the forms that come back with no analysis at all. A form that the system's
 * own rules produce and its own rules then reject is a gap in the rules or in
 * the lexicon, so this is the diagnostic for closing those gaps.
 *
 * Output is a grouped summary followed by one tab-separated line per
 * unanalyzed form: form, lemma, POS, stem class, the feature it was generated
 * for. Both go to stdout so the listing can be sorted or counted downstream.
 *
 * @param segmenter  Word to ranked surface segmentations
 * @param lexicon    The loaded lexicon, iterated lexeme by lexeme
 * @param analyzer   Supplies the registered derivational rules
 * @return           Number of forms with no analysis
 */
size_t selfcheck_mode(const WordSegmenter& segmenter, const LexiconFSA& lexicon,
                      const Analyzer& analyzer) {
    struct Row {
        std::string form, lemma, pos, stem_class, origin;
    };

    std::vector<Row> missing;
    // Grouped by cause, which is the (POS, stem class, feature) triple the
    // form was generated from: that triple is what a fix has to address.
    std::map<std::string, std::vector<std::string>> by_cause;
    size_t forms = 0;

    for (const auto& lex : lexicon.all_lexemes()) {
        for (const auto& gf : generate_forms(lex, analyzer.derivation_rules())) {
            forms++;
            const auto ranked = segmenter.segment_word(gf.surface);
            if (ranked[0].source != seg_source::FALLBACK) continue;

            const std::string pos = pos_to_string(gf.pos);
            const std::string sc  = stem_class_to_string(gf.stem_class);
            missing.push_back({gf.surface, gf.lemma, pos, sc, gf.origin});
            by_cause[pos + " " + sc + " " + gf.origin].push_back(gf.surface);
        }
    }

    std::cout << "Self-check over the loaded lexicon\n"
              << "  lexemes:          " << lexicon.num_lexemes() << "\n"
              << "  generated forms:  " << forms << "\n"
              << "  no analysis:      " << missing.size() << "\n"
              << "  causes:           " << by_cause.size() << "\n" << std::endl;

    if (!by_cause.empty()) {
        // Largest groups first, so the biggest cause is the first thing read
        std::vector<std::pair<std::string, size_t>> order;
        for (const auto& [cause, examples] : by_cause) {
            order.push_back({cause, examples.size()});
        }
        std::sort(order.begin(), order.end(),
                  [](const auto& a, const auto& b) {
                      if (a.second != b.second) return a.second > b.second;
                      return a.first < b.first;
                  });

        std::cout << "By cause (POS, stem class, feature):" << std::endl;
        for (const auto& [cause, count] : order) {
            std::cout << "  " << count << "\t" << cause << "\t";
            const auto& examples = by_cause[cause];
            for (size_t i = 0; i < examples.size() && i < 3; i++) {
                std::cout << (i ? ", " : "") << examples[i];
            }
            if (examples.size() > 3) std::cout << ", ...";
            std::cout << std::endl;
        }
        std::cout << std::endl;

        std::cout << "form\tlemma\tpos\tstem_class\torigin" << std::endl;
        for (const auto& r : missing) {
            std::cout << r.form << "\t" << r.lemma << "\t" << r.pos << "\t"
                      << r.stem_class << "\t" << r.origin << std::endl;
        }
    }

    return missing.size();
}

// ── Entry point ────────────────────────────────────────────────────────────

void print_usage() {
    std::cerr
        << "Usage:\n"
        << "  analyzer                     Interactive mode\n"
        << "  analyzer WORD...             Analyze the given words\n"
        << "  analyzer --segment-jsonl     Surface pieces, one word per stdin line\n"
        << "  analyzer --segment-text      Surface pieces, raw text lines via Scalpel\n"
        << "  analyzer --nbest K           Report K analyses per word (default 1)\n"
        << "  analyzer --selfcheck         List generated forms the analyzer cannot read back\n"
        << "  analyzer --lexicon FILE      Load an extra lexicon TSV; repeatable, merged in order\n"
        << "  analyzer --load-stats        Print lexicon size, load time and peak RSS to stderr\n"
        << "  analyzer --derived-first     Rank a decomposed reading above a stored whole word\n"
        << "  analyzer --closed-class FILE Words with no morphology: the -y rule"
           " skips them and --derived-first never splits them;"
           " default data/closed_class.tsv, empty to turn the list off\n";
}

/**
 * peak_rss_bytes: Peak resident set size of this process
 *
 * getrusage reports ru_maxrss in bytes on macOS and in kilobytes on Linux,
 * which is a documented difference between the two, so the unit is fixed here
 * rather than at the call site.
 *
 * @return Peak RSS in bytes, or 0 if it could not be read
 */
static size_t peak_rss_bytes() {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#ifdef __APPLE__
    return static_cast<size_t>(usage.ru_maxrss);
#else
    return static_cast<size_t>(usage.ru_maxrss) * 1024;
#endif
}

int main(int argc, char* argv[]) {
    bool segment_jsonl = false;
    bool segment_text  = false;
    bool selfcheck     = false;
    bool load_stats    = false;
    bool derived_first = false;
    int  nbest = 1;
    std::vector<std::string> words;
    std::vector<std::string> lexicon_files;
    std::string closed_class_file = ClosedClassSet::default_path();

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];

        if (arg == "--segment-jsonl") {
            segment_jsonl = true;
        } else if (arg == "--segment-text") {
            segment_text = true;
        } else if (arg == "--selfcheck") {
            selfcheck = true;
        } else if (arg == "--load-stats") {
            load_stats = true;
        } else if (arg == "--derived-first") {
            derived_first = true;
        } else if (arg == "--lexicon") {
            if (i + 1 >= argc) {
                std::cerr << "error: --lexicon needs a file" << std::endl;
                return 2;
            }
            lexicon_files.push_back(argv[++i]);
        } else if (arg.rfind("--lexicon=", 0) == 0) {
            lexicon_files.push_back(arg.substr(10));
        } else if (arg == "--closed-class") {
            if (i + 1 >= argc) {
                std::cerr << "error: --closed-class needs a file" << std::endl;
                return 2;
            }
            closed_class_file = argv[++i];
        } else if (arg.rfind("--closed-class=", 0) == 0) {
            closed_class_file = arg.substr(15);
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
    if (selfcheck && (segment_jsonl || segment_text)) {
        std::cerr << "error: --selfcheck and the segmentation modes are exclusive"
                  << std::endl;
        return 2;
    }
    // --derived-first re-ranks surface segmentations. The lexical analysis
    // output does not go through that ranking at all and is identical under
    // either policy, so asking for the flag outside a segmentation mode is a
    // mistake worth reporting rather than ignoring.
    if (derived_first && !segment_jsonl && !segment_text && !selfcheck) {
        std::cerr << "error: --derived-first applies to the segmentation "
                     "modes; the lexical output is the same either way"
                  << std::endl;
        return 2;
    }
    const bool segmenting = segment_jsonl || segment_text;

    // A lexicon the user named explicitly has to be there. The loader treats
    // a missing file as a warning, which is right for the optional
    // data/english_lexicon.tsv and wrong here: carrying on would silently
    // measure a different lexicon from the one that was asked for.
    for (const auto& path : lexicon_files) {
        std::ifstream probe(path);
        if (!probe.is_open()) {
            std::cerr << "error: cannot open lexicon file " << path
                      << std::endl;
            return 2;
        }
    }

    // Two things read this list, and neither can add a reading: the guard on
    // --derived-first declines to promote a decomposition of a closed-class
    // word, and the -y rule declines to produce one. It used to be read only
    // when --derived-first was on, which was right while the guard was its
    // only reader; the -y condition applies to the default ranking too, so it
    // is read either way now. The list is read independently of --lexicon,
    // since that file is opt-in and the most frequent words in English are the
    // ones that need protecting; see LEXICON/closed_class.h.
    ClosedClassSet closed_class;
    if (!closed_class_file.empty()) {
        if (!closed_class.load(closed_class_file)) {
            std::cerr << "note: cannot open " << closed_class_file
                      << "; the -y rule and --derived-first will rely on "
                         "their other conditions" << std::endl;
        }
    }

    // Initialization order matters: SymbolTable must outlive everything else,
    // and the closed-class list above outlives the analyzer that points at it.
    SymbolTable symbols;
    LexiconFSA lexicon(&symbols);
    Analyzer   analyzer(&symbols, &lexicon);
    analyzer.set_closed_class(&closed_class);

    // In segmentation mode stdout carries nothing but JSON records, so the
    // startup notices go to stderr instead. --selfcheck writes a report that
    // is meant to be filtered, so its notices go to stderr too.
    std::ostream& log = (segmenting || selfcheck) ? std::cerr : std::cout;

    log << "Loading English lexicon and rules..." << std::endl;
    const auto load_start = std::chrono::steady_clock::now();
    EnglishConfig::initialize(symbols, lexicon, analyzer, lexicon_files);
    const auto load_end = std::chrono::steady_clock::now();
    log << "Loaded " << lexicon.num_lexemes() << " lexemes.\n" << std::endl;

    if (load_stats) {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            load_end - load_start).count();
        std::cerr << "load-stats"
                  << " lexemes=" << lexicon.num_lexemes()
                  << " states=" << lexicon.num_states()
                  << " transitions=" << lexicon.num_transitions()
                  << " load_ms=" << ms
                  << " rss_mb=" << (peak_rss_bytes() / (1024 * 1024))
                  << std::endl;
    }

    if (selfcheck) {
        if (!words.empty()) {
            std::cerr << "note: --selfcheck walks the lexicon, ignoring "
                      << words.size() << " command-line word(s)" << std::endl;
        }
        WordSegmenter segmenter(&analyzer, &lexicon, derived_first,
                                &closed_class);
        selfcheck_mode(segmenter, lexicon, analyzer);
        return 0;
    }

    if (segmenting) {
        if (!words.empty()) {
            std::cerr << "note: segmentation mode reads stdin, ignoring "
                      << words.size() << " command-line word(s)" << std::endl;
        }
        WordSegmenter segmenter(&analyzer, &lexicon, derived_first,
                                &closed_class);
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
