/**
 * test_segmentation.cpp: Tests for surface-piece segmentation output
 *
 * Four groups:
 *
 *   1. A table of hand-written expected segmentations, one row per case in the
 *      documented convention (regular suffixation, each orthographic
 *      alternation, prefixes, derivation, irregulars, fallback, passthrough,
 *      case preservation).
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

#include <cassert>
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
    Env() : lexicon(&symbols), analyzer(&symbols, &lexicon) {
        EnglishConfig::initialize(symbols, lexicon, analyzer);
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

// ── Group 2: property test over the lexicon ──────────────────────────────────

/**
 * generated_forms: Surface forms for one lexeme
 *
 * The inflectional FSTs in RULES/ are built for recognition and expose no
 * generation entry point, so the forms are produced here from the lexeme's
 * stem class, which is the same orthography the analyzer reverses during
 * analysis. Irregular forms are taken straight from the lexeme, and
 * derivational forms from the DerivationRule objects the configuration
 * registers, so those two groups come from the existing rule data directly.
 */
static std::vector<std::string> generated_forms(
        const std::shared_ptr<Lexeme>& lex,
        const std::vector<std::shared_ptr<DerivationRule>>& deriv_rules) {

    const std::string s = lex->stem();
    const StemClass sc  = lex->stem_class();
    std::vector<std::string> forms;

    forms.push_back(s);  // the citation form itself

    if (s.empty()) return forms;

    const std::string no_last = s.substr(0, s.size() - 1);
    const char last = s.back();

    switch (lex->pos()) {
        case PartOfSpeech::NOUN:
            if (sc == StemClass::SIBILANT) forms.push_back(s + "es");
            else if (sc == StemClass::Y_FINAL) forms.push_back(no_last + "ies");
            else forms.push_back(s + "s");
            break;

        case PartOfSpeech::VERB:
            if (sc == StemClass::SIBILANT) {
                forms.push_back(s + "es");
                forms.push_back(s + "ed");
                forms.push_back(s + "ing");
            } else if (sc == StemClass::Y_FINAL) {
                forms.push_back(no_last + "ies");
                forms.push_back(no_last + "ied");
                forms.push_back(s + "ing");
            } else if (sc == StemClass::DOUBLE_CONS) {
                forms.push_back(s + "s");
                forms.push_back(s + last + "ed");
                forms.push_back(s + last + "ing");
            } else if (sc == StemClass::SILENT_E) {
                forms.push_back(s + "s");
                forms.push_back(s + "d");
                forms.push_back(no_last + "ed");
                forms.push_back(no_last + "ing");
            } else {
                forms.push_back(s + "s");
                forms.push_back(s + "ed");
                forms.push_back(s + "ing");
            }
            break;

        case PartOfSpeech::ADJECTIVE:
            if (sc == StemClass::Y_FINAL) {
                forms.push_back(no_last + "ier");
                forms.push_back(no_last + "iest");
            } else if (sc == StemClass::DOUBLE_CONS) {
                forms.push_back(s + last + "er");
                forms.push_back(s + last + "est");
            } else if (sc == StemClass::SILENT_E) {
                forms.push_back(no_last + "er");
                forms.push_back(no_last + "est");
            } else {
                forms.push_back(s + "er");
                forms.push_back(s + "est");
            }
            break;

        case PartOfSpeech::ADVERB:
        default:
            break;
    }

    // Stored irregular forms: mice, went, children, better
    for (const auto& [feat, form] : lex->irregular_forms_map()) {
        (void)feat;
        forms.push_back(form);
    }

    // Derivational forms, from the registered rules
    for (const auto& rule : deriv_rules) {
        if (lex->pos() != rule->source_pos()) continue;
        if (!rule->applies_to(sc)) continue;
        forms.push_back(s + rule->suffix());
        if (rule->needs_y_restoration() && last == 'y') {
            forms.push_back(no_last + "i" + rule->suffix());
        }
        if (sc == StemClass::SILENT_E) {
            forms.push_back(no_last + rule->suffix());
        }
    }

    // A prefixed variant, exercising the prefix piece on real stems
    forms.push_back("un" + s);
    forms.push_back("re" + s);

    return forms;
}

static void test_property_over_lexicon(Env& env, const WordSegmenter& ws) {
    std::cout << "\nTest: invariants over every lexeme's generated forms"
              << std::endl;

    // The same derivation rules the English configuration registers
    std::vector<std::shared_ptr<DerivationRule>> deriv_rules = {
        std::make_shared<NessRule>(),
        std::make_shared<LyRule>(),
        std::make_shared<AgentiveRule>(),
        std::make_shared<TionRule>(),
        std::make_shared<MentRule>()
    };

    size_t lexemes = 0, forms = 0, records = 0;
    size_t by_gate_decomposed = 0, by_gate_stem = 0, by_gate_irregular = 0;
    size_t by_gate_none = 0, by_source_fst = 0;
    size_t with_alt = 0, multi_piece = 0;

    for (const auto& lex : env.lexicon.all_lexemes()) {
        lexemes++;
        for (const auto& form : generated_forms(lex, deriv_rules)) {
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
    test_json_escaping();
    test_nbest_records(ws);
    test_fuzz(ws);
    test_text_mode(ws);
    test_property_over_lexicon(env, ws);

    std::cout << "\n=== All segmentation tests passed! ===" << std::endl;
    std::cout << "Table rows checked: " << g_table_checks << std::endl;
    return 0;
}
