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
 *   1d. The --derived-first ranking, which promotes a decomposition over a
 *      stored whole word.
 *
 *   1e. The derivational suffix inventory, one case per suffix, with the
 *      stacking and the readings the rules decline.
 *
 *   1f. The guard on the --derived-first ranking, which is what keeps `is`,
 *      `his` and `number` whole while `government` and `quickly` still split.
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

    // The silent e is deleted after u as well as after a consonant, so
    // "argue" gives "arguing". It is kept after any other vowel, where it is
    // part of a digraph, so "see" gives "seeing" and not "sing".
    assert(has_analysis(env, "arguing", "argue", "+PRES_PART"));
    assert(has_analysis(env, "seeing", "see", "+PRES_PART"));
    assert(!has_analysis(env, "sing", "see", "+PRES_PART"));
    assert(!has_analysis(env, "freing", "free", "+PRES_PART"));
    std::cout << "  ✓ arguing deletes its e, seeing keeps it" << std::endl;

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

// ── Group 1d: the --derived-first policy ─────────────────────────────────────

/**
 * test_derived_first: The flag promotes a decomposition over a stored word
 *
 * The default ranks by weight, and a stored whole word at 1.0 outranks a
 * derivational reading of the same input at 0.9, so `clearly` stays one piece
 * even though `clear` is in the lexicon. `--derived-first` makes the number of
 * pieces the primary key instead, which flips exactly those cases and nothing
 * else: both orders agree below the first key, and an input with only one
 * reading has nothing to re-rank.
 *
 * Three things are asserted. The flag changes what it should: `clearly` and
 * `movement` decompose. It leaves alone what it cannot reach: `quickly` and
 * `teacher` stay whole because `quick` and `teach` are not in the built-in
 * lexicon at all, so no second reading exists to promote. And the set of
 * analyses is identical under both policies, which is what keeps the lexical
 * output byte for byte the same: the flag is a reporting order over analyses
 * the analyzer already produced.
 */
static void test_derived_first(Env& env) {
    std::cout << "\nTest: the --derived-first ranking" << std::endl;

    const WordSegmenter standard(&env.analyzer, &env.lexicon, false);
    const WordSegmenter derived(&env.analyzer, &env.lexicon, true);

    // Words with both a stored whole form and a derivational reading
    const struct { const char* word; const char* decomposed; } flips[] = {
        {"clearly",  "clear|ly"},
        {"movement", "move|ment"},
    };
    for (const auto& c : flips) {
        const auto before = standard.segment_word(c.word);
        const auto after = derived.segment_word(c.word);
        verify(before[0], std::string("standard:") + c.word);
        verify(after[0], std::string("derived:") + c.word);
        assert(before[0].pieces.size() == 1);
        assert(before[0].gate == seg_gate::LEXICON_STEM);
        assert(join_pieces(after[0]) == c.decomposed);
        assert(after[0].gate == seg_gate::DECOMPOSED);
        std::cout << "  ✓ " << c.word << ": " << join_pieces(before[0])
                  << " → " << join_pieces(after[0]) << std::endl;
    }

    // Stored words whose stem is not in the lexicon have no second reading
    for (const char* word : {"quickly", "slowly", "teacher"}) {
        const auto after = derived.segment_word(word);
        verify(after[0], std::string("derived:") + word);
        assert(after[0].pieces.size() == 1);
        assert(after[0].gate == seg_gate::LEXICON_STEM);
    }
    std::cout << "  ✓ quickly, slowly and teacher stay whole: no stem to "
                 "decompose onto" << std::endl;

    // Both policies rank the same set of analyses, so nothing is lost
    size_t compared = 0;
    for (const char* word : {"cats", "cities", "unhappiness", "went", "sheep",
                             "clearly", "movement", "quickly", "stopped",
                             "hoping", "unkind", "zxqvkj", "42", ""}) {
        const auto before = standard.segment_word(word);
        const auto after = derived.segment_word(word);
        assert(before.size() == after.size());
        std::set<std::string> a, b;
        for (const auto& s : before) a.insert(join_pieces(s) + " " + s.lemma);
        for (const auto& s : after) b.insert(join_pieces(s) + " " + s.lemma);
        assert(a == b);
        compared++;
    }
    std::cout << "  ✓ both policies rank the same analyses, over "
              << compared << " inputs" << std::endl;
}

// ── Group 1e: the derivational suffix inventory ──────────────────────────────

/** add_lexeme: Put one lexeme into an already loaded lexicon */
static void add_lexeme(Env& env, const std::string& stem, PartOfSpeech pos,
                       StemClass sc) {
    env.lexicon.insert_lexeme(std::make_shared<Lexeme>(stem, pos, sc));
}

/**
 * test_derivational_suffixes: One case per suffix in the inventory
 *
 * The stems are inserted here rather than taken from the built-in lists, so
 * that the test says which lexeme each reading needs and does not quietly
 * change meaning when the lists do. Every one of them is the only reading of
 * its word, so the default ranking reports it and the test does not depend on
 * --derived-first either.
 *
 * The negative cases are the point as much as the positive ones. A suffix the
 * measurement dropped has to stay dropped, a restriction has to keep
 * restricting, and the three things the rules decline to do are what keeps
 * precision at 0.97 on the independent gold.
 */
static void test_derivational_suffixes() {
    std::cout << "\nTest: the derivational suffix inventory" << std::endl;

    Env env;
    const PartOfSpeech N = PartOfSpeech::NOUN;
    const PartOfSpeech V = PartOfSpeech::VERB;
    const PartOfSpeech ADJ = PartOfSpeech::ADJECTIVE;

    add_lexeme(env, "read",     V,   StemClass::REG);
    add_lexeme(env, "nation",   N,   StemClass::REG);
    add_lexeme(env, "hope",     N,   StemClass::REG);
    add_lexeme(env, "care",     N,   StemClass::REG);
    add_lexeme(env, "nerve",    N,   StemClass::REG);
    add_lexeme(env, "base",     N,   StemClass::REG);
    add_lexeme(env, "act",      V,   StemClass::REG);
    add_lexeme(env, "fool",     N,   StemClass::REG);
    add_lexeme(env, "national", ADJ, StemClass::UNKNOWN);
    add_lexeme(env, "pure",     ADJ, StemClass::SILENT_E);
    add_lexeme(env, "real",     ADJ, StemClass::REG);
    add_lexeme(env, "inform",   V,   StemClass::REG);
    add_lexeme(env, "comply",   V,   StemClass::Y_FINAL);
    add_lexeme(env, "differ",   V,   StemClass::REG);
    add_lexeme(env, "child",    N,   StemClass::IRR_CHILD);
    add_lexeme(env, "friend",   N,   StemClass::REG);
    add_lexeme(env, "free",     ADJ, StemClass::REG);
    add_lexeme(env, "modern",   ADJ, StemClass::REG);
    add_lexeme(env, "class",    N,   StemClass::SIBILANT);
    add_lexeme(env, "wide",     ADJ, StemClass::SILENT_E);
    add_lexeme(env, "run",      V,   StemClass::DOUBLE_CONS);
    add_lexeme(env, "govern",   V,   StemClass::REG);
    add_lexeme(env, "thank",    V,   StemClass::REG);
    add_lexeme(env, "arm",      N,   StemClass::REG);
    add_lexeme(env, "pet",      N,   StemClass::REG);
    add_lexeme(env, "vise",     V,   StemClass::SILENT_E);
    add_lexeme(env, "emul",     V,   StemClass::REG);
    add_lexeme(env, "drunk",    N,   StemClass::REG);
    add_lexeme(env, "cloud",    N,   StemClass::REG);
    add_lexeme(env, "risk",     N,   StemClass::REG);
    add_lexeme(env, "select",   V,   StemClass::REG);

    const WordSegmenter ws(&env.analyzer, &env.lexicon);

    // One row per suffix: the surface word, the pieces, and the lemma
    const struct { const char* word; const char* pieces; const char* lemma; } table[] = {
        {"readable",    "read|able",    "read"},
        {"purity",      "pur|ity",      "pure"},      // silent e deleted
        {"hopeful",     "hope|ful",     "hope"},
        {"careless",    "care|less",    "care"},
        {"nervous",     "nerv|ous",     "nerve"},     // silent e deleted
        {"basic",       "bas|ic",       "base"},      // silent e, a regular noun
        {"selective",   "select|ive",   "select"},
        {"foolish",     "fool|ish",     "fool"},
        {"risky",       "risk|y",       "risk"},
        {"realism",     "real|ism",     "real"},
        {"realist",     "real|ist",     "real"},
        {"action",      "act|ion",      "act"},
        {"information", "inform|ation", "inform"},
        {"compliance",  "compl|iance",  "comply"},    // y to i
        {"difference",  "differ|ence",  "differ"},
        {"childhood",   "child|hood",   "child"},     // an irregular stem
        {"friendship",  "friend|ship",  "friend"},
        {"freedom",     "free|dom",     "free"},
        {"modernize",   "modern|ize",   "modern"},
        {"realise",     "real|ise",     "real"},
        {"purify",      "pur|ify",      "pure"},      // silent e deleted
        {"widen",       "wide|n",       "wide"},
        {"runner",      "run|ner",      "run"},       // consonant doubled
        {"governments", "govern|ment|s", "govern"},   // inflection outside
    };
    for (const auto& row : table) {
        const auto ranked = ws.segment_word(row.word);
        verify(ranked[0], std::string("suffix:") + row.word);
        if (join_pieces(ranked[0]) != row.pieces ||
            ranked[0].lemma != row.lemma) {
            std::cerr << "FAIL: " << row.word << " gave "
                      << join_pieces(ranked[0]) << " from " << ranked[0].lemma
                      << ", wanted " << row.pieces << " from " << row.lemma
                      << std::endl;
            assert(false);
        }
    }
    std::cout << "  ✓ " << (sizeof(table) / sizeof(table[0]))
              << " suffixes split as documented" << std::endl;

    // Two derivational suffixes, plus an inflection outside them
    const struct { const char* word; const char* pieces; } stacked[] = {
        {"carelessness", "care|less|ness"},
        {"thankfully",   "thank|ful|ly"},
    };
    for (const auto& row : stacked) {
        const auto ranked = ws.segment_word(row.word);
        verify(ranked[0], std::string("stacked:") + row.word);
        assert(join_pieces(ranked[0]) == row.pieces);
        assert(ranked[0].pieces.size() == 3);
    }
    // "nationality" has the two-layer reading as well, ranked below the
    // one-layer one because a stored "national" costs only one 0.9 factor
    bool deep = false;
    for (const auto& seg : ws.segment_word("nationality")) {
        if (join_pieces(seg) == "nation|al|ity") deep = true;
    }
    assert(deep);
    std::cout << "  ✓ care|less|ness, thank|ful|ly, nation|al|ity"
              << std::endl;

    // Suffixes the MorphoLex measurement dropped, and the restrictions it
    // added. See the per-suffix table in docs/LEXICON.md.
    const struct { const char* word; const char* why; } declined[] = {
        {"visible",     "-ible was dropped: no free base in English"},
        {"emulsion",    "-sion was dropped: -ion covers the family"},
        {"drunkard",    "-ard was dropped: it only mis-split compounds"},
        {"army",        "-y needs a stem of four letters"},
        {"petitious",   "two suffixes need a root of four letters"},
        {"nationaler",  "a derived adjective has no comparative"},
    };
    for (const auto& row : declined) {
        const auto ranked = ws.segment_word(row.word);
        verify(ranked[0], std::string("declined:") + row.word);
        if (ranked[0].pieces.size() != 1) {
            std::cerr << "FAIL: " << row.word << " gave "
                      << join_pieces(ranked[0]) << ", " << row.why
                      << std::endl;
            assert(false);
        }
    }
    std::cout << "  ✓ " << (sizeof(declined) / sizeof(declined[0]))
              << " readings the rules decline" << std::endl;

    // No rule fires on a stem the lexicon does not hold, which is what keeps
    // precision where it is. None of these stems is in the lexicon.
    for (const char* word : {"zorbable", "zorbness", "zorbity", "zorbism",
                             "zorbation", "zorbhood", "zorbify"}) {
        const auto ranked = ws.segment_word(word);
        verify(ranked[0], std::string("unknown-stem:") + word);
        assert(ranked[0].source == seg_source::FALLBACK);
    }
    std::cout << "  ✓ no suffix fires without the stem in the lexicon"
              << std::endl;
}

// ── Group 1f: the guard on --derived-first ───────────────────────────────────

/**
 * add_weighted: Put one lexeme with a stated weight into a loaded lexicon
 *
 * The built-in lists state no weight, so every one of their lexemes sits at
 * the default 1.0 and says nothing about frequency, which is exactly what
 * Lexeme::weight_known reports. The guard's frequency test therefore cannot be
 * exercised against them, and these entries supply the missing side of it. The
 * weights are the ones `~/data/morph/lexicons/en_100k.tsv` carries, so the
 * cases below are the real ones rather than invented arithmetic.
 */
static void add_weighted(Env& env, const std::string& stem, PartOfSpeech pos,
                         StemClass sc, float weight) {
    auto lex = std::make_shared<Lexeme>(stem, pos, sc);
    lex->set_weight(weight);
    env.lexicon.insert_lexeme(lex);
}

/**
 * test_derived_first_guard: A decomposition may beat a stored word only when
 * the guard allows it
 *
 * Ranking on "decomposes at all" alone split the most frequent words in
 * English into a letter and a suffix: `is` became `i|s`, `his` became `hi|s`
 * and `number` became `numb|er`, which docs/BASELINES.md measures at 1.3% of
 * the test tokens for the first two alone. Segmenter::is_promotable states the
 * four conditions; this checks each of them does its own job, that the words
 * the flag exists to split still split, and that the default ranking is not
 * touched by any of it.
 */
static void add_fixture(Env& env) {
    add_weighted(env, "i",          PartOfSpeech::NOUN,      StemClass::REG, 1.23f);
    add_weighted(env, "hi",         PartOfSpeech::NOUN,      StemClass::REG, 1.07f);
    add_weighted(env, "wa",         PartOfSpeech::NOUN,      StemClass::REG, 1.01f);
    add_weighted(env, "u",          PartOfSpeech::NOUN,      StemClass::REG, 1.08f);
    add_weighted(env, "us",         PartOfSpeech::NOUN,      StemClass::REG, 1.15f);
    add_weighted(env, "thi",        PartOfSpeech::NOUN,      StemClass::REG, 0.90f);
    // "during" as a marginal open-class sense, which is how a mined lexicon
    // carries a preposition (docs/LEXICON.md, decision 13), plus a verb "dur"
    // under it. Three letters of lemma and three of affix, so the closed-class
    // list is the only condition left to block "dur|ing".
    add_weighted(env, "during",     PartOfSpeech::NOUN,      StemClass::REG, 1.30f);
    add_weighted(env, "dur",        PartOfSpeech::VERB,      StemClass::REG, 0.90f);
    add_weighted(env, "this",       PartOfSpeech::ADVERB,    StemClass::REG, 1.21f);
    add_weighted(env, "number",     PartOfSpeech::NOUN,      StemClass::REG, 1.12f);
    add_weighted(env, "numb",       PartOfSpeech::ADJECTIVE, StemClass::REG, 0.96f);
    add_weighted(env, "numb",       PartOfSpeech::VERB,      StemClass::REG, 0.96f);
    add_weighted(env, "government", PartOfSpeech::NOUN,      StemClass::REG, 1.12f);
    add_weighted(env, "govern",     PartOfSpeech::VERB,      StemClass::REG, 0.98f);
    add_weighted(env, "player",     PartOfSpeech::NOUN,      StemClass::REG, 1.09f);
    add_weighted(env, "play",       PartOfSpeech::VERB,      StemClass::REG, 1.12f);
    add_weighted(env, "quickly",    PartOfSpeech::ADVERB,    StemClass::REG, 1.07f);
    add_weighted(env, "quick",      PartOfSpeech::ADJECTIVE, StemClass::REG, 1.07f);
    add_weighted(env, "certainly",  PartOfSpeech::ADVERB,    StemClass::REG, 1.07f);
    add_weighted(env, "certain",    PartOfSpeech::ADJECTIVE, StemClass::REG, 1.09f);
    add_weighted(env, "higher",     PartOfSpeech::NOUN,      StemClass::REG, 1.09f);
    add_weighted(env, "high",       PartOfSpeech::ADJECTIVE, StemClass::REG, 1.13f);
    // Two synthetic pairs for the lemma length condition. They differ in
    // nothing but the length of the stem, weights included, so the flag on the
    // record isolates that condition exactly. No real pair does: a two-letter
    // lemma that reaches this condition is a function word or fails the
    // frequency test as well.
    add_weighted(env, "zi",         PartOfSpeech::NOUN,      StemClass::REG, 1.20f);
    add_weighted(env, "zis",        PartOfSpeech::NOUN,      StemClass::REG, 1.10f);
    add_weighted(env, "ziq",        PartOfSpeech::NOUN,      StemClass::REG, 1.20f);
    add_weighted(env, "ziqs",       PartOfSpeech::NOUN,      StemClass::REG, 1.10f);
}

/**
 * promotable_of: The guard's verdict on one particular split of one word
 *
 * @return  1 when that split is promotable, 0 when it is blocked, and -1 when
 *          the analyzer did not produce it at all, which would mean the test
 *          below is asserting on a reading that no longer exists.
 */
static int promotable_of(const WordSegmenter& ws, const std::string& word,
                         const std::string& split) {
    for (const auto& seg : ws.segment_word(word)) {
        if (join_pieces(seg) == split) return seg.promotable ? 1 : 0;
    }
    return -1;
}

/**
 * demoted_of: Whether the layer declined one particular split of one word
 *
 * The same shape as promotable_of, for the other verdict: 1 when that split is
 * demoted, 0 when it stands, and -1 when the analyzer did not produce it.
 */
static int demoted_of(const WordSegmenter& ws, const std::string& word,
                      const std::string& split) {
    for (const auto& seg : ws.segment_word(word)) {
        if (join_pieces(seg) == split) return seg.demoted ? 1 : 0;
    }
    return -1;
}

static void test_derived_first_guard() {
    std::cout << "\nTest: the guard on --derived-first" << std::endl;

    // The closed class as a lexicon, which is what gives a function word a
    // whole-word reading for the guard to keep, plus the frequency-weighted
    // entries the fourth condition reads.
    Env env({"data/closed_class.tsv"});
    add_fixture(env);

    ClosedClassSet closed_class;
    assert(closed_class.load("data/closed_class.tsv"));
    assert(closed_class.contains("is") && closed_class.contains("His"));
    assert(!closed_class.contains("number"));

    const WordSegmenter standard(&env.analyzer, &env.lexicon, false,
                                 &closed_class);
    const WordSegmenter derived(&env.analyzer, &env.lexicon, true,
                                &closed_class);
    const WordSegmenter unguarded(&env.analyzer, &env.lexicon, true, nullptr);

    // The words the fix is for: whole under the flag, and still whole under
    // the default ranking they were always whole under.
    for (const char* word : {"is", "his", "number", "was", "this", "us"}) {
        const auto after = derived.segment_word(word);
        verify(after[0], std::string("guard:") + word);
        if (after[0].pieces.size() != 1) {
            std::cerr << "FAIL: " << word << " came out as "
                      << join_pieces(after[0]) << std::endl;
            assert(false);
        }
        assert(standard.segment_word(word)[0].pieces.size() == 1);
        std::cout << "  ✓ " << word << " stays whole" << std::endl;
    }

    // The words the flag exists for: still split, with the same boundary
    const struct { const char* word; const char* pieces; } splits[] = {
        {"government", "govern|ment"},
        {"player",     "play|er"},
        {"quickly",    "quick|ly"},
        {"certainly",  "certain|ly"},
        {"higher",     "high|er"},
    };
    for (const auto& c : splits) {
        const auto after = derived.segment_word(c.word);
        verify(after[0], std::string("guard:") + c.word);
        if (join_pieces(after[0]) != c.pieces) {
            std::cerr << "FAIL: " << c.word << " came out as "
                      << join_pieces(after[0]) << ", wanted " << c.pieces
                      << std::endl;
            assert(false);
        }
        assert(after[0].gate == seg_gate::DECOMPOSED);
        assert(after[0].promotable);
        std::cout << "  ✓ " << c.word << ": " << c.pieces << std::endl;
    }

    // Condition 1 on its own, over a lexicon that does NOT load the closed
    // class as a lexicon, so the list is the only thing that knows "during" is
    // a function word: it arrives as a marginal noun, exactly as a mined
    // lexicon carries it, and a FUNCTION reading that the guard could read off
    // the analysis is not there to be read.
    Env open_env;
    add_fixture(open_env);
    const WordSegmenter open_guarded(&open_env.analyzer, &open_env.lexicon,
                                     true, &closed_class);
    const WordSegmenter open_unguarded(&open_env.analyzer, &open_env.lexicon,
                                       true, nullptr);
    assert(promotable_of(open_unguarded, "during", "dur|ing") == 1);
    assert(promotable_of(open_guarded, "during", "dur|ing") == 0);
    assert(join_pieces(open_unguarded.segment_word("during")[0]) == "dur|ing");
    assert(join_pieces(open_guarded.segment_word("during")[0]) == "during");
    std::cout << "  ✓ condition 1: during would be dur|ing without the list"
              << std::endl;

    // Condition 2 on its own: the two pairs differ in nothing but the length
    // of the lemma, so the verdict differing is that condition and nothing
    // else. Both still come out split, because the stem outweighs the whole
    // word in both and weight decides once the guard has had its say.
    assert(promotable_of(derived, "zis", "zi|s") == 0);
    assert(promotable_of(derived, "ziqs", "ziq|s") == 1);
    std::cout << "  ✓ condition 2: a two-letter lemma is not promoted, a "
                 "three-letter one is" << std::endl;

    // Condition 4 on its own: "number" is not closed class and "numb" is four
    // letters, so the frequency test is the only thing left. "higher" is the
    // same shape, a stored noun over an adjective plus "-er", and it is
    // promoted because "high" is the more frequent of the two.
    assert(promotable_of(derived, "number", "numb|er") == 0);
    assert(promotable_of(derived, "higher", "high|er") == 1);
    assert(join_pieces(derived.segment_word("number")[0]) == "number");
    std::cout << "  ✓ condition 4: number outweighs numb and stays whole, "
                 "high outweighs higher and splits" << std::endl;

    // Condition 3 holds of every record the guard promotes: the lemma really
    // is a lexeme under the part of speech the affix attached to.
    size_t promoted = 0;
    for (const char* word : {"government", "player", "quickly", "certainly",
                             "higher", "cats", "cities", "stopped", "hoping",
                             "unhappiness", "clearly", "movement"}) {
        for (const auto& seg : derived.segment_word(word)) {
            if (!seg.promotable) continue;
            bool found = false;
            for (const auto& lex : env.lexicon.lookup_all(seg.lemma)) {
                if (lex->stem() == seg.lemma) found = true;
            }
            assert(found && seg.lemma.size() >= 3);
            promoted++;
        }
    }
    std::cout << "  ✓ condition 3: " << promoted
              << " promoted records have their lemma in the lexicon"
              << std::endl;

    // Nothing the guard does reaches the default ranking: the same records
    // come out of it whether the list is there or not.
    const WordSegmenter standard_unguarded(&env.analyzer, &env.lexicon, false,
                                           nullptr);
    size_t compared = 0;
    for (const char* word : {"is", "his", "number", "was", "this", "us", "zis",
                             "government", "player", "quickly", "certainly",
                             "higher", "cats", "cities", "went", "sheep",
                             "stopped", "hoping", "unkind", "zxqvkj", "42",
                             ""}) {
        const auto a = standard.segment_word(word);
        const auto b = standard_unguarded.segment_word(word);
        assert(a.size() == b.size());
        for (size_t i = 0; i < a.size(); i++) {
            assert(same_segmentation(a[i], b[i]));
        }
        compared++;
    }
    std::cout << "  ✓ the default ranking is unchanged, over " << compared
              << " inputs" << std::endl;

    // A blocked reading is ranked down, not withdrawn
    bool kept = false;
    for (const auto& seg : derived.segment_word("number")) {
        if (join_pieces(seg) == "numb|er") kept = true;
    }
    assert(kept);
    std::cout << "  ✓ numb|er is still reported below, under --nbest 2"
              << std::endl;
}

// ── Group 1g: the orthography fixes ──────────────────────────────────────────

/**
 * add_orthography_fixture: The lexemes the three orthography fixes turn on
 *
 * Weights again from `~/data/morph/lexicons/en_100k.tsv`, so each case below
 * is the real one. "one" is the exception: it carries no adjective reading in
 * any lexicon this project builds, and the adjective is what the -ly rule
 * needs to reach "only" at all, so it is supplied here at a weight that would
 * win if the silent-e candidate were still offered to -ly. That is the only
 * way to hold `on|ly` in place and watch it go.
 */
static void add_orthography_fixture(Env& env) {
    const auto N   = PartOfSpeech::NOUN;
    const auto V   = PartOfSpeech::VERB;
    const auto ADJ = PartOfSpeech::ADJECTIVE;
    const auto ADV = PartOfSpeech::ADVERB;
    const auto REG = StemClass::REG;
    const auto SE  = StemClass::SILENT_E;
    const auto YF  = StemClass::Y_FINAL;
    const auto DC  = StemClass::DOUBLE_CONS;

    // Fix 1: the silent e before a consonant-initial suffix
    add_weighted(env, "one",            ADJ, REG, 1.40f);
    add_weighted(env, "only",           ADV, REG, 1.16f);
    add_weighted(env, "lone",           ADJ, SE,  0.90f);
    add_weighted(env, "lonely",         ADJ, REG, 1.02f);
    add_weighted(env, "love",           N,   SE,  1.12f);
    add_weighted(env, "lovely",         ADJ, REG, 1.06f);
    add_weighted(env, "safe",           ADJ, SE,  1.12f);
    add_weighted(env, "safely",         ADV, REG, 1.05f);
    add_weighted(env, "acknowledge",    V,   SE,  0.96f);
    add_weighted(env, "abridge",        V,   SE,  0.78f);
    add_weighted(env, "due",            ADJ, SE,  1.10f);

    // Fix 2: the -y suffix. "ever" is not a noun in any real lexicon; it is
    // here so that the closed-class condition has something to block, since
    // no genuine noun of four letters plus -y spells a function word.
    add_weighted(env, "cloud",          N,   REG, 1.03f);
    add_weighted(env, "cloudy",         ADJ, YF,  0.97f);
    add_weighted(env, "rain",           N,   REG, 1.05f);
    add_weighted(env, "rainy",          ADJ, YF,  0.98f);
    add_weighted(env, "dirt",           N,   REG, 1.02f);
    add_weighted(env, "dirty",          ADJ, YF,  1.04f);
    add_weighted(env, "read",           N,   REG, 1.12f);
    add_weighted(env, "ready",          ADJ, YF,  1.10f);
    add_weighted(env, "part",           N,   REG, 1.13f);
    add_weighted(env, "party",          N,   YF,  1.11f);
    add_weighted(env, "happy",          ADJ, YF,  1.10f);
    add_weighted(env, "fun",            N,   REG, 1.10f);
    add_weighted(env, "funny",          ADJ, YF,  1.08f);
    add_weighted(env, "mare",           N,   REG, 0.97f);
    add_weighted(env, "ever",           N,   REG, 1.40f);

    // Fix 3: the doubling -er, and the stored participle
    add_weighted(env, "let",            V,   DC,  1.12f);
    add_weighted(env, "letter",         N,   REG, 1.07f);
    add_weighted(env, "letter",         V,   REG, 1.07f);
    add_weighted(env, "run",            V,   DC,  1.11f);
    add_weighted(env, "runner",         N,   REG, 1.01f);
    add_weighted(env, "mad",            ADJ, DC,  1.11f);
    add_weighted(env, "madden",         V,   REG, 0.90f);
    // "break" comes from the built-in lists with "broken" stored under
    // +PAST_PART, which is the whole point of the case. "zoke" is the same
    // shape with nothing stored, so the guard's verdict differing between
    // "broken" and "zoken" is condition 5 and nothing else.
    add_weighted(env, "broke",          ADJ, SE,  1.06f);
    add_weighted(env, "zoke",           ADJ, SE,  1.06f);
}

/** split_of: The primary surface split of one word, pieces joined with | */
static std::string split_of(const WordSegmenter& ws, const std::string& word) {
    const auto ranked = ws.segment_word(word);
    assert(!ranked.empty());
    verify(ranked[0], "orthography:" + word);
    return join_pieces(ranked[0]);
}

/** expect_split: Assert the primary split, with a readable failure */
static void expect_split(const WordSegmenter& ws, const std::string& word,
                         const std::string& wanted) {
    const std::string got = split_of(ws, word);
    if (got != wanted) {
        std::cerr << "FAIL: " << word << " came out as " << got
                  << ", wanted " << wanted << std::endl;
        assert(false);
    }
}

/**
 * test_orthography_fixes: The three fixes in docs/LEXICON.md decisions 33 to 35
 *
 * Fix 1 narrows the silent-e candidate to the vowel-initial suffixes plus an
 * exception list. Fix 2 puts two evidential conditions on -y. Fix 3 adds
 * conditions 5 and 6 to the guard on --derived-first. Each group below checks
 * the words the fix is for and the words it must not touch.
 */
static void test_orthography_fixes() {
    std::cout << "\nTest: the orthography fixes" << std::endl;

    Env env({"data/closed_class.tsv"});
    add_orthography_fixture(env);

    ClosedClassSet closed_class;
    assert(closed_class.load("data/closed_class.tsv"));
    assert(closed_class.contains("every"));
    env.analyzer.set_closed_class(&closed_class);

    const WordSegmenter standard(&env.analyzer, &env.lexicon, false,
                                 &closed_class);
    const WordSegmenter derived(&env.analyzer, &env.lexicon, true,
                                &closed_class);

    // ── Fix 1: no silent-e deletion before a consonant-initial suffix ──

    // "only" is the word the fix is for. "one" plus -ly with the e deleted
    // spells it, and the adjective in the fixture outweighs the adverb, so
    // without the fix the flag reports `on|ly`.
    for (const auto* ws : {&standard, &derived}) {
        expect_split(*ws, "only", "only");
    }
    // The e is kept, so these come out whole or as stem plus -ly with the e
    // still in the stem piece. Never `lov|ely`, never `on|ly`.
    for (const auto* ws : {&standard, &derived}) {
        for (const char* word : {"lonely", "lovely", "safely"}) {
            const std::string got = split_of(*ws, word);
            const std::string stem_ly =
                std::string(word).substr(0, std::string(word).size() - 2) +
                "|ly";
            if (got != word && got != stem_ly) {
                std::cerr << "FAIL: " << word << " came out as " << got
                          << ", wanted " << word << " or " << stem_ly
                          << std::endl;
                assert(false);
            }
        }
    }
    std::cout << "  ✓ only stays whole; lonely, lovely and safely keep the e"
              << std::endl;

    // The exception list: ten words English really does spell with the e gone
    const struct { const char* word; const char* pieces; } exceptions[] = {
        {"argument",       "argu|ment"},
        {"judgment",       "judg|ment"},
        {"acknowledgment", "acknowledg|ment"},
        {"abridgment",     "abridg|ment"},
        {"truly",          "tru|ly"},
        {"duly",           "du|ly"},
        {"wholly",         "whol|ly"},
        {"introduction",   "introduc|tion"},
        {"production",     "produc|tion"},
        {"reduction",      "reduc|tion"},
    };
    for (const auto& c : exceptions) {
        expect_split(derived, c.word, c.pieces);
        assert(derived.segment_word(c.word)[0].alt == seg_alt::E_DELETION);
    }
    std::cout << "  ✓ the ten exceptions still delete the e" << std::endl;

    // And nothing else does. These are the shape the narrowing withdrew: a
    // silent-e stem plus a consonant-initial suffix, 217 of them in the
    // regression baseline, none of them an English word.
    for (const char* word : {"safly", "wholness", "lovness", "onness",
                             "safment", "lovment"}) {
        for (const auto& seg : derived.segment_word(word)) {
            if (seg.alt == seg_alt::E_DELETION) {
                std::cerr << "FAIL: " << word << " still reads as "
                          << join_pieces(seg) << " with the e deleted"
                          << std::endl;
                assert(false);
            }
        }
    }
    // A vowel-initial suffix still deletes it, which is where the alternation
    // belongs: "love" plus -able, "safe" plus -ity.
    expect_split(derived, "lovable", "lov|able");
    assert(derived.segment_word("lovable")[0].alt == seg_alt::E_DELETION);
    std::cout << "  ✓ no other word deletes it, and -able still does"
              << std::endl;

    // ── Fix 2: the conditions on -y ──

    // The stem has to be no rarer than the whole word. "cloud" and "rain"
    // are, "dirt" is not.
    expect_split(derived, "cloudy", "cloud|y");
    expect_split(derived, "rainy", "rain|y");
    expect_split(derived, "dirty", "dirty");
    // Four letters of stem, which "fun" has not, and a stem in the lexicon,
    // which "happ" is not. Both words come out whole without the frequency
    // test having to be asked.
    expect_split(derived, "funny", "funny");
    expect_split(derived, "happy", "happy");
    // "read" and "part" are the more frequent words, so the frequency test
    // passes and these split. docs/LEXICON.md decision 34 says why no
    // frequency test can hold them whole.
    expect_split(derived, "ready", "read|y");
    expect_split(derived, "party", "part|y");
    // "Mary" is fix 1, not fix 2: it was "mare" plus -y with the e deleted,
    // and -y is consonant-initial, so the candidate is simply not offered.
    expect_split(derived, "Mary", "Mary");
    expect_split(derived, "mary", "mary");
    std::cout << "  ✓ -y: cloudy and rainy split; dirty, funny, happy and "
                 "Mary stay whole; ready and party still split" << std::endl;

    // The closed-class condition, isolated from the guard's own condition 1.
    // Both segmenters below carry the list for the guard; only one analyzer
    // carries it for the rule, and the reading is produced in exactly the
    // case where the rule does not have it.
    Env open_env({"data/closed_class.tsv"});
    add_orthography_fixture(open_env);
    const WordSegmenter open_derived(&open_env.analyzer, &open_env.lexicon,
                                     true, &closed_class);
    auto reads_ever = [](const WordSegmenter& ws) {
        for (const auto& seg : ws.segment_word("every")) {
            if (seg.lemma == "ever") return true;
        }
        return false;
    };
    assert(reads_ever(open_derived));
    assert(!reads_ever(derived));
    expect_split(derived, "every", "every");
    std::cout << "  ✓ -y: every has an ever|y reading only without the "
                 "closed-class list" << std::endl;

    // ── Fix 3: conditions 5 and 6 of the guard ──

    // Condition 6. "letter" and "let" differ in nothing the first five
    // conditions can see: "let" is a DOUBLE_CONS verb of three letters, the
    // affix is two characters, and "let" is the more frequent word. What
    // separates it from "runner" is the stored verb "letter".
    expect_split(derived, "letter", "letter");
    expect_split(derived, "runner", "run|ner");
    assert(promotable_of(derived, "letter", "let|ter") == 0);
    assert(promotable_of(derived, "runner", "run|ner") == 1);
    // A suffix that makes verbs is exempt: a stored verb is the expected
    // reading there, not evidence against the derivation.
    expect_split(derived, "madden", "mad|den");
    std::cout << "  ✓ letter stays whole, runner and madden still split"
              << std::endl;

    // Condition 5. "broken" is stored as the participle of "break", so no
    // reading the rules assembled is promoted over it, and the word comes out
    // whole with "break" as its lemma. "zoken" is the same shape with nothing
    // stored, and it is promoted, which is the condition isolated: the two
    // adjectives carry the same weight and the same stem class.
    expect_split(derived, "broken", "broken");
    assert(derived.segment_word("broken")[0].lemma == "break");
    assert(promotable_of(derived, "broken", "broke|n") == 0);
    assert(promotable_of(derived, "zoken", "zoke|n") == 1);
    expect_split(derived, "zoken", "zoke|n");
    // The blocked reading is ranked down, not withdrawn
    bool kept = false;
    for (const auto& seg : derived.segment_word("broken")) {
        if (join_pieces(seg) == "broke|n") kept = true;
    }
    assert(kept);
    std::cout << "  ✓ broken is the participle of break, and broke|n is "
                 "still reported below" << std::endl;

    // Neither new condition reaches the default ranking.
    const WordSegmenter standard_unguarded(&env.analyzer, &env.lexicon, false,
                                           nullptr);
    for (const char* word : {"letter", "runner", "madden", "broken", "only",
                             "cloudy", "dirty", "ready", "argument"}) {
        const auto a = standard.segment_word(word);
        const auto b = standard_unguarded.segment_word(word);
        assert(a.size() == b.size());
        for (size_t i = 0; i < a.size(); i++) {
            assert(same_segmentation(a[i], b[i]));
        }
    }
    std::cout << "  ✓ the default ranking is unchanged by conditions 5 and 6"
              << std::endl;
}

// ── Group 1h: the guard on prefix readings ───────────────────────────────────

/**
 * add_prefix_fixture: The lexemes the prefix guard turns on
 *
 * Weights are the ones in `~/data/morph/lexicons/en_100k.tsv`, so every case
 * below is the real one rather than a shape chosen to pass. The parts of
 * speech are the ones that file lists, which is why `cord`, `port` and `sent`
 * arrive under more categories than a dictionary would give them: a mined
 * lexicon lists a word under every sense Wiktionary has for it, and the guard
 * has to work on that.
 */
static void add_prefix_fixture(Env& env) {
    const auto N   = PartOfSpeech::NOUN;
    const auto V   = PartOfSpeech::VERB;
    const auto ADJ = PartOfSpeech::ADJECTIVE;
    const auto REG = StemClass::REG;
    const auto SE  = StemClass::SILENT_E;
    const auto YF  = StemClass::Y_FINAL;

    // co- on a two-letter base: the length condition
    add_weighted(env, "come",        V,   SE,  1.13f);
    add_weighted(env, "me",          N,   REG, 1.19f);

    // The frequency condition, in both directions
    add_weighted(env, "understand",  V,   REG, 1.10f);
    add_weighted(env, "stand",       N,   REG, 1.09f);
    add_weighted(env, "stand",       V,   REG, 1.09f);
    add_weighted(env, "present",     N,   REG, 1.09f);
    add_weighted(env, "present",     V,   REG, 1.09f);
    add_weighted(env, "sent",        N,   REG, 1.09f);
    add_weighted(env, "report",      N,   REG, 1.10f);
    add_weighted(env, "report",      V,   REG, 1.10f);
    add_weighted(env, "port",        N,   REG, 1.05f);
    add_weighted(env, "port",        V,   REG, 1.05f);
    add_weighted(env, "record",      N,   REG, 1.09f);
    add_weighted(env, "record",      V,   REG, 1.09f);
    add_weighted(env, "cord",        N,   REG, 1.00f);
    add_weighted(env, "cord",        V,   REG, 1.00f);
    add_weighted(env, "return",      N,   REG, 1.09f);
    add_weighted(env, "return",      V,   REG, 1.09f);
    add_weighted(env, "turn",        N,   REG, 1.10f);
    add_weighted(env, "turn",        V,   REG, 1.10f);

    // A prefix with a suffix outside it, where the frequency condition reads
    // the prefixed lemma: "department" against "depart", "unhappiness"
    // against "unhappy"
    add_weighted(env, "department",  N,   REG, 1.09f);
    add_weighted(env, "depart",      N,   REG, 0.98f);
    add_weighted(env, "depart",      V,   REG, 0.98f);
    add_weighted(env, "part",        N,   REG, 1.13f);
    add_weighted(env, "part",        V,   REG, 1.13f);
    add_weighted(env, "unhappiness", N,   StemClass::SIBILANT, 0.94f);
    add_weighted(env, "unhappy",     ADJ, YF,  1.00f);
    add_weighted(env, "happy",       ADJ, YF,  1.10f);

    // The readings the guard has to leave alone
    add_weighted(env, "rewrite",     V,   SE,  0.96f);
    add_weighted(env, "write",       V,   SE,  1.08f);
    add_weighted(env, "nonsense",    N,   REG, 1.02f);
    add_weighted(env, "sense",       N,   SE,  1.09f);
    add_weighted(env, "disagree",    V,   REG, 1.01f);
    add_weighted(env, "agree",       V,   REG, 1.07f);
    add_weighted(env, "unkind",      ADJ, REG, 0.92f);
    add_weighted(env, "kind",        ADJ, REG, 1.11f);
    add_weighted(env, "kind",        N,   REG, 1.11f);
    add_weighted(env, "war",         N,   REG, 1.11f);

    // The part of speech condition, isolated. "re-" makes verbs out of verbs,
    // so a noun-only base is not one it attaches to. Both entries are commoner
    // than the whole word, so frequency cannot be what decides either pair.
    add_weighted(env, "requs",       N,   REG, 0.90f);
    add_weighted(env, "qus",         N,   REG, 1.20f);
    add_weighted(env, "reqvs",       N,   REG, 0.90f);
    add_weighted(env, "qvs",         V,   REG, 1.20f);

    // The closed-class condition. "non" plus the preposition "for" is the
    // shape, with "for" supplied as the marginal noun a mined lexicon carries
    // so that the list is the only thing that knows better.
    add_weighted(env, "nonfor",      N,   REG, 0.90f);
    add_weighted(env, "for",         N,   REG, 1.25f);

    // The stem convention the fix must not touch
    add_weighted(env, "beauty",      N,   YF,  1.07f);
    add_weighted(env, "busy",        ADJ, YF,  1.08f);

    // The two-letter noun under "SIS", and the plural it used to license
    add_weighted(env, "si",          N,   REG, 1.01f);

    // Condition 7, the stored base. `can` is carried with PRES_PART=able in
    // every mined lexicon this project builds, a Wiktionary artifact, so
    // `able` is in the irregular index and `unable` reads as un- plus the
    // participle of `can` at 1.18 against `un|able` at 1.11. `axle` is the
    // same pair with nothing stored under it, identical in every weight, so
    // the verdict differing between the two is condition 7 alone.
    auto can = std::make_shared<Lexeme>("can", V, StemClass::DOUBLE_CONS);
    can->set_weight(1.18f);
    can->add_irregular_form(Lexeme::Feature::PRES_PART, "able");
    env.lexicon.insert_lexeme(can);
    add_weighted(env, "able",        ADJ, REG, 1.11f);
    add_weighted(env, "unable",      ADJ, REG, 1.05f);
    add_weighted(env, "axle",        ADJ, REG, 1.11f);
    add_weighted(env, "unaxle",      ADJ, REG, 1.05f);
    // The two words the plural conditions decide, at their real weights.
    // `clothes` is the -es condition: `cloth` does not end in a sibilant or an
    // o, so the only plural reading left is the one through the verb
    // `clothe`. `married` is condition 4 of the promotion guard, where the
    // stored adjective outweighs the verb it would be derived from.
    add_weighted(env, "clothe",      V,   SE,  0.91f);
    add_weighted(env, "cloth",       N,   REG, 1.00f);
    add_weighted(env, "marry",       V,   YF,  1.03f);
    add_weighted(env, "married",     N,   REG, 1.08f);

    // The index was built when the Env was constructed, before any of the
    // above existed. Rebuilding appends rather than replaces, so the built-in
    // forms arrive a second time; identical analyses are deduplicated in
    // Analyzer::analyze, and the only reader that could care, is_irregular_form,
    // stops at the first match.
    env.analyzer.build_irregular_index();

    // The readings the margin withdraws, and the one the part of speech
    // condition withdraws. Weights again from en_100k.tsv, so the gap between
    // each word and its base is the real one; docs/LEXICON.md tabulates the
    // margin at which each of these comes back.
    add_weighted(env, "unusual",     ADJ, REG, 1.03f);
    add_weighted(env, "usual",       ADJ, REG, 1.05f);
    add_weighted(env, "repair",      N,   REG, 1.03f);
    add_weighted(env, "repair",      V,   REG, 1.03f);
    add_weighted(env, "pair",        N,   REG, 1.05f);
    add_weighted(env, "pair",        V,   REG, 1.05f);
    add_weighted(env, "review",      N,   REG, 1.08f);
    add_weighted(env, "review",      V,   REG, 1.08f);
    add_weighted(env, "view",        N,   REG, 1.09f);
    add_weighted(env, "view",        V,   REG, 1.09f);
    add_weighted(env, "reserve",     N,   REG, 1.04f);
    add_weighted(env, "reserve",     V,   SE,  1.04f);
    add_weighted(env, "serve",       N,   REG, 1.06f);
    add_weighted(env, "serve",       V,   SE,  1.06f);
    add_weighted(env, "upstairs",    ADJ, REG, 0.99f);
    add_weighted(env, "upstairs",    N,   REG, 0.99f);
    add_weighted(env, "upstair",     ADJ, REG, 0.80f);
    add_weighted(env, "stair",       N,   REG, 0.94f);
}

/**
 * test_prefix_guard: Four conditions on a prefix reading, and what they fix
 *
 * Prefix stripping had no guard of its own: the analyzer tries fifteen
 * prefixes against every analysis of the remainder, and the reading it built
 * carried the base's weight, so `co|me` was reported at the weight of the
 * pronoun `me` and beat the stored verb `come` on either ranking. That is
 * 17,000 test tokens in docs/BASELINES.md, more than the four words the
 * previous round fixed put together.
 *
 * Segmenter::prefix_is_licensed states the conditions and
 * Segmentation::demoted is where the verdict goes. Unlike the promotion
 * guard, the verdict is read by both rankings, because weight cannot settle a
 * pair where one side's weight belongs to a different word.
 */
static void test_prefix_guard() {
    std::cout << "\nTest: the guard on prefix readings" << std::endl;

    Env env({"data/closed_class.tsv"});
    add_prefix_fixture(env);

    ClosedClassSet closed_class;
    assert(closed_class.load("data/closed_class.tsv"));

    const WordSegmenter standard(&env.analyzer, &env.lexicon, false,
                                 &closed_class);
    const WordSegmenter derived(&env.analyzer, &env.lexicon, true,
                                &closed_class);

    // The words the fix is for. Each comes out whole, or with the one suffix
    // boundary that is right, on both rankings.
    const struct { const char* word; const char* pieces; } fixed[] = {
        {"come",       "come"},
        {"Come",       "Come"},
        {"understand", "understand"},
        {"present",    "present"},
        {"department", "depart|ment"},
        {"report",     "report"},
        {"record",     "record"},
        {"return",     "return"},
    };
    for (const auto& c : fixed) {
        expect_split(derived, c.word, c.pieces);
        const std::string plain = split_of(standard, c.word);
        if (plain.find('|') != std::string::npos && plain != c.pieces) {
            std::cerr << "FAIL: " << c.word << " is " << plain
                      << " on the default ranking" << std::endl;
            assert(false);
        }
        std::cout << "  ✓ " << c.word << " → " << c.pieces
                  << " on both rankings" << std::endl;
    }

    // The readings a prefix guard must not withdraw
    const struct { const char* word; const char* pieces; } kept[] = {
        {"unhappy",     "un|happy"},
        {"rewrite",     "re|write"},
        {"nonsense",    "non|sense"},
        {"disagree",    "dis|agree"},
        {"unkind",      "un|kind"},
        {"prewar",      "pre|war"},
    };
    for (const auto& c : kept) {
        expect_split(derived, c.word, c.pieces);
        std::cout << "  ✓ " << c.word << " still splits: " << c.pieces
                  << std::endl;
    }

    // A prefix reading the guard licenses is promoted, which is the policy
    // the measurement in docs/LEXICON.md selected. `un|happ|iness` carries
    // both of the boundaries MorphoLex marks, and it wins on either ranking:
    // on the default one by weight, since it carries the weight of `happy`
    // against the weight of `unhappy`, and under the flag by promotion.
    assert(demoted_of(derived, "unhappiness", "un|happ|iness") == 0);
    assert(promotable_of(derived, "unhappiness", "un|happ|iness") == 1);
    expect_split(standard, "unhappiness", "un|happ|iness");
    expect_split(derived, "unhappiness", "un|happ|iness");
    std::cout << "  ✓ unhappiness: un|happ|iness on both rankings"
              << std::endl;

    // Condition 1, the length of the base, isolated on the word it was
    // written for: "me" is two letters, and every other condition passes,
    // since "come" is rarer than "me" and co- does attach to a noun.
    assert(demoted_of(derived, "come", "co|me") == 1);
    std::cout << "  ✓ condition 1: co|me is demoted on a two-letter base"
              << std::endl;

    // Condition 2, the part of speech, isolated. The two synthetic words
    // differ in nothing but the category of the base.
    assert(demoted_of(derived, "requs", "re|qus") == 1);
    assert(demoted_of(derived, "reqvs", "re|qvs") == 0);
    expect_split(derived, "requs", "requs");
    expect_split(derived, "reqvs", "re|qvs");
    std::cout << "  ✓ condition 2: re- takes a verb, not a noun" << std::endl;

    // Condition 3, the closed class, isolated, over a lexicon that does not
    // load the closed class as a lexicon: the base arrives as the marginal
    // noun a mined lexicon carries, so there is no FUNCTION reading to read
    // off the analysis and the list is the only thing that knows better. The
    // base is three letters and far commoner than the whole word, so no other
    // condition can be what blocks it.
    Env open_env;
    add_prefix_fixture(open_env);
    const WordSegmenter open_guarded(&open_env.analyzer, &open_env.lexicon,
                                     true, &closed_class);
    const WordSegmenter open_unguarded(&open_env.analyzer, &open_env.lexicon,
                                       true, nullptr);
    assert(demoted_of(open_unguarded, "nonfor", "non|for") == 0);
    assert(demoted_of(open_guarded, "nonfor", "non|for") == 1);
    assert(join_pieces(open_unguarded.segment_word("nonfor")[0]) == "non|for");
    assert(join_pieces(open_guarded.segment_word("nonfor")[0]) == "nonfor");
    std::cout << "  ✓ condition 3: non|for needs the closed-class list to go"
              << std::endl;

    // Condition 4, the frequency margin, isolated. Both pairs are a listed
    // prefix on a listed base of the right category, and the only difference
    // is that the base clears the margin in one and not the other.
    assert(demoted_of(derived, "return", "re|turn") == 1);
    assert(demoted_of(derived, "rewrite", "re|write") == 0);
    assert(demoted_of(derived, "department", "de|part|ment") == 1);
    assert(demoted_of(derived, "unhappiness", "un|happ|iness") == 0);
    std::cout << "  ✓ condition 4: return and department go, rewrite and "
                 "unhappiness stand" << std::endl;

    // The words docs/LEXICON.md attributes to a condition each, at the margin
    // of a quarter of a Zipf point the sweep settled on. `review` is inside
    // the margin at 0.13 Zipf and goes; `unusual`, `repair` and `reserve` are
    // outside it at 0.27 and stand. `upstairs` goes to the part of speech
    // condition, not the margin: the material after `up-` is not a lexeme and
    // the lemma `stair` is a noun where `up-` takes a verb.
    const struct { const char* word; const char* split; bool kept; }
    margin_cases[] = {
        {"review",   "re|view",    false},
        {"upstairs", "up|stair|s", false},
        {"unusual",  "un|usual",   true},
        {"repair",   "re|pair",    true},
        {"reserve",  "re|serve",   true},
    };
    for (const auto& c : margin_cases) {
        assert(demoted_of(derived, c.word, c.split) == (c.kept ? 0 : 1));
        expect_split(derived, c.word, c.kept ? c.split : c.word);
        expect_split(standard, c.word, c.kept ? c.split : c.word);
        std::cout << "  ✓ " << c.word << " → "
                  << (c.kept ? c.split : c.word) << std::endl;
    }

    // Condition 7: a promoted prefix reading does not outrank a stored
    // irregular form of the base. The fixture is the real one, a mined
    // lexicon carrying `can` with PRES_PART=able, which puts `able` in the
    // irregular index and reads `unable` as un- plus the participle of `can`
    // at 1.18. Without the condition the promotion key puts `un|able` at 1.11
    // above it, which is condition 5 defeated one layer in.
    assert(demoted_of(derived, "unable", "un|able") == 0);
    assert(promotable_of(derived, "unable", "un|able") == 0);
    expect_split(derived, "unable", "unable");
    expect_split(standard, "unable", "unable");
    assert(derived.segment_word("unable")[0].gate == seg_gate::IRREGULAR);
    // The same reading with nothing stored under it is promoted, so the
    // verdict differing between these two is condition 7 and nothing else.
    assert(promotable_of(derived, "unaxle", "un|axle") == 1);
    expect_split(derived, "unaxle", "un|axle");
    std::cout << "  ✓ unable stays the participle of can, un|axle is promoted"
              << std::endl;

    // A demoted reading is ranked down, not withdrawn
    bool kept_below = false;
    for (const auto& seg : derived.segment_word("come")) {
        if (join_pieces(seg) == "co|me") kept_below = true;
    }
    assert(kept_below);
    std::cout << "  ✓ co|me is still reported below, under --nbest 2"
              << std::endl;

    // The stem convention is untouched: docs/SEGMENTATION.md puts the
    // displaced i in the piece after the stem, and neither guard moves it.
    expect_split(derived, "beautiful", "beaut|iful");
    expect_split(derived, "business", "bus|iness");
    std::cout << "  ✓ beaut|iful and bus|iness are unchanged" << std::endl;

    // A plural needs a stem of three letters, and a short word in capitals
    // with nothing but a demoted reading behind it comes out whole.
    assert(demoted_of(derived, "sis", "si|s") == 1);
    for (const auto& ws : {std::ref(standard), std::ref(derived)}) {
        const auto sis = ws.get().segment_word("SIS");
        verify(sis[0], "prefix:SIS");
        assert(join_pieces(sis[0]) == "SIS");
        assert(sis[0].gate == seg_gate::NO_ANALYSIS);
        assert(sis[0].source == seg_source::FALLBACK);
        // The reading is kept behind it
        assert(sis.size() > 1 && join_pieces(sis[1]) == "SI|S");
        // A reading the guards left alone is reported as it always was
        assert(join_pieces(ws.get().segment_word("CATS")[0]) == "CAT|S");
        assert(join_pieces(ws.get().segment_word("cats")[0]) == "cat|s");
    }
    std::cout << "  ✓ SIS comes out whole with gate no_analysis, CAT|S does "
                 "not move" << std::endl;

    // The -es plural has an orthographic trigger, so the corpus name is not a
    // plural of "child" and "clothes" is not a plural of "cloth". A plain -s
    // is accepted on any noun, as it always was, and -es after a sibilant or
    // a final o is what the rule is for.
    for (const char* word : {"CHILDES", "childes"}) {
        const auto rec = env.analyzer.analyze(word);
        assert(rec.empty());
    }
    const auto childes = derived.segment_word("CHILDES");
    verify(childes[0], "prefix:CHILDES");
    assert(join_pieces(childes[0]) == "CHILDES");
    assert(childes[0].gate == seg_gate::NO_ANALYSIS);
    expect_split(derived, "boxes", "box|es");
    expect_split(derived, "kisses", "kiss|es");
    expect_split(derived, "cats", "cat|s");
    std::cout << "  ✓ CHILDES has no analysis, box|es and kiss|es stand"
              << std::endl;

    // Every word the prefix round and the reader round name in a document,
    // with what it comes out as on each ranking. The two columns differ for
    // none of them, which is itself the point: the guard and the plural
    // conditions are read by both rankings, so the only words whose reading
    // depends on the flag are the ones promotion settles.
    const struct { const char* word; const char* plain; const char* flagged; }
    named[] = {
        {"come",       "come",       "come"},
        {"present",    "present",    "present"},
        {"return",     "return",     "return"},
        {"unusual",    "un|usual",   "un|usual"},
        {"repair",     "re|pair",    "re|pair"},
        {"review",     "review",     "review"},
        {"reserve",    "re|serve",   "re|serve"},
        {"upstairs",   "upstairs",   "upstairs"},
        {"understand", "understand", "understand"},
        {"SIS",        "SIS",        "SIS"},
        {"CHILDES",    "CHILDES",    "CHILDES"},
        {"clothes",    "clothe|s",   "clothe|s"},
        {"married",    "married",    "married"},
    };
    for (const auto& c : named) {
        expect_split(standard, c.word, c.plain);
        expect_split(derived, c.word, c.flagged);
        std::cout << "  ✓ " << c.word << ": " << c.plain << " by default, "
                  << c.flagged << " under the flag" << std::endl;
    }
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
    test_derived_first(env);
    test_derivational_suffixes();
    test_derived_first_guard();
    test_orthography_fixes();
    test_prefix_guard();
    test_json_escaping();
    test_nbest_records(ws);
    test_fuzz(ws);
    test_text_mode(ws);
    test_property_over_lexicon(env, ws);

    std::cout << "\n=== All segmentation tests passed! ===" << std::endl;
    std::cout << "Table rows checked: " << g_table_checks << std::endl;
    return 0;
}
