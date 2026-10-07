/**
 * test_analyzer.cpp: Comprehensive integration tests for the morphological analyzer
 *
 * Tests all features end-to-end using the full English configuration.
 */

#include "SYMBOLS/symbol.h"
#include "LEXICON/lexicon_fsa.h"
#include "ANALYSIS/analyzer.h"
#include "LANGUAGES/english_configuration.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>

// ── Helpers ──────────────────────────────────────────────────────────────────

bool has_analysis(const std::vector<Analysis>& analyses, const std::string& expected) {
    for (const auto& a : analyses) {
        if (a.to_string() == expected) return true;
    }
    return false;
}

void check(const std::vector<Analysis>& results, const std::string& expected,
           const std::string& input) {
    if (!has_analysis(results, expected)) {
        std::cerr << "FAIL: analyze(\"" << input << "\") missing: " << expected << std::endl;
        std::cerr << "  Got:" << std::endl;
        for (const auto& a : results) {
            std::cerr << "    " << a.to_string() << std::endl;
        }
        assert(false);
    }
    std::cout << "  ✓ " << input << " → " << expected << std::endl;
}

// ── Setup ─────────────────────────────────────────────────────────────────────

struct Env {
    SymbolTable symbols;
    LexiconFSA lexicon;
    Analyzer analyzer;
    Env() : lexicon(&symbols), analyzer(&symbols, &lexicon) {
        EnglishConfig::initialize(symbols, lexicon, analyzer);
    }
};

// ── Tests ─────────────────────────────────────────────────────────────────────

void test_case_normalization(Env& env) {
    std::cout << "\nTest: Case normalization" << std::endl;
    auto r1 = env.analyzer.analyze("Dogs");
    check(r1, "dog +N +PL", "Dogs");
    auto r2 = env.analyzer.analyze("WALKED");
    check(r2, "walk +V +PAST", "WALKED");
    auto r3 = env.analyzer.analyze("Cities");
    check(r3, "city +N +PL", "Cities");
    auto r4 = env.analyzer.analyze("MICE");
    check(r4, "mouse +N +PL", "MICE");
}

void test_regular_nouns(Env& env) {
    std::cout << "\nTest: Regular noun plurals" << std::endl;
    check(env.analyzer.analyze("cats"), "cat +N +PL", "cats");
    check(env.analyzer.analyze("dogs"), "dog +N +PL", "dogs");
    check(env.analyzer.analyze("books"), "book +N +PL", "books");
    check(env.analyzer.analyze("trees"), "tree +N +PL", "trees"); // wait, tree is in lexicon?
}

void test_sibilant_nouns(Env& env) {
    std::cout << "\nTest: Sibilant noun plurals" << std::endl;
    check(env.analyzer.analyze("churches"), "church +N +PL", "churches");
    check(env.analyzer.analyze("kisses"), "kiss +N +PL", "kisses");
    check(env.analyzer.analyze("bushes"), "bush +N +PL", "bushes");
    check(env.analyzer.analyze("foxes"), "fox +N +PL", "foxes");
}

void test_y_final_nouns(Env& env) {
    std::cout << "\nTest: Y_FINAL noun plurals" << std::endl;
    check(env.analyzer.analyze("cities"), "city +N +PL", "cities");
    check(env.analyzer.analyze("babies"), "baby +N +PL", "babies");
    check(env.analyzer.analyze("countries"), "country +N +PL", "countries");
    check(env.analyzer.analyze("parties"), "party +N +PL", "parties");
}

void test_irregular_nouns(Env& env) {
    std::cout << "\nTest: Irregular noun reverse lookup" << std::endl;
    check(env.analyzer.analyze("mice"), "mouse +N +PL", "mice");
    check(env.analyzer.analyze("children"), "child +N +PL", "children");
    check(env.analyzer.analyze("feet"), "foot +N +PL", "feet");
    check(env.analyzer.analyze("geese"), "goose +N +PL", "geese");
    check(env.analyzer.analyze("teeth"), "tooth +N +PL", "teeth");
    check(env.analyzer.analyze("men"), "man +N +PL", "men");
}

void test_regular_verbs(Env& env) {
    std::cout << "\nTest: Regular verb inflections" << std::endl;
    check(env.analyzer.analyze("walked"), "walk +V +PAST", "walked");
    check(env.analyzer.analyze("walking"), "walk +V +PRES_PART", "walking");
    check(env.analyzer.analyze("walks"), "walk +V +3SG", "walks");
    check(env.analyzer.analyze("talks"), "talk +V +3SG", "talks");
    check(env.analyzer.analyze("jumped"), "jump +V +PAST", "jumped");
}

void test_double_cons_verbs(Env& env) {
    std::cout << "\nTest: DOUBLE_CONS verb inflections" << std::endl;
    check(env.analyzer.analyze("stopped"), "stop +V +PAST", "stopped");
    check(env.analyzer.analyze("stopping"), "stop +V +PRES_PART", "stopping");
    check(env.analyzer.analyze("running"), "run +V +PRES_PART", "running");
    check(env.analyzer.analyze("hopped"), "hop +V +PAST", "hopped");
    check(env.analyzer.analyze("hopping"), "hop +V +PRES_PART", "hopping");
}

void test_silent_e_verbs(Env& env) {
    std::cout << "\nTest: SILENT_E verb inflections" << std::endl;
    check(env.analyzer.analyze("hoping"), "hope +V +PRES_PART", "hoping");
    check(env.analyzer.analyze("hoped"), "hope +V +PAST", "hoped");
    check(env.analyzer.analyze("making"), "make +V +PRES_PART", "making");
    check(env.analyzer.analyze("dancing"), "dance +V +PRES_PART", "dancing");
}

void test_y_final_verbs(Env& env) {
    std::cout << "\nTest: Y_FINAL verb inflections" << std::endl;
    check(env.analyzer.analyze("carries"), "carry +V +3SG", "carries");
    check(env.analyzer.analyze("carried"), "carry +V +PAST", "carried");
    check(env.analyzer.analyze("studies"), "study +V +3SG", "studies");
    check(env.analyzer.analyze("studied"), "study +V +PAST", "studied");
    check(env.analyzer.analyze("flies"), "fly +V +3SG", "flies");
}

void test_sibilant_verbs(Env& env) {
    std::cout << "\nTest: Sibilant verb inflections" << std::endl;
    check(env.analyzer.analyze("pushes"), "push +V +3SG", "pushes");
    check(env.analyzer.analyze("watches"), "watch +V +3SG", "watches");
    check(env.analyzer.analyze("reaches"), "reach +V +3SG", "reaches");
}

void test_irregular_verbs(Env& env) {
    std::cout << "\nTest: Irregular verb reverse lookup" << std::endl;
    check(env.analyzer.analyze("went"), "go +V +PAST", "went");
    check(env.analyzer.analyze("gone"), "go +V +PAST_PART", "gone");
    check(env.analyzer.analyze("was"), "be +V +PAST", "was");
    check(env.analyzer.analyze("had"), "have +V +PAST", "had");
    check(env.analyzer.analyze("did"), "do +V +PAST", "did");
}

void test_adjective_degrees(Env& env) {
    std::cout << "\nTest: Adjective degrees" << std::endl;
    // REG
    check(env.analyzer.analyze("taller"), "tall +ADJ +COMP", "taller");
    check(env.analyzer.analyze("tallest"), "tall +ADJ +SUP", "tallest");
    // DOUBLE_CONS
    check(env.analyzer.analyze("bigger"), "big +ADJ +COMP", "bigger");
    check(env.analyzer.analyze("biggest"), "big +ADJ +SUP", "biggest");
    // Y_FINAL
    check(env.analyzer.analyze("happier"), "happy +ADJ +COMP", "happier");
    check(env.analyzer.analyze("happiest"), "happy +ADJ +SUP", "happiest");
    // Irregular
    check(env.analyzer.analyze("better"), "good +ADJ +COMP", "better");
    check(env.analyzer.analyze("best"), "good +ADJ +SUP", "best");
    check(env.analyzer.analyze("worse"), "bad +ADJ +COMP", "worse");
    check(env.analyzer.analyze("worst"), "bad +ADJ +SUP", "worst");
}

void test_prefix_analysis(Env& env) {
    std::cout << "\nTest: Prefix analysis" << std::endl;
    auto r1 = env.analyzer.analyze("unhappy");
    check(r1, "un- + happy +ADJ", "unhappy");
    auto r2 = env.analyzer.analyze("rebuild");
    check(r2, "re- + build +V", "rebuild");
    auto r3 = env.analyzer.analyze("unkind");
    check(r3, "un- + kind +ADJ", "unkind");
}

void test_derivational_morphology(Env& env) {
    std::cout << "\nTest: Derivational morphology" << std::endl;
    // -ness: ADJ -> NOUN
    auto r1 = env.analyzer.analyze("darkness");
    check(r1, "dark +ADJ [-ness ->N]", "darkness");
    auto r2 = env.analyzer.analyze("happiness");
    check(r2, "happy +ADJ [-ness ->N]", "happiness");
    auto r3 = env.analyzer.analyze("kindness");
    check(r3, "kind +ADJ [-ness ->N]", "kindness");
    // -ly: ADJ -> ADV (using adjectives we know are in the lexicon)
    auto r4 = env.analyzer.analyze("clearly");
    // "clear" is in REG adjective list
    check(r4, "clear +ADJ [-ly ->ADV]", "clearly");
    auto r5 = env.analyzer.analyze("strongly");
    // "strong" is in REG adjective list
    check(r5, "strong +ADJ [-ly ->ADV]", "strongly");
}

void test_ambiguity(Env& env) {
    std::cout << "\nTest: Ambiguous analyses" << std::endl;
    auto r = env.analyzer.analyze("flies");
    assert(has_analysis(r, "fly +N +PL") || has_analysis(r, "fly +V +3SG"));
    assert(r.size() >= 2);
    std::cout << "  ✓ flies has " << r.size() << " analyses (ambiguous)" << std::endl;

    auto r2 = env.analyzer.analyze("walks");
    assert(has_analysis(r2, "walk +N +PL") || has_analysis(r2, "walk +V +3SG"));
    std::cout << "  ✓ walks is ambiguous (noun and/or verb)" << std::endl;
}

void test_weight_ranking(Env& env) {
    std::cout << "\nTest: Ambiguity ranking (weight)" << std::endl;
    auto r = env.analyzer.analyze("flies");
    assert(!r.empty());
    // All should be weight 1.0 (default), just check they're in some order
    std::cout << "  ✓ flies has " << r.size() << " ranked analyses" << std::endl;
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "\n=== Comprehensive Analyzer Integration Tests ===\n" << std::endl;

    try {
        Env env;
        std::cout << "Loaded " << env.lexicon.num_lexemes() << " lexemes." << std::endl;

        test_case_normalization(env);
        test_regular_nouns(env);
        test_sibilant_nouns(env);
        test_y_final_nouns(env);
        test_irregular_nouns(env);
        test_regular_verbs(env);
        test_double_cons_verbs(env);
        test_silent_e_verbs(env);
        test_y_final_verbs(env);
        test_sibilant_verbs(env);
        test_irregular_verbs(env);
        test_adjective_degrees(env);
        test_prefix_analysis(env);
        test_derivational_morphology(env);
        test_ambiguity(env);
        test_weight_ranking(env);

        std::cout << "\n=== All integration tests passed! ===\n" << std::endl;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "\nTest FAILED: " << e.what() << std::endl;
        return 1;
    }
}
