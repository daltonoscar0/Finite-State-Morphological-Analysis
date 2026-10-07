#ifndef PLURAL_RULE_H
#define PLURAL_RULE_H

#include "morph_rule.h"

/**
 * RegularPluralRule: Standard English plural suffixation
 * 
 * Linguistic Generalization:
 *   The default (unmarked) plural formation in English adds suffix -s
 *   to the noun stem. This is the productive rule that applies to:
 *     - Common nouns with regular phonology
 *     - Novel words entering the language
 *     - Borrowed words (typically)
 * 
 * Formal Rule:
 *   +PL -> s
 *   
 *   In FST notation:
 *     (start) --+PL:s--> (final)
 * 
 * Examples:
 *   cat +PL -> cats
 *   dog +PL -> dogs
 *   book +PL -> books
 *   computer +PL -> computers (modern word)
 *   emoji +PL -> emojis (recent borrowing)
 * 
 * Phonological Note:
 *   Although we write "s", phonologically this has three variants:
 *     - /s/ after voiceless consonants: cats /kæts/
 *     - /z/ after voiced sounds: dogs /dɔgz/, bees /biz/
 *     - /ɪz/ after sibilants: kisses /kɪsɪz/ (handled by SibilantPluralRule)
 *   
 *   Our system models orthography only, not phonology, so we don't
 *   represent these allomorphs explicitly.
 * 
 * Stem Class Restriction:
 *   Applies ONLY to REG (regular) stem class
 *   Does not apply to:
 *     - Y_FINAL: "city" -> "cities" (not *citys) [handled by YPluralRule]
 *     - SIBILANT: "kiss" -> "kisses" (not *kisss) [handled by SibilantPluralRule]
 *     - IRR_*: "mouse" -> "mice" (not *mouses) [handled by lexicon]
 */
class RegularPluralRule : public MorphRule {
public:
    /**
     * Constructor
     * 
     * Sets up:
     *   - Rule name for debugging
     *   - Linguistic description
     *   - Stem class restriction (REG only)
     */
    RegularPluralRule() 
        : MorphRule("RegularPlural", "+PL → s (regular nouns)") {
        restrict_to(StemClass::REG);
    }
    
    /**
     * build_fst: Construct the FST for regular pluralization
     * 
     * Creates a simple two-state FST:
     *   State 0 (start) --+PL:s--> State 1 (final)
     * 
     * This reads the abstract feature +PL and outputs the concrete suffix 's'.
     * 
     * Implementation uses the helper function make_simple_affix_fst()
     * which handles the boilerplate FST construction.
     * 
     * @param symbols  Global SymbolTable for creating symbols
     */
    void build_fst(SymbolTable* symbols) override {
        fst_ = make_simple_affix_fst(symbols, "+PL", "s");
    }
};

/**
 * SibilantPluralRule: Plural after sibilant consonants
 * 
 * Linguistic Generalization:
 *   After sibilant consonants (/s/, /z/, /ʃ/, /ʒ/, /tʃ/, /dʒ/), the plural
 *   suffix surfaces as -es rather than -s. This is due to a phonotactic
 *   constraint: English doesn't allow sequences of sibilants without an
 *   intervening vowel.
 * 
 * Phonological Motivation:
 *   *kisss would be unpronounceable in English
 *   kisses /kɪsɪz/ inserts epenthetic schwa /ɪ/
 * 
 * Orthographic Realization:
 *   The phonological /ɪz/ is written as "-es"
 * 
 * Formal Rule:
 *   +PL -> es / [+sibilant] _
 *   
 *   In FST notation:
 *     (start) --+PL:e--> (s1) --ε:s--> (final)
 * 
 * Sibilant Consonants in English Orthography:
 *   - s: kiss, dress, class
 *   - z: buzz, fizz, quiz
 *   - x: box, fox, tax (x = /ks/)
 *   - ch: church, bench, match (/tʃ/)
 *   - sh: wish, bush, fish (/ʃ/)
 *   - (ge/dge): (sometimes) judge, garage (/dʒ/)
 * 
 * Examples:
 *   kiss +PL -> kisses
 *   buzz +PL -> buzzes
 *   church +PL -> churches
 *   box +PL -> boxes
 *   wish +PL -> wishes
 * 
 * Stem Class Restriction:
 *   Applies ONLY to SIBILANT stem class
 *   Stems are classified as SIBILANT based on their final phoneme
 */
class SibilantPluralRule : public MorphRule {
public:
    /**
     * Constructor
     */
    SibilantPluralRule()
        : MorphRule("SibilantPlural", "+PL → es (sibilant nouns)") {
        restrict_to(StemClass::SIBILANT);
    }
    
    /**
     * build_fst: Construct FST for sibilant pluralization
     * 
     * Creates FST that outputs "es" for +PL feature:
     *   State 0 (start) --+PL:e--> State 1 --ε:s--> State 2 (final)
     * 
     * The helper make_simple_affix_fst() handles multi-character affixes
     * by creating epsilon transitions for all but the first character.
     * 
     * @param symbols  Global SymbolTable
     */
    void build_fst(SymbolTable* symbols) override {
        fst_ = make_simple_affix_fst(symbols, "+PL", "es");
    }
};

/**
 * YPluralRule: Plural with y->i orthographic alternation
 * 
 * Linguistic Generalization:
 *   English nouns ending in consonant+y change the 'y' to 'i' before
 *   adding -es for plural. This is an orthographic convention that
 *   reflects a historical sound change.
 * 
 * Historical Background:
 *   In Middle English, final /i/ was spelled <y>. Before plural suffix,
 *   this vowel changed to /i/ (spelled <ie> or <i>), creating the modern
 *   spelling pattern.
 * 
 * Formal Rule:
 *   y -> i / C _ +PL
 *   +PL -> es
 *   
 *   Where C = consonant (the stem ends in consonant + y)
 * 
 * Important Distinction:
 *   - Consonant + y: city -> cities, baby -> babies
 *   - Vowel + y: boy -> boys, key -> keys (no change!)
 * 
 * FST Structure:
 *   This rule must handle TWO operations:
 *     1. Orthographic alternation: y -> i
 *     2. Suffixation: +PL -> es
 *   
 *   FST encoding:
 *     (s0) --y:i--> (s1) --+PL:e--> (s2) --ε:s--> (s3:final)
 * 
 *   Reading:
 *     - Read 'y' from stem, write 'i'
 *     - Read '+PL' feature, write 'e'
 *     - Write 's' (epsilon transition)
 * 
 * Examples:
 *   city +PL -> cities
 *   fly +PL -> flies
 *   baby +PL -> babies
 *   party +PL -> parties
 *   cherry +PL -> cherries
 * 
 * Counterexamples (vowel+y, NOT handled by this rule):
 *   boy -> boys (not *boies)
 *   key -> keys (not *keies)
 *   These use RegularPluralRule instead
 * 
 * Stem Class Restriction:
 *   Applies ONLY to Y_FINAL stem class
 *   Stems are marked Y_FINAL if they end in consonant+y
 */
class YPluralRule : public MorphRule {
public:
    /**
     * Constructor
     */
    YPluralRule()
        : MorphRule("YPlural", "y → i / _ +PL, then +PL → es") {
        restrict_to(StemClass::Y_FINAL);
    }
    
    /**
     * build_fst: Construct FST for y-plural with orthographic alternation
     * 
     * Unlike the simple affix rules, this one requires manual FST construction
     * because it combines two operations:
     *   1. Orthographic alternation (y:i)
     *   2. Affixation (+PL:es)
     * 
     * FST Structure (4 states):
     *   State 0 (start): Initial state
     *   State 1: After y->i alternation
     *   State 2: After +PL->e
     *   State 3 (final): After ε->s
     * 
     * Transitions:
     *   s0 --y:i--> s1    Read stem-final 'y', write 'i'
     *   s1 --+PL:e--> s2  Read plural feature, write 'e'
     *   s2 --ε:s--> s3    Write final 's' without consuming input
     * 
     * Example Derivation for "city +PL":
     *   Input tape:  c i t y +PL
     *   Output tape: c i t i e s
     *   
     *   The stem "cit" is handled by previous processing
     *   This FST handles: y +PL -> i e s
     * 
     * @param symbols  Global SymbolTable
     */
    void build_fst(SymbolTable* symbols) override {
        fst_ = std::make_unique<FST>(symbols);
        
        // Create 4-state FST
        auto s0 = fst_->create_fst_state(false);  // Start
        auto s1 = fst_->create_fst_state(false);  // After y->i
        auto s2 = fst_->create_fst_state(false);  // After +PL->e
        auto s3 = fst_->create_fst_state(true);   // Final (after ε->s)
        
        fst_->set_start_state(s0);
        
        // Get all symbols needed
        auto sym_y = symbols->get_or_create("y", SymbolType::SURFACE);
        auto sym_i = symbols->get_or_create("i", SymbolType::SURFACE);
        auto sym_e = symbols->get_or_create("e", SymbolType::SURFACE);
        auto sym_s = symbols->get_or_create("s", SymbolType::SURFACE);
        auto sym_pl = symbols->get_or_create("+PL", SymbolType::LEXICAL);
        auto eps = symbols->epsilon();
        
        // Build transition chain
        // Transition 1: y -> i (orthographic alternation)
        fst_->create_fst_transition(s0, s1, sym_y, sym_i);
        
        // Transition 2: +PL -> e (first part of suffix)
        fst_->create_fst_transition(s1, s2, sym_pl, sym_e);
        
        // Transition 3: ε -> s (second part of suffix)
        fst_->create_fst_transition(s2, s3, eps, sym_s);
    }
};

/**
 * IrregularPluralRule: Marker for lexically-stored plurals
 * 
 * Linguistic Background:
 *   Some English nouns have irregular plurals that don't follow productive
 *   rules. These irregular forms must be memorized and stored in the lexicon.
 * 
 * Types of Irregular Plurals in English:
 *   
 *   1. Umlaut/Ablaut (vowel change):
 *      - mouse -> mice, louse -> lice
 *      - goose -> geese, tooth -> teeth
 *      - foot -> feet, man -> men, woman -> women
 *      Historical: Germanic i-mutation (vowel fronting)
 *   
 *   2. -en Plurals (archaic suffix):
 *      - ox -> oxen, child -> children
 *      Historical: Old English weak declension
 *   
 *   3. Zero-marked Plurals:
 *      - sheep -> sheep, deer -> deer, fish -> fish
 *      - series -> series, species -> species
 *      Often: mass nouns or animals
 *   
 *   4. Latin/Greek Plurals:
 *      - datum -> data, phenomenon -> phenomena
 *      - criterion -> criteria, analysis -> analyses
 *      (Sometimes alternate with regular: formulas/formulae)
 * 
 * Implementation Strategy:
 *   Unlike regular rules, we DON'T generate irregular plurals via FST.
 *   Instead:
 *     1. Irregular forms are stored in the Lexeme object
 *     2. This "rule" is essentially a marker/identity transducer
 *     3. The analyzer checks lexicon first for irregular forms
 * 
 * FST Structure:
 *   Simple identity/deletion FST:
 *     (start) --+PL:ε--> (final)
 *   
 *   This reads the +PL feature but produces no output, because the
 *   actual surface form comes from the lexicon, not from the rule.
 * 
 * Usage in Analysis:
 *   When analyzing "mice":
 *     1. Lookup "mice" in lexicon -> not found
 *     2. Try stem "mouse" + suffix -> lookup "mouse"
 *     3. Found "mouse" with irregular_forms[PLURAL] = "mice"
 *     4. Return Analysis("mouse", NOUN, {"+PL"})
 * 
 * Stem Class Restriction:
 *   Applies to all IRR_* classes that have irregular plurals:
 *     - IRR_MOUSE (umlaut plurals)
 *     - IRR_CHILD (-ren plurals)
 *     - IRR_SHEEP (zero plurals)
 *     - IRR_OX (-en plurals)
 */
class IrregularPluralRule : public MorphRule {
public:
    /**
     * Constructor
     * 
     * Restricts to all irregular noun classes
     */
    IrregularPluralRule()
        : MorphRule("IrregularPlural", "Lexically specified plurals") {
        restrict_to({
            StemClass::IRR_MOUSE,
            StemClass::IRR_CHILD,
            StemClass::IRR_SHEEP,
            StemClass::IRR_OX
        });
    }
    
    /**
     * build_fst: Create identity FST (marker only)
     * 
     * This FST doesn't actually generate output. It just accepts the +PL
     * feature and signals that the form should come from the lexicon.
     * 
     * FST structure:
     *   (start) --+PL:ε--> (final)
     * 
     * The epsilon output means "produce nothing from the rule; the actual
     * form is stored in the Lexeme object."
     * 
     * @param symbols  Global SymbolTable
     */
    void build_fst(SymbolTable* symbols) override {
        fst_ = std::make_unique<FST>(symbols);
        
        auto s0 = fst_->create_fst_state(false);
        auto s1 = fst_->create_fst_state(true);
        fst_->set_start_state(s0);
        
        auto sym_pl = symbols->get_or_create("+PL", SymbolType::LEXICAL);
        auto eps = symbols->epsilon();
        
        // +PL -> ε (output comes from lexicon instead)
        fst_->create_fst_transition(s0, s1, sym_pl, eps);
    }
};

#endif // PLURAL_RULE_H