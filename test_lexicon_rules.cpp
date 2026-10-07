/**
 * test_lexicon_rules.cpp: Unit tests for lexicon and morphological rules
 */

#include "SYMBOLS/symbol.h"
#include "LEXICON/lexicon_fsa.h"
#include "RULES/plural_rule.h"
#include "LANGUAGES/english_configuration.h"
#include "ANALYSIS/analyzer.h"
#include <iostream>
#include <cassert>
#include <string>
#include <vector>

// Helper: check if a specific analysis string is present in results
bool has_analysis(const std::vector<Analysis>& analyses, const std::string& expected) {
    for (const auto& a : analyses) {
        if (a.to_string() == expected) return true;
    }
    return false;
}

/**
 * test_lexicon: Verify LexiconFSA trie functionality
 * 
 * Tests:
 *   1. Lexeme insertion into trie
 *   2. Successful lookup of inserted lexemes
 *   3. Correct retrieval of lexeme properties (stem, POS, stem class)
 *   4. Null return for non-existent lexemes
 * 
 * Trie Structure Built:
 *   For lexemes "cat", "dog", "city", "fly":
 *   
 *   (root)
 *     ├── c -> a -> t [cat]
 *     │        └── r (hypothetical "car")
 *     ├── d -> o -> g [dog]
 *     ├── c -> i -> t -> y [city]
 *     └── f -> l -> y [fly]
 * 
 * Coverage:
 *   - LexiconFSA.insert_lexeme()
 *   - LexiconFSA.lookup()
 *   - Lexeme construction
 *   - Lexeme accessors (stem, pos, stem_class)
 *   - Lexeme.to_string()
 */
void test_lexicon() {
    std::cout << "Testing Lexicon FSA..." << std::endl;
    
    SymbolTable symbols;
    LexiconFSA lexicon(&symbols);
    
    // Create lexeme objects
    auto cat = std::make_shared<Lexeme>("cat", PartOfSpeech::NOUN, StemClass::REG);
    auto dog = std::make_shared<Lexeme>("dog", PartOfSpeech::NOUN, StemClass::REG);
    auto city = std::make_shared<Lexeme>("city", PartOfSpeech::NOUN, StemClass::Y_FINAL);
    auto fly = std::make_shared<Lexeme>("fly", PartOfSpeech::NOUN, StemClass::Y_FINAL);
    
    // Insert into lexicon (builds trie)
    lexicon.insert_lexeme(cat);
    lexicon.insert_lexeme(dog);
    lexicon.insert_lexeme(city);
    lexicon.insert_lexeme(fly);
    
    std::cout << "  Inserted " << lexicon.num_lexemes() << " lexemes" << std::endl;
    
    // Test successful lookup
    auto found_cat = lexicon.lookup("cat");
    assert(found_cat != nullptr);        // Found
    assert(found_cat->stem() == "cat");  // Correct stem
    assert(found_cat->pos() == PartOfSpeech::NOUN);  // Correct POS
    std::cout << "  ✓ Found: " << found_cat->to_string() << std::endl;
    
    auto found_city = lexicon.lookup("city");
    assert(found_city != nullptr);
    assert(found_city->stem_class() == StemClass::Y_FINAL);  // Correct stem class
    std::cout << "  ✓ Found: " << found_city->to_string() << std::endl;
    
    // Test failed lookup (lexeme not in trie)
    auto not_found = lexicon.lookup("elephant");
    assert(not_found == nullptr);  // Should return null
    std::cout << "  ✓ Correctly returns null for missing lexeme" << std::endl;
    
    std::cout << "Lexicon tests passed!\n" << std::endl;
}

/**
 * test_plural_rules: Verify morphological rule FSTs
 * 
 * Tests all four plural rules:
 *   1. RegularPluralRule: +PL -> s
 *   2. SibilantPluralRule: +PL -> es
 *   3. YPluralRule: y +PL -> ies
 *   4. (IrregularPluralRule tested separately)
 * 
 * For each rule, we test:
 *   - FST construction (build_fst)
 *   - Stem class applicability (applies_to)
 *   - Transduction correctness (input -> expected output)
 * 
 * Coverage:
 *   - MorphRule base class
 *   - Rule-specific build_fst() implementations
 *   - make_simple_affix_fst() helper
 *   - FST transduction with multiple outputs
 *   - Epsilon transitions in y-plural rule
 */
void test_plural_rules() {
    std::cout << "Testing Plural Rules..." << std::endl;
    
    SymbolTable symbols;
    
    // ========== Test 1: Regular Plural Rule ==========
    
    RegularPluralRule reg_rule;
    reg_rule.build_fst(&symbols);
    
    assert(reg_rule.is_built());                     // FST constructed
    assert(reg_rule.applies_to(StemClass::REG));     // Applies to REG
    assert(!reg_rule.applies_to(StemClass::Y_FINAL)); // Doesn't apply to Y_FINAL
    std::cout << "  ✓ Regular plural rule: " << reg_rule.description() << std::endl;
    
    // Test transduction: +PL -> s
    auto pl_sym = symbols.get_or_create("+PL", SymbolType::LEXICAL);
    std::vector<std::shared_ptr<Symbol>> input = {pl_sym};
    auto results = reg_rule.fst()->transduce(input);
    
    assert(results.size() == 1);             // One transduction path
    assert(results[0].output.size() == 1);   // Outputs one symbol
    assert(results[0].output[0]->repr() == "s");  // Symbol is 's'
    std::cout << "  ✓ +PL → 's'" << std::endl;
    
    // ========== Test 2: Sibilant Plural Rule ==========
    
    SibilantPluralRule sib_rule;
    sib_rule.build_fst(&symbols);
    
    assert(sib_rule.applies_to(StemClass::SIBILANT));  // Applies to SIBILANT
    results = sib_rule.fst()->transduce(input);
    
    assert(results.size() == 1);               // One path
    assert(results[0].output.size() == 2);     // Outputs two symbols: "es"
    assert(results[0].output[0]->repr() == "e");  // First: 'e'
    assert(results[0].output[1]->repr() == "s");  // Second: 's'
    std::cout << "  ✓ +PL → 'es' (sibilant)" << std::endl;
    
    // ========== Test 3: Y-Plural Rule ==========
    
    YPluralRule y_rule;
    y_rule.build_fst(&symbols);
    
    assert(y_rule.applies_to(StemClass::Y_FINAL));  // Applies to Y_FINAL
    
    // Build input: "y" + "+PL" (stem-final y plus plural feature)
    auto y_sym = symbols.get_or_create("y", SymbolType::SURFACE);
    std::vector<std::shared_ptr<Symbol>> y_input = {y_sym, pl_sym};
    results = y_rule.fst()->transduce(y_input);
    
    assert(results.size() == 1);               // One path
    assert(results[0].output.size() == 3);     // Outputs three symbols: "ies"
    assert(results[0].output[0]->repr() == "i");  // y -> i alternation
    assert(results[0].output[1]->repr() == "e");  // First part of suffix
    assert(results[0].output[2]->repr() == "s");  // Second part of suffix
    std::cout << "  ✓ y +PL → 'ies'" << std::endl;
    
    std::cout << "Plural rule tests passed!\n" << std::endl;
}

/**
 * test_lexeme_with_irregular: Verify irregular form storage
 * 
 * Tests:
 *   1. Creating lexeme with irregular stem class
 *   2. Adding irregular forms to lexeme
 *   3. Querying for irregular forms
 *   4. Checking is_irregular() predicate
 * 
 * Example:
 *   Lexeme "mouse" with irregular plural "mice"
 *   This is stored in the lexeme, NOT computed by rules
 * 
 * Coverage:
 *   - Lexeme.add_irregular_form()
 *   - Lexeme.has_irregular_form()
 *   - Lexeme.get_irregular_form()
 *   - Lexeme.is_irregular()
 *   - std::optional usage for optional values
 */
void test_lexeme_with_irregular() {
    std::cout << "Testing irregular lexemes..." << std::endl;
    
    // Create irregular noun with suppletive plural
    auto mouse = std::make_shared<Lexeme>("mouse", PartOfSpeech::NOUN, StemClass::IRR_MOUSE);
    mouse->add_irregular_form(Lexeme::Feature::PLURAL, "mice");
    
    // Test predicates
    assert(mouse->is_irregular());  // Should be irregular
    assert(mouse->has_irregular_form(Lexeme::Feature::PLURAL));  // Has plural form
    
    // Test retrieval (returns std::optional)
    auto plural_form = mouse->get_irregular_form(Lexeme::Feature::PLURAL);
    assert(plural_form.has_value());           // Optional is not empty
    assert(plural_form.value() == "mice");     // Value is "mice"
    
    std::cout << "  ✓ Irregular: " << mouse->stem() << " +PL → " 
              << plural_form.value() << std::endl;
    
    std::cout << "Irregular lexeme tests passed!\n" << std::endl;
}

// ── Helper: build the full English analyzer ──────────────────────────────

static SymbolTable* g_symbols = nullptr;
static LexiconFSA* g_lexicon = nullptr;
static Analyzer* g_analyzer = nullptr;

void setup_english_analyzer() {
    if (!g_symbols) {
        g_symbols = new SymbolTable();
        g_lexicon = new LexiconFSA(g_symbols);
        g_analyzer = new Analyzer(g_symbols, g_lexicon);
        EnglishConfig::initialize(*g_symbols, *g_lexicon, *g_analyzer);
    }
}

// ── New test functions ────────────────────────────────────────────────────

void test_y_final_noun_plurals() {
    std::cout << "Testing Y_FINAL noun plurals..." << std::endl;
    setup_english_analyzer();

    auto r1 = g_analyzer->analyze("cities");
    assert(has_analysis(r1, "city +N +PL"));
    std::cout << "  ✓ cities → city +N +PL" << std::endl;

    auto r2 = g_analyzer->analyze("babies");
    assert(has_analysis(r2, "baby +N +PL"));
    std::cout << "  ✓ babies → baby +N +PL" << std::endl;

    auto r3 = g_analyzer->analyze("countries");
    assert(has_analysis(r3, "country +N +PL"));
    std::cout << "  ✓ countries → country +N +PL" << std::endl;

    std::cout << "Y_FINAL noun plural tests passed!\n" << std::endl;
}

void test_sibilant_noun_plurals() {
    std::cout << "Testing SIBILANT noun plurals..." << std::endl;
    setup_english_analyzer();

    auto r1 = g_analyzer->analyze("churches");
    assert(has_analysis(r1, "church +N +PL"));
    std::cout << "  ✓ churches → church +N +PL" << std::endl;

    auto r2 = g_analyzer->analyze("kisses");
    assert(has_analysis(r2, "kiss +N +PL"));
    std::cout << "  ✓ kisses → kiss +N +PL" << std::endl;

    auto r3 = g_analyzer->analyze("boxes");
    assert(has_analysis(r3, "box +N +PL"));
    std::cout << "  ✓ boxes → box +N +PL" << std::endl;

    std::cout << "SIBILANT noun plural tests passed!\n" << std::endl;
}

void test_irregular_reverse_lookup() {
    std::cout << "Testing irregular reverse lookup..." << std::endl;
    setup_english_analyzer();

    auto r1 = g_analyzer->analyze("mice");
    assert(has_analysis(r1, "mouse +N +PL"));
    std::cout << "  ✓ mice → mouse +N +PL" << std::endl;

    auto r2 = g_analyzer->analyze("went");
    assert(has_analysis(r2, "go +V +PAST"));
    std::cout << "  ✓ went → go +V +PAST" << std::endl;

    auto r3 = g_analyzer->analyze("children");
    assert(has_analysis(r3, "child +N +PL"));
    std::cout << "  ✓ children → child +N +PL" << std::endl;

    auto r4 = g_analyzer->analyze("feet");
    assert(has_analysis(r4, "foot +N +PL"));
    std::cout << "  ✓ feet → foot +N +PL" << std::endl;

    auto r5 = g_analyzer->analyze("geese");
    assert(has_analysis(r5, "goose +N +PL"));
    std::cout << "  ✓ geese → goose +N +PL" << std::endl;

    std::cout << "Irregular reverse lookup tests passed!\n" << std::endl;
}

void test_case_normalization() {
    std::cout << "Testing case normalization..." << std::endl;
    setup_english_analyzer();

    auto r1 = g_analyzer->analyze("Dogs");
    assert(has_analysis(r1, "dog +N +PL"));
    std::cout << "  ✓ Dogs → dog +N +PL" << std::endl;

    auto r2 = g_analyzer->analyze("WALKED");
    assert(has_analysis(r2, "walk +V +PAST"));
    std::cout << "  ✓ WALKED → walk +V +PAST" << std::endl;

    auto r3 = g_analyzer->analyze("Cities");
    assert(has_analysis(r3, "city +N +PL"));
    std::cout << "  ✓ Cities → city +N +PL" << std::endl;

    std::cout << "Case normalization tests passed!\n" << std::endl;
}

void test_verb_inflections() {
    std::cout << "Testing verb inflections..." << std::endl;
    setup_english_analyzer();

    // Regular past
    auto r1 = g_analyzer->analyze("walked");
    assert(has_analysis(r1, "walk +V +PAST"));
    std::cout << "  ✓ walked → walk +V +PAST" << std::endl;

    // Regular present participle
    auto r2 = g_analyzer->analyze("walking");
    assert(has_analysis(r2, "walk +V +PRES_PART"));
    std::cout << "  ✓ walking → walk +V +PRES_PART" << std::endl;

    // Regular 3sg
    auto r3 = g_analyzer->analyze("talks");
    assert(has_analysis(r3, "talk +V +3SG"));
    std::cout << "  ✓ talks → talk +V +3SG" << std::endl;

    // DOUBLE_CONS past
    auto r4 = g_analyzer->analyze("stopped");
    assert(has_analysis(r4, "stop +V +PAST"));
    std::cout << "  ✓ stopped → stop +V +PAST" << std::endl;

    // DOUBLE_CONS pres_part
    auto r5 = g_analyzer->analyze("stopping");
    assert(has_analysis(r5, "stop +V +PRES_PART"));
    std::cout << "  ✓ stopping → stop +V +PRES_PART" << std::endl;

    // SILENT_E pres_part
    auto r6 = g_analyzer->analyze("hoping");
    assert(has_analysis(r6, "hope +V +PRES_PART"));
    std::cout << "  ✓ hoping → hope +V +PRES_PART" << std::endl;

    // Y_FINAL 3sg
    auto r7 = g_analyzer->analyze("carries");
    assert(has_analysis(r7, "carry +V +3SG"));
    std::cout << "  ✓ carries → carry +V +3SG" << std::endl;

    // Y_FINAL past
    auto r8 = g_analyzer->analyze("carried");
    assert(has_analysis(r8, "carry +V +PAST"));
    std::cout << "  ✓ carried → carry +V +PAST" << std::endl;

    // Sibilant 3sg
    auto r9 = g_analyzer->analyze("pushes");
    assert(has_analysis(r9, "push +V +3SG"));
    std::cout << "  ✓ pushes → push +V +3SG" << std::endl;

    std::cout << "Verb inflection tests passed!\n" << std::endl;
}

void test_adjective_degrees() {
    std::cout << "Testing adjective degrees..." << std::endl;
    setup_english_analyzer();

    auto r1 = g_analyzer->analyze("taller");
    assert(has_analysis(r1, "tall +ADJ +COMP"));
    std::cout << "  ✓ taller → tall +ADJ +COMP" << std::endl;

    auto r2 = g_analyzer->analyze("tallest");
    assert(has_analysis(r2, "tall +ADJ +SUP"));
    std::cout << "  ✓ tallest → tall +ADJ +SUP" << std::endl;

    auto r3 = g_analyzer->analyze("bigger");
    assert(has_analysis(r3, "big +ADJ +COMP"));
    std::cout << "  ✓ bigger → big +ADJ +COMP" << std::endl;

    auto r4 = g_analyzer->analyze("biggest");
    assert(has_analysis(r4, "big +ADJ +SUP"));
    std::cout << "  ✓ biggest → big +ADJ +SUP" << std::endl;

    auto r5 = g_analyzer->analyze("happier");
    assert(has_analysis(r5, "happy +ADJ +COMP"));
    std::cout << "  ✓ happier → happy +ADJ +COMP" << std::endl;

    auto r6 = g_analyzer->analyze("happiest");
    assert(has_analysis(r6, "happy +ADJ +SUP"));
    std::cout << "  ✓ happiest → happy +ADJ +SUP" << std::endl;

    std::cout << "Adjective degree tests passed!\n" << std::endl;
}

/**
 * main: Test runner for lexicon and rules
 */
int main() {
    std::cout << "\n=== Testing Lexicon and Rules ===\n" << std::endl;

    try {
        test_lexicon();
        test_plural_rules();
        test_lexeme_with_irregular();

        // New tests
        test_y_final_noun_plurals();
        test_sibilant_noun_plurals();
        test_irregular_reverse_lookup();
        test_case_normalization();
        test_verb_inflections();
        test_adjective_degrees();

        std::cout << "=== All lexicon and rule tests passed! ===\n" << std::endl;

        // Cleanup
        delete g_analyzer;
        delete g_lexicon;
        delete g_symbols;

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}