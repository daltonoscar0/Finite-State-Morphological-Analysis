#ifndef PLURAL_RULE_H
#define PLURAL_RULE_H

#include "morph_rule.h"

/**
 * RegularPluralRule: Standard English plural suffixation
 *
 * Formal Rule:
 *   +PL -> s
 *
 *   FST: (start) --+PL:s--> (final)
 *
 * Examples: cat -> cats, dog -> dogs, book -> books
 *
 * Stem Class Restriction: REG only
 */
class RegularPluralRule : public MorphRule {
public:
    RegularPluralRule()
        : MorphRule("RegularPlural", "+PL → s (regular nouns)") {
        restrict_to(StemClass::REG);
    }

    void build_fst(SymbolTable* symbols) override {
        fst_ = make_simple_affix_fst(symbols, "+PL", "s");
    }
};

/**
 * SibilantPluralRule: Plural after sibilant consonants
 *
 * Formal Rule:
 *   +PL -> es / [+sibilant] _
 *
 *   FST: (start) --+PL:e--> (s1) --ε:s--> (final)
 *
 * Examples: kiss -> kisses, buzz -> buzzes, church -> churches
 *
 * Stem Class Restriction: SIBILANT only
 */
class SibilantPluralRule : public MorphRule {
public:
    SibilantPluralRule()
        : MorphRule("SibilantPlural", "+PL → es (sibilant nouns)") {
        restrict_to(StemClass::SIBILANT);
    }

    void build_fst(SymbolTable* symbols) override {
        fst_ = make_simple_affix_fst(symbols, "+PL", "es");
    }
};

/**
 * YPluralRule: Plural with y->i orthographic alternation
 *
 * Formal Rule:
 *   y -> i / C _ +PL, +PL -> es
 *
 *   FST: (s0) --y:i--> (s1) --+PL:e--> (s2) --ε:s--> (s3:final)
 *
 * Examples: city -> cities, fly -> flies, baby -> babies
 *
 * Stem Class Restriction: Y_FINAL only
 */
class YPluralRule : public MorphRule {
public:
    YPluralRule()
        : MorphRule("YPlural", "y → i / _ +PL, then +PL → es") {
        restrict_to(StemClass::Y_FINAL);
    }

    void build_fst(SymbolTable* symbols) override {
        fst_ = std::make_unique<FST>(symbols);

        auto s0 = fst_->create_fst_state(false);
        auto s1 = fst_->create_fst_state(false);
        auto s2 = fst_->create_fst_state(false);
        auto s3 = fst_->create_fst_state(true);

        fst_->set_start_state(s0);

        auto sym_y  = symbols->get_or_create("y",   SymbolType::SURFACE);
        auto sym_i  = symbols->get_or_create("i",   SymbolType::SURFACE);
        auto sym_e  = symbols->get_or_create("e",   SymbolType::SURFACE);
        auto sym_s  = symbols->get_or_create("s",   SymbolType::SURFACE);
        auto sym_pl = symbols->get_or_create("+PL", SymbolType::LEXICAL);
        auto eps    = symbols->epsilon();

        fst_->create_fst_transition(s0, s1, sym_y,  sym_i);   // y:i
        fst_->create_fst_transition(s1, s2, sym_pl, sym_e);   // +PL:e
        fst_->create_fst_transition(s2, s3, eps,    sym_s);   // ε:s
    }
};

/**
 * IrregularPluralRule: Marker for lexically-stored plurals
 *
 * Irregular forms are stored in the Lexeme object, not computed by rules.
 * This FST reads +PL and produces no output (actual form comes from lexicon).
 *
 *   FST: (start) --+PL:ε--> (final)
 *
 * Stem Class Restriction: IRR_MOUSE, IRR_CHILD, IRR_SHEEP, IRR_OX
 */
class IrregularPluralRule : public MorphRule {
public:
    IrregularPluralRule()
        : MorphRule("IrregularPlural", "Lexically specified plurals") {
        restrict_to({
            StemClass::IRR_MOUSE,
            StemClass::IRR_CHILD,
            StemClass::IRR_SHEEP,
            StemClass::IRR_OX
        });
    }

    void build_fst(SymbolTable* symbols) override {
        fst_ = std::make_unique<FST>(symbols);

        auto s0 = fst_->create_fst_state(false);
        auto s1 = fst_->create_fst_state(true);
        fst_->set_start_state(s0);

        auto sym_pl = symbols->get_or_create("+PL", SymbolType::LEXICAL);
        auto eps    = symbols->epsilon();

        fst_->create_fst_transition(s0, s1, sym_pl, eps);  // +PL:ε
    }
};

#endif // PLURAL_RULE_H
