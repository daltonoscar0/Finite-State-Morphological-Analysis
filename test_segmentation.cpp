/**
 * test_segmentation.cpp: Tests for surface-piece segmentation output
 *
 * Six groups:
 *
 *   1. A table of hand-written expected segmentations, one row per case in the
 *      documented convention (regular suffixation, each orthographic
 *      alternation, prefixes, derivation, irregulars, fallback, passthrough,
 *      case preservation).
 *
 *   1b. Irregular stems under the productive rules: an irregular verb still
 *      inflects regularly where it stores nothing, and not where it does.
 *
 *   1c. Extra lexicon files, as --lexicon loads them, and the closed-class
 *      list coming through as known stems.
 *
 *   2. A property test over the whole loaded lexicon. Every lexeme is expanded
 *      into its inflected and derived surface forms using the same stem-class
 *      orthography the analyzer reverses, and the output invariants are
 *      asserted on each one. Nothing here requires the analyzer to succeed:
 *      the invariants must hold for fallback records too.
 *
 *   3. A fuzz-style group: empty strings, mixed case, digits, punctuation,
 *      very long strings, non-ASCII, and invalid UTF-8.
 *
 *   4. Text mode: whole inputs, newlines included, checked to rebuild exactly
 *      from ws_before plus the pieces.
 *
 * The invariants are re-derived here from the output rather than delegated to
 * Segmentation::check(), so a bug inside check() cannot hide a bug in the
 * segmenter. check() is then asserted to agree.
 */

#include "SYMBOLS/symbol.h"
#include "LEXICON/lexicon_fsa.h"
#include "ANALYSIS/analyzer.h"
#include "ANALYSIS/segment_driver.h"
#include "ANALYSIS/text_segmenter.h"
#include "OUTPUT/jsonl_print.h"
#include "LANGUAGES/english_configuration.h"
#include "RULES/derivation_rule.h"
#include "RULES/form_generator.h"

#include <cassert>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ── Setup ─────────────────────────────────────────────────────────────────────

struct Env {
    SymbolTable symbols;
    LexiconFSA lexicon;
    Analyzer analyzer;

    /**
     * @param extra  Extra lexicon files, as --lexicon passes them. Empty by
     *               default, which is the lexicon every other test sees.
     */
    explicit Env(const std::vector<std::string>& extra = {})
        : lexicon(&symbols), analyzer(&symbols, &lexicon) {
        EnglishConfig::initialize(symbols, lexicon, analyzer, extra);
    }
};

// ── Independent invariant verification ───────────────────────────────────────

/** utf8_len: Code-point length, counting each invalid byte as one */
static size_t utf8_len(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        bool valid = false;
        i += utf8::sequence_length(s, i, valid);
        n++;
    }
    return n;
}

static const std::set<std::string>& valid_labels() {
    static const std::set<std::string> labels = {
        "STEM", "PREFIX", "SUFFIX_INFL", "SUFFIX_PL", "SUFFIX_PAST",
        "SUFFIX_ING", "SUFFIX_3SG", "SUFFIX_COMP", "SUFFIX_SUP",
        "SUFFIX_DERIV", "IRREGULAR", "UNK"
    };
    return labels;
}

/**
 * verify: Assert every documented invariant on one record
 *
 * @param seg    The record under test
 * @param where  Label for the failure message
 */
static void verify(const Segmentation& seg, const std::string& where) {
    std::string why;

    // 1. Pieces concatenate back to the word, byte for byte
    std::string joined;
    for (const auto& p : seg.pieces) joined += p.text;
    if (joined != seg.word) {
        std::cerr << "FAIL [" << where << "] pieces do not rebuild the word:\n"
                  << "  word   = " << seg.word << "\n"
                  << "  joined = " << joined << std::endl;
        assert(false);
    }

    // 2. Offsets: code-point boundaries, right length, strictly increasing
    if (seg.offsets.size() != seg.pieces.size() + 1) {
        std::cerr << "FAIL [" << where << "] offsets length" << std::endl;
        assert(false);
    }
    if (seg.offsets.front() != 0) {
        std::cerr << "FAIL [" << where << "] offsets do not start at 0" << std::endl;
        assert(false);
    }
    if (seg.offsets.back() != utf8_len(seg.word)) {
        std::cerr << "FAIL [" << where << "] last offset " << seg.offsets.back()
                  << " != char length " << utf8_len(seg.word) << std::endl;
        assert(false);
    }
    size_t running = 0;
    for (size_t i = 0; i < seg.pieces.size(); i++) {
        if (seg.offsets[i] != running) {
            std::cerr << "FAIL [" << where << "] offset " << i << " mismatch"
                      << std::endl;
            assert(false);
        }
        running += utf8_len(seg.pieces[i].text);
        if (seg.offsets[i + 1] <= seg.offsets[i]) {
            std::cerr << "FAIL [" << where << "] offsets not increasing"
                      << std::endl;
            assert(false);
        }
    }

    // 3. Labels parallel the pieces, drawn from the documented set
    for (const auto& p : seg.pieces) {
        if (p.text.empty()) {
            std::cerr << "FAIL [" << where << "] empty piece" << std::endl;
            assert(false);
        }
        if (valid_labels().count(p.label) == 0) {
            std::cerr << "FAIL [" << where << "] unknown label " << p.label
                      << std::endl;
            assert(false);
        }
    }

    // 4. Fallback records are canonical
    if (seg.source == seg_source::FALLBACK) {
        if (seg.pieces.size() != 1 || seg.pieces[0].text != seg.word ||
            seg.pieces[0].label != "UNK" || seg.gate != seg_gate::NO_ANALYSIS ||
            !seg.lemma.empty() || seg.weight != 0.0f) {
            std::cerr << "FAIL [" << where << "] non-canonical fallback"
                      << std::endl;
            assert(false);
        }
    }
    if (seg.source != seg_source::FST &&
        seg.source != seg_source::FALLBACK &&
        seg.source != seg_source::PASSTHROUGH) {
        std::cerr << "FAIL [" << where << "] bad source" << std::endl;
        assert(false);
    }

    // 5. Gate values, and decomposed implies a real decomposition
    if (seg.gate != seg_gate::DECOMPOSED && seg.gate != seg_gate::LEXICON_STEM &&
        seg.gate != seg_gate::IRREGULAR && seg.gate != seg_gate::NO_ANALYSIS &&
        seg.gate != seg_gate::PASSTHROUGH) {
        std::cerr << "FAIL [" << where << "] bad gate " << seg.gate << std::endl;
        assert(false);
    }
    if (seg.gate == seg_gate::DECOMPOSED && seg.pieces.size() < 2) {
        std::cerr << "FAIL [" << where << "] decomposed with one piece"
                  << std::endl;
        assert(false);
    }

    // 6. Alternation names come from the documented set
    if (seg.alt != "" && seg.alt != "y_to_i" && seg.alt != "e_deletion" &&
        seg.alt != "consonant_doubling" && seg.alt != "epenthesis") {
        std::cerr << "FAIL [" << where << "] bad alt " << seg.alt << std::endl;
        assert(false);
    }

    // 7. The emitted JSON is valid UTF-8 with no raw control characters
    std::ostringstream os;
    JsonlPrinter::write(os, seg, {}, 1);
    const std::string json = os.str();
    if (!utf8::is_valid(json)) {
        std::cerr << "FAIL [" << where << "] emitted invalid UTF-8" << std::endl;
        assert(false);
    }
    for (unsigned char c : json) {
        if (c < 0x20) {
            std::cerr << "FAIL [" << where << "] raw control byte in JSON"
                      << std::endl;
            assert(false);
        }
    }
    if (json.front() != '{' || json.back() != '}') {
        std::cerr << "FAIL [" << where << "] JSON not brace-delimited"
                  << std::endl;
        assert(false);
    }

    // check() must agree with the independent verification above
    if (!seg.check(why)) {
        std::cerr << "FAIL [" << where << "] check() rejected a valid record: "
                  << why << std::endl;
        assert(false);
    }
}

// ── Group 1: table of expected segmentations ─────────────────────────────────

struct Expected {
    const char* word;
    const char* pieces;   // pipe-separated
    const char* labels;   // pipe-separated
    const char* lemma;
    const char* alt;
    const char* source;
    const char* gate;
};

static std::string join_pieces(const Segmentation& seg) {
    std::string out;
    for (size_t i = 0; i < seg.pieces.size(); i++) {
        if (i) out += "|";
        out += seg.pieces[i].text;
    }
    return out;
}

static std::string join_labels(const Segmentation& seg) {
    std::string out;
    for (size_t i = 0; i < seg.pieces.size(); i++) {
        if (i) out += "|";
        out += seg.pieces[i].label;
    }
    return out;
}

static int g_table_checks = 0;

static void expect_row(const WordSegmenter& ws, const Expected& e) {
    const Segmentation seg = ws.segment_word(e.word)[0];
    verify(seg, std::string("table:") + e.word);

    bool ok = true;
    if (join_pieces(seg) != e.pieces) ok = false;
    if (join_labels(seg) != e.labels) ok = false;
    if (seg.lemma  != e.lemma)  ok = false;
    if (seg.alt    != e.alt)    ok = false;
    if (seg.source != e.source) ok = false;
    if (seg.gate   != e.gate)   ok = false;

    if (!ok) {
        std::cerr << "FAIL: " << e.word << "\n"
                  << "  expected pieces=" << e.pieces
                  << " labels=" << e.labels
                  << " lemma=" << e.lemma << " alt=" << e.alt
                  << " source=" << e.source << " gate=" << e.gate << "\n"
                  << "  got      pieces=" << join_pieces(seg)
                  << " labels=" << join_labels(seg)
                  << " lemma=" << seg.lemma << " alt=" << seg.alt
                  << " source=" << seg.source << " gate=" << seg.gate
                  << std::endl;
        assert(false);
    }

    g_table_checks++;
    std::cout << "  ✓ " << e.word << " → " << join_pieces(seg)
              << "  [" << seg.gate
              << (seg.alt[0] ? std::string(", ") + seg.alt : std::string())
              << "]" << std::endl;
}

static void test_table(const WordSegmenter& ws) {
    std::cout << "\nTest: documented segmentation convention" << std::endl;

    const Expected rows[] = {
        // Regular suffixation, no alternation
        {"cats",    "cat|s",    "STEM|SUFFIX_PL",    "cat",  "", "fst", "decomposed"},
        {"dogs",    "dog|s",    "STEM|SUFFIX_PL",    "dog",  "", "fst", "decomposed"},
        {"walked",  "walk|ed",  "STEM|SUFFIX_PAST",  "walk", "", "fst", "decomposed"},
        {"walking", "walk|ing", "STEM|SUFFIX_ING",   "walk", "", "fst", "decomposed"},

        // y -> i: the shared prefix stops before the lemma's y
        {"cities",  "cit|ies",  "STEM|SUFFIX_PL",    "city", "y_to_i", "fst", "decomposed"},
        {"flies",   "fl|ies",   "STEM|SUFFIX_3SG",   "fly",  "y_to_i", "fst", "decomposed"},
        {"carried", "carr|ied", "STEM|SUFFIX_PAST",  "carry", "y_to_i", "fst", "decomposed"},
        {"happier", "happ|ier", "STEM|SUFFIX_COMP",  "happy", "y_to_i", "fst", "decomposed"},

        // Silent e deletion: the shared prefix stops before the lemma's e
        {"hoping",  "hop|ing",  "STEM|SUFFIX_ING",   "hope", "e_deletion", "fst", "decomposed"},

        // Consonant doubling: the doubled consonant joins the suffix
        {"stopped", "stop|ped", "STEM|SUFFIX_PAST",  "stop", "consonant_doubling", "fst", "decomposed"},
        {"running", "run|ning", "STEM|SUFFIX_ING",   "run",  "consonant_doubling", "fst", "decomposed"},
        {"biggest", "big|gest", "STEM|SUFFIX_SUP",   "big",  "consonant_doubling", "fst", "decomposed"},

        // Epenthesis: the inserted e joins the suffix
        {"kisses",  "kiss|es",  "STEM|SUFFIX_PL",    "kiss", "epenthesis", "fst", "decomposed"},
        {"boxes",   "box|es",   "STEM|SUFFIX_PL",    "box",  "epenthesis", "fst", "decomposed"},

        // Derivation
        {"darkness", "dark|ness", "STEM|SUFFIX_DERIV", "dark", "", "fst", "decomposed"},
        {"happiness", "happ|iness", "STEM|SUFFIX_DERIV", "happy", "y_to_i", "fst", "decomposed"},

        // Prefixes
        {"unkind",  "un|kind",  "PREFIX|STEM",       "kind", "", "fst", "decomposed"},
        {"rewrite", "re|write", "PREFIX|STEM",       "write", "", "fst", "decomposed"},

        // Prefix plus derivation, three pieces
        {"unhappiness", "un|happ|iness", "PREFIX|STEM|SUFFIX_DERIV", "happy", "y_to_i", "fst", "decomposed"},

        // Irregular and suppletive forms stay whole
        {"went",     "went",     "IRREGULAR", "go",    "", "fst", "irregular"},
        {"mice",     "mice",     "IRREGULAR", "mouse", "", "fst", "irregular"},
        {"sheep",    "sheep",    "IRREGULAR", "sheep", "", "fst", "irregular"},
        {"children", "children", "IRREGULAR", "child", "", "fst", "irregular"},
        {"oxen",     "oxen",     "IRREGULAR", "ox",    "", "fst", "irregular"},
        {"better",   "better",   "IRREGULAR", "good",  "", "fst", "irregular"},

        // Known stem, no affix
        {"bank",    "bank",    "STEM", "bank",  "", "fst", "lexicon_stem"},
        {"light",   "light",   "STEM", "light", "", "fst", "lexicon_stem"},
        // "quickly" is itself an adverb lexeme, and the stored whole form
        // outranks the -ly derivation, so it is reported as one stem piece.
        {"quickly", "quickly", "STEM", "quickly", "", "fst", "lexicon_stem"},

        // Case is preserved: pieces are slices of the original input
        {"Cats",    "Cat|s",   "STEM|SUFFIX_PL", "cat", "", "fst", "decomposed"},
        {"CATS",    "CAT|S",   "STEM|SUFFIX_PL", "cat", "", "fst", "decomposed"},
        {"Cities",  "Cit|ies", "STEM|SUFFIX_PL", "city", "y_to_i", "fst", "decomposed"},

        // Nothing found
        {"zxqvkj",  "zxqvkj",  "UNK", "", "", "fallback", "no_analysis"},
        {"the",     "the",     "UNK", "", "", "fallback", "no_analysis"},

        // No letters at all: passed through
        {"42",      "42",      "UNK", "", "", "passthrough", "passthrough"},
        {"!!!",     "!!!",     "UNK", "", "", "passthrough", "passthrough"},
    };

    for (const auto& row : rows) expect_row(ws, row);

    // The empty line is the one record with no pieces
    const Segmentation empty = ws.segment_word("")[0];
    verify(empty, "table:<empty>");
    assert(empty.word.empty());
    assert(empty.pieces.empty());
    assert(empty.offsets.size() == 1 && empty.offsets[0] == 0);
    assert(empty.source == seg_source::PASSTHROUGH);
    assert(empty.gate == seg_gate::PASSTHROUGH);
    g_table_checks++;
    std::cout << "  ✓ <empty line> → no pieces, offsets [0]" << std::endl;
}

// ── Group 1b: irregular stems and the productive rules ───────────────────────

/**
 * has_analysis: Does the analyzer report this lemma with this feature?
 *
 * @param env   The loaded analyzer
 * @param word  Surface form to analyze
 * @param lemma Expected lemma, or "" to accept any
 * @param tag   Expected feature tag, or "" to accept any
 */
static bool has_analysis(Env& env, const std::string& word,
                         const std::string& lemma, const std::string& tag) {
    for (const auto& a : env.analyzer.analyze(word)) {
        if (!lemma.empty() && a.stem() != lemma) continue;
        if (tag.empty()) return true;
        for (const auto& f : a.features()) {
            if (f == tag) return true;
        }
    }
    return false;
}

/**
 * test_irregular_inflection: An irregular stem still inflects regularly where
 * it stores nothing, and not where it does
 *
 * StemClass holds either the orthography or the irregularity, never both, so
 * an irregular verb used to be shut out of the productive rules entirely and
 * "breaks", "goes" and "choosing" came back with no analysis at all. The
 * orthography is now read off the spelling (RULES/orthography.h) and lexical
 * storage blocks the rule for the feature it stores, so the two halves have to
 * be tested together: widening without blocking would accept "breaked".
 */
static void test_irregular_inflection(Env& env) {
    std::cout << "\nTest: irregular stems under the productive rules"
              << std::endl;

    // Must analyze: the stem stores nothing for this feature, and the
    // spelling fixes how the suffix attaches.
    const struct { const char* word; const char* lemma; const char* tag; } present[] = {
        // Plain -s and -ing on an irregular stem
        {"breaks",    "break",  "+3SG"},
        {"breaking",  "break",  "+PRES_PART"},
        {"drinks",    "drink",  "+3SG"},
        {"drinking",  "drink",  "+PRES_PART"},
        {"puts",      "put",    "+3SG"},
        {"gets",      "get",    "+3SG"},
        // -es, because the stem ends in o or in a sibilant
        {"goes",      "go",     "+3SG"},
        {"going",     "go",     "+PRES_PART"},
        {"teaches",   "teach",  "+3SG"},
        {"catches",   "catch",  "+3SG"},
        // A silent e the irregular class hid: chooses, choosing, having
        {"chooses",   "choose", "+3SG"},
        {"choosing",  "choose", "+PRES_PART"},
        {"having",    "have",   "+PRES_PART"},
        // Short stems where the final e is not silent: being, doing
        {"being",     "be",     "+PRES_PART"},
        {"doing",     "do",     "+PRES_PART"},
        // Regular classes that the feature gate used to exclude
        {"hopes",     "hope",   "+3SG"},    // SILENT_E verb, plain -s
        {"stops",     "stop",   "+3SG"},    // DOUBLE_CONS verb, plain -s
        {"fixed",     "fix",    "+PAST"},   // SIBILANT verb, plain -ed
        {"passed",    "pass",   "+PAST"},
        // The verb "light" is regular, not silent-e
        {"lighting",  "light",  "+PRES_PART"},
        {"lighted",   "light",  "+PAST"},
        // -er on a stem that ends in y but is not Y_FINAL
        {"delayer",   "delay",  ""},
    };

    for (const auto& c : present) {
        if (!has_analysis(env, c.word, c.lemma, c.tag)) {
            std::cerr << "FAIL: " << c.word << " should analyze as " << c.lemma
                      << " " << c.tag << std::endl;
            assert(false);
        }
        std::cout << "  ✓ " << c.word << " → " << c.lemma << " " << c.tag
                  << std::endl;
    }

    // Must not analyze at all. Each of these is blocked by a stored form, or
    // by an orthography the lexicon does not determine.
    const char* absent[] = {
        // The stored past blocks the productive -ed
        "breaked", "goed", "doed", "beed", "drinked", "ringed", "choosed",
        "teached", "fighted",
        // The stored third person singular blocks the productive -s
        "haves", "bes", "dos",
        // The stored comparative and superlative block -er and -est
        "farer", "farest",
        // An irregular stem that might double its final consonant leaves -ing
        // undetermined, so the undoubled form is not accepted
        "puting", "geting",
        // y-to-i needs the Y_FINAL class, which "delay" does not have
        "delaier",
        // The former SILENT_E reading of the verb "light" read an e that is
        // not in the spelling
        "lightd", "lighing", "lighment",
    };

    for (const char* word : absent) {
        if (!env.analyzer.analyze(word).empty()) {
            std::cerr << "FAIL: " << word << " should have no analysis, got "
                      << env.analyzer.analyze(word)[0].to_string() << std::endl;
            assert(false);
        }
    }
    std::cout << "  ✓ " << (sizeof(absent) / sizeof(absent[0]))
              << " blocked forms have no analysis" << std::endl;

    // "riding" is the participle of "ride". "rid" would have to double its
    // consonant to reach it, so the undoubled reading must not appear.
    assert(has_analysis(env, "riding", "ride", "+PRES_PART"));
    assert(!has_analysis(env, "riding", "rid", "+PRES_PART"));
    std::cout << "  ✓ riding is ride, not rid" << std::endl;

    // The noun plural path is deliberately left permissive, so these keep the
    // analyses the output contract already fixed in place. See docs/LEXICON.md.
    assert(has_analysis(env, "sheeps", "sheep", "+PL"));
    assert(has_analysis(env, "mouses", "mouse", "+PL"));
    std::cout << "  ✓ noun plurals stay unblocked (sheeps, mouses)" << std::endl;
}

// ── Group 1c: extra lexicon files ────────────────────────────────────────────

/**
 * test_extra_lexicon: --lexicon loads a file, and the closed class comes
 * through as known stems
 *
 * Wiktionary's dump carries only the four open classes, so no built lexicon
 * can supply a determiner or a preposition, and those are the most frequent
 * words in any text. `data/closed_class.tsv` fills that hole. Every word in it
 * has to come out as a single known stem, which takes three things working
 * together: the FUNCTION part of speech that no rule matches, a weight above
 * the ordinary 1.0 so the function reading outranks a competing inflection
 * ("being", "used"), and the analyzer declining to strip a derivational
 * prefix off a function word ("unto", "upon").
 *
 * The file is not loaded by default, and that is asserted too: loading it
 * would change the output of inputs that already had an analysis.
 */
static void test_extra_lexicon(Env& plain) {
    std::cout << "\nTest: --lexicon and the closed-class list" << std::endl;

    std::ifstream file("data/closed_class.tsv");
    assert(file.is_open() && "run the tests from the repository root");

    std::vector<std::string> words;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        words.push_back(line.substr(0, tab));
    }
    assert(words.size() > 250);

    // Not loaded by default: the default lexicon knows none of these as a
    // function word, and "the" is the canonical no_analysis case elsewhere in
    // this file.
    for (const auto& a : plain.analyzer.analyze("the")) {
        assert(a.pos() != PartOfSpeech::FUNCTION);
    }
    assert(plain.analyzer.analyze("the").empty());

    Env loaded({"data/closed_class.tsv"});
    WordSegmenter ws(&loaded.analyzer, &loaded.lexicon);
    assert(loaded.lexicon.num_lexemes() ==
           plain.lexicon.num_lexemes() + words.size());

    size_t checked = 0;
    for (const auto& word : words) {
        const auto ranked = ws.segment_word(word);
        verify(ranked[0], "closed-class:" + word);
        if (ranked[0].gate != seg_gate::LEXICON_STEM) {
            std::cerr << "FAIL: " << word << " gate " << ranked[0].gate
                      << " pieces " << ranked[0].pieces.size() << std::endl;
            assert(false);
        }
        checked++;
    }

    std::cout << "  lexemes without the file: " << plain.lexicon.num_lexemes()
              << std::endl;
    std::cout << "  lexemes with it:          " << loaded.lexicon.num_lexemes()
              << std::endl;
    std::cout << "  ✓ " << checked
              << " closed-class words report gate lexicon_stem" << std::endl;

    // The inflected readings are still there, just ranked below
    bool found_be_ing = false;
    for (const auto& seg : ws.segment_word("being")) {
        if (seg.lemma == "be" && seg.pieces.size() == 2) found_be_ing = true;
    }
    assert(found_be_ing);
    std::cout << "  ✓ being is one piece, with be + -ing still available"
              << std::endl;

    // A function word is not a stem for prefixation or inflection
    assert(loaded.analyzer.analyze("unto").size() == 1);
    assert(!loaded.analyzer.analyze("thes").size());
    assert(!loaded.analyzer.analyze("ofs").size());
    std::cout << "  ✓ no prefixing or inflection of function words"
              << std::endl;
}

// ── Group 2: property test over the lexicon ──────────────────────────────────

static void test_property_over_lexicon(Env& env, const WordSegmenter& ws) {
    std::cout << "\nTest: invariants over every lexeme's generated forms"
              << std::endl;

    // The rules the English configuration registered, not a second copy of
    // the list: generate_forms and the analyzer must see the same objects.
    const auto& deriv_rules = env.analyzer.derivation_rules();

    size_t lexemes = 0, forms = 0, records = 0;
    size_t by_gate_decomposed = 0, by_gate_stem = 0, by_gate_irregular = 0;
    size_t by_gate_none = 0, by_source_fst = 0;
    size_t with_alt = 0, multi_piece = 0;

    // Every generated form the analyzer cannot read back, with the cause it
    // was generated from, so a regression names the gap rather than a count.
    std::vector<std::string> unreadable;

    for (const auto& lex : env.lexicon.all_lexemes()) {
        lexemes++;
        for (const auto& gf : generate_forms(lex, deriv_rules)) {
            const std::string& form = gf.surface;
            forms++;

            // Every ranked candidate must satisfy the invariants, not just
            // the primary one, since --nbest emits them all.
            const auto ranked = ws.segment_word(form);
            assert(!ranked.empty());
            for (const auto& seg : ranked) {
                records++;
                verify(seg, "property:" + form);
            }

            const Segmentation& best = ranked[0];
            if (best.source == seg_source::FST) by_source_fst++;
            if (best.gate == seg_gate::DECOMPOSED)        by_gate_decomposed++;
            else if (best.gate == seg_gate::LEXICON_STEM) by_gate_stem++;
            else if (best.gate == seg_gate::IRREGULAR)    by_gate_irregular++;
            else if (best.gate == seg_gate::NO_ANALYSIS)  by_gate_none++;
            if (best.source == seg_source::FALLBACK) {
                unreadable.push_back(form + " (" + gf.lemma + " " +
                                     pos_to_string(gf.pos) + " " +
                                     stem_class_to_string(gf.stem_class) + " " +
                                     gf.origin + ")");
            }
            if (!best.alt.empty()) with_alt++;
            if (best.pieces.size() > 1) multi_piece++;

            // Mixed case must not change the split, only the piece text
            std::string upper = form;
            for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            const auto upper_ranked = ws.segment_word(upper);
            verify(upper_ranked[0], "property-upper:" + upper);
            if (upper_ranked[0].pieces.size() == best.pieces.size()) {
                for (size_t i = 0; i < best.pieces.size(); i++) {
                    assert(upper_ranked[0].pieces[i].text.size() ==
                           best.pieces[i].text.size());
                }
            }
        }
    }

    std::cout << "  lexemes:               " << lexemes << std::endl;
    std::cout << "  generated forms:       " << forms << std::endl;
    std::cout << "  records verified:      " << records
              << " (plus " << forms << " uppercased)" << std::endl;
    std::cout << "  analyzed (source fst): " << by_source_fst
              << " of " << forms << std::endl;
    std::cout << "  gate decomposed:       " << by_gate_decomposed << std::endl;
    std::cout << "  gate lexicon_stem:     " << by_gate_stem << std::endl;
    std::cout << "  gate irregular:        " << by_gate_irregular << std::endl;
    std::cout << "  gate no_analysis:      " << by_gate_none << std::endl;
    std::cout << "  multi-piece splits:    " << multi_piece << std::endl;
    std::cout << "  with an alternation:   " << with_alt << std::endl;
    std::cout << "  ✓ all invariants hold" << std::endl;

    // The analyzer must be able to read back everything its own rules
    // generate. Anything left here is a gap in the rules or in the lexicon;
    // see docs/LEXICON.md for the documented remainder.
    if (!unreadable.empty()) {
        std::cerr << "FAIL [property] " << unreadable.size()
                  << " generated forms have no analysis:" << std::endl;
        for (size_t i = 0; i < unreadable.size() && i < 40; i++) {
            std::cerr << "  " << unreadable[i] << std::endl;
        }
        if (unreadable.size() > 40) {
            std::cerr << "  ... and " << (unreadable.size() - 40) << " more"
                      << std::endl;
        }
        std::cerr << "Run ./analyzer --selfcheck for the grouped report."
                  << std::endl;
        assert(false);
    }
    std::cout << "  ✓ every generated form reads back" << std::endl;
}

// ── Group 3: fuzz-style inputs ───────────────────────────────────────────────

static void test_fuzz(const WordSegmenter& ws) {
    std::cout << "\nTest: fuzz-style inputs" << std::endl;

    std::vector<std::string> cases = {
        // Empty and whitespace
        "", " ", "  ", "\t", " \t ",

        // Digits and numbers
        "0", "42", "3.14", "1,000", "007", "x42", "42x", "a1b2c3",

        // Punctuation only
        ".", "!!!", "...", "-", "--", "?!", "(", ")", "[]", "@#$%^&*",

        // Mixed case
        "a", "A", "I", "Cats", "CATS", "cAtS", "CiTiEs", "WENT", "McDonald",

        // Quotes, backslashes, control characters
        "a\"b", "a\\b", "\"", "\\", "a\tb", "a\nb", "a\rb",
        std::string("a\x01" "b"), std::string(1, '\0') + "x",

        // Non-ASCII, well formed
        "naïve", "café", "Ünkind", "日本語", "👍", "ʼn", "straße",

        // Invalid UTF-8
        "\xff", "\xfe\xff", "caf\xe9", "\xc3", "\xe2\x82", "a\x80" "b",
        "\xed\xa0\x80",              // surrogate, must be rejected
        "\xc0\xaf",                  // overlong
        "\xf4\x90\x80\x80",          // beyond U+10FFFF

        // Long strings
        std::string(1000, 'a'),
        std::string(5000, 'z'),
        std::string(300, 'c') + "ities",
        "un" + std::string(500, 'k') + "ind",
        std::string(2000, '7'),
        std::string(700, '\xff')
    };

    size_t checked = 0, passthrough = 0, fallback = 0, fst = 0;
    for (const auto& input : cases) {
        const auto ranked = ws.segment_word(input);
        assert(!ranked.empty());
        for (const auto& seg : ranked) {
            verify(seg, "fuzz");
            checked++;
        }
        if (ranked[0].source == seg_source::PASSTHROUGH) passthrough++;
        else if (ranked[0].source == seg_source::FALLBACK) fallback++;
        else fst++;
    }

    std::cout << "  inputs:            " << cases.size() << std::endl;
    std::cout << "  records verified:  " << checked << std::endl;
    std::cout << "  source passthrough:" << passthrough << std::endl;
    std::cout << "  source fallback:   " << fallback << std::endl;
    std::cout << "  source fst:        " << fst << std::endl;
    std::cout << "  ✓ all invariants hold" << std::endl;

    // Input that is not well-formed UTF-8 is a fallback record, never a
    // passthrough one, even when it contains no letters at all.
    const std::vector<std::string> broken = {
        "\xff", "\xfe\xff", "caf\xe9", "\xc3", "\xed\xa0\x80", "\xc0\xaf",
        "\xf4\x90\x80\x80"
    };
    for (const auto& input : broken) {
        const Segmentation seg = ws.segment_word(input)[0];
        if (seg.source != seg_source::FALLBACK) {
            std::cerr << "FAIL: invalid UTF-8 reported as " << seg.source
                      << ", expected fallback" << std::endl;
            assert(false);
        }
    }
    std::cout << "  ✓ " << broken.size()
              << " invalid UTF-8 inputs report source fallback" << std::endl;

    // Letterless but well-formed input stays passthrough
    for (const char* input : {"", "42", "...", " ", "\t"}) {
        const Segmentation seg = ws.segment_word(input)[0];
        if (seg.source != seg_source::PASSTHROUGH) {
            std::cerr << "FAIL: " << input << " reported as " << seg.source
                      << ", expected passthrough" << std::endl;
            assert(false);
        }
    }
    std::cout << "  ✓ letterless valid input reports source passthrough"
              << std::endl;
}

/** test_json_escaping: The writer's escaping, checked directly */
static void test_json_escaping() {
    std::cout << "\nTest: JSON escaping" << std::endl;

    struct { const char* in; const char* out; } rows[] = {
        {"plain",     "plain"},
        {"a\"b",      "a\\\"b"},
        {"a\\b",      "a\\\\b"},
        {"a\tb",      "a\\tb"},
        {"a\nb",      "a\\nb"},
        {"a\rb",      "a\\rb"},
        {"naïve",     "naïve"},          // valid UTF-8 passes through
        {"\xff",      "\\u00ff"},        // lone invalid byte is escaped
        {"\xc0\xaf",  "\\u00c0\\u00af"}, // overlong sequence, byte by byte
    };

    for (const auto& r : rows) {
        const std::string got = JsonlPrinter::escape(r.in);
        if (got != r.out) {
            std::cerr << "FAIL: escape mismatch, expected " << r.out
                      << " got " << got << std::endl;
            assert(false);
        }
        std::cout << "  ✓ escaped " << r.out << std::endl;
    }

    // A control byte must never survive into the output
    const std::string ctrl = JsonlPrinter::escape(std::string("a\x01" "b"));
    assert(ctrl == "a\\u0001b");
    std::cout << "  ✓ escaped a\\u0001b" << std::endl;
}

/** test_nbest_records: Alternatives are ranked and distinct */
static void test_nbest_records(const WordSegmenter& ws) {
    std::cout << "\nTest: ranked alternatives" << std::endl;

    const auto flies = ws.segment_word("flies");
    assert(flies.size() >= 2);
    for (size_t i = 1; i < flies.size(); i++) {
        assert(flies[i - 1].weight >= flies[i].weight);
        assert(!same_segmentation(flies[i - 1], flies[i]));
    }
    std::cout << "  ✓ flies has " << flies.size()
              << " distinct splits, weight-ordered" << std::endl;

    // Both readings of "unkind" describe the same split, so they collapse
    const auto unkind = ws.segment_word("unkind");
    assert(unkind.size() == 1);
    assert(unkind[0].n_analyses == 2);
    std::cout << "  ✓ unkind: 2 analyses collapse to 1 split" << std::endl;

    // Repeated calls must give the same answer
    for (const char* w : {"flies", "sheep", "unhappiness", "stopped"}) {
        const auto a = ws.segment_word(w);
        const auto b = ws.segment_word(w);
        assert(a.size() == b.size());
        for (size_t i = 0; i < a.size(); i++) {
            assert(same_segmentation(a[i], b[i]));
        }
    }
    std::cout << "  ✓ ranking is deterministic across calls" << std::endl;
}

// ── Group 4: text mode reconstruction ────────────────────────────────────────

/**
 * test_text_mode: ws_before plus the pieces must rebuild the input exactly
 *
 * This is the one invariant text mode adds. It is harder than it looks because
 * Scalpel does not emit a token for every character (a stray apostrophe in
 * "'single'" is dropped) and because whitespace at the end of a line has no
 * following token to attach to.
 */
static void test_text_mode(const WordSegmenter& ws) {
    std::cout << "\nTest: text mode rebuilds its input" << std::endl;

    LineSegmenter lines(&ws, 1);

    // Each case is a whole input, newlines included, exactly as it would
    // arrive on stdin.
    const std::vector<std::string> inputs = {
        "The cats walked quickly.\n",
        "hello, world (yes) [ok] {fine} \"quoted\" 'single'\n",
        "  leading and   multiple   spaces  \n",
        "abc def",                       // no final newline
        "cats dogs\r\nmice\r\n",         // CRLF
        "a\tb\tc\n",
        "cats\n\n\ndogs\n",              // blank lines
        "   \n\t\n",                     // whitespace only
        "",                              // empty input
        "naïve café 42\n",
        "Dr. Smith paid $3.50 for 12 apples!\n",
        "don't re-write it, isn't that right?\n",
        "cats   ",                       // whitespace at end of input
        "...!!!???\n",
        std::string(500, 'a') + " " + std::string(500, 'b') + "\n",
    };

    const std::set<std::string> kinds = {
        "WORD", "PUNCT", "NUMBER", "ABBREVIATION", "CONTRACTION", "HYPHENATED"
    };

    size_t records = 0;
    for (const auto& input : inputs) {
        // Split the input into lines the way the CLI does, tracking whether
        // each line was newline-terminated.
        std::string rebuilt;
        std::string pending;
        size_t pos = 0;

        while (pos < input.size()) {
            const size_t nl = input.find('\n', pos);
            const bool terminated = nl != std::string::npos;
            const std::string line = terminated ? input.substr(pos, nl - pos)
                                                : input.substr(pos);

            for (const auto& record : lines.segment_line(line, pending)) {
                records++;
                verify(record.seg, "text:" + line);
                if (kinds.count(record.kind) == 0) {
                    std::cerr << "FAIL: unknown kind " << record.kind
                              << std::endl;
                    assert(false);
                }
                rebuilt += record.ws_before;
                for (const auto& piece : record.seg.pieces) rebuilt += piece.text;
            }

            if (terminated) {
                pending += "\n";
                pos = nl + 1;
            } else {
                pos = input.size();
            }
        }

        if (!pending.empty()) {
            const TokenRecord tail = lines.trailing_record(pending);
            records++;
            verify(tail.seg, "text-tail");
            rebuilt += tail.ws_before;
            for (const auto& piece : tail.seg.pieces) rebuilt += piece.text;
        }

        if (rebuilt != input) {
            std::cerr << "FAIL: text mode did not rebuild its input\n"
                      << "  input   [" << input << "]\n"
                      << "  rebuilt [" << rebuilt << "]" << std::endl;
            assert(false);
        }
    }

    std::cout << "  inputs rebuilt exactly: " << inputs.size() << std::endl;
    std::cout << "  records verified:       " << records << std::endl;
    std::cout << "  ✓ ws_before plus pieces reproduces every input"
              << std::endl;
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "Loading English lexicon and rules..." << std::endl;
    Env env;
    std::cout << "Loaded " << env.lexicon.num_lexemes() << " lexemes."
              << std::endl;

    WordSegmenter ws(&env.analyzer, &env.lexicon);

    test_table(ws);
    test_irregular_inflection(env);
    test_extra_lexicon(env);
    test_json_escaping();
    test_nbest_records(ws);
    test_fuzz(ws);
    test_text_mode(ws);
    test_property_over_lexicon(env, ws);

    std::cout << "\n=== All segmentation tests passed! ===" << std::endl;
    std::cout << "Table rows checked: " << g_table_checks << std::endl;
    return 0;
}
