#ifndef VERB_RULE_H
#define VERB_RULE_H

#include "morph_rule.h"

/**
 * Verb inflection rules
 */

// Regular +PAST: walk->walked (REG verbs)
class RegularPastRule : public MorphRule {
public:
    RegularPastRule() : MorphRule("RegularPast", "+PAST → ed (regular verbs)") {
        restrict_to(StemClass::REG);
    }
    void build_fst(SymbolTable* symbols) override {
        fst_ = make_simple_affix_fst(symbols, "+PAST", "ed");
    }
};

// Regular +PRES_PART: walk->walking (REG, Y_FINAL, SIBILANT)
class RegularPresPartRule : public MorphRule {
public:
    RegularPresPartRule() : MorphRule("RegularPresPart", "+PRES_PART → ing (regular verbs)") {
        restrict_to({StemClass::REG, StemClass::Y_FINAL, StemClass::SIBILANT});
    }
    void build_fst(SymbolTable* symbols) override {
        fst_ = make_simple_affix_fst(symbols, "+PRES_PART", "ing");
    }
};

// Regular +3SG: walk->walks (REG verbs)
class Regular3SGRule : public MorphRule {
public:
    Regular3SGRule() : MorphRule("Regular3SG", "+3SG → s (regular verbs)") {
        restrict_to(StemClass::REG);
    }
    void build_fst(SymbolTable* symbols) override {
        fst_ = make_simple_affix_fst(symbols, "+3SG", "s");
    }
};

// Sibilant +3SG: push->pushes (SIBILANT verbs)
class Sibilant3SGRule : public MorphRule {
public:
    Sibilant3SGRule() : MorphRule("Sibilant3SG", "+3SG → es (sibilant verbs)") {
        restrict_to(StemClass::SIBILANT);
    }
    void build_fst(SymbolTable* symbols) override {
        fst_ = make_simple_affix_fst(symbols, "+3SG", "es");
    }
};

// Y_FINAL +3SG: carry->carries  (y:i -> +3SG:e -> ε:s)
class YFinal3SGRule : public MorphRule {
public:
    YFinal3SGRule() : MorphRule("YFinal3SG", "y→i, +3SG → es (y-final verbs)") {
        restrict_to(StemClass::Y_FINAL);
    }
    void build_fst(SymbolTable* symbols) override {
        fst_ = std::make_unique<FST>(symbols);

        auto s0 = fst_->create_fst_state(false);
        auto s1 = fst_->create_fst_state(false);
        auto s2 = fst_->create_fst_state(false);
        auto s3 = fst_->create_fst_state(true);
        fst_->set_start_state(s0);

        auto sym_y   = symbols->get_or_create("y",    SymbolType::SURFACE);
        auto sym_i   = symbols->get_or_create("i",    SymbolType::SURFACE);
        auto sym_e   = symbols->get_or_create("e",    SymbolType::SURFACE);
        auto sym_s   = symbols->get_or_create("s",    SymbolType::SURFACE);
        auto sym_3sg = symbols->get_or_create("+3SG", SymbolType::LEXICAL);
        auto eps     = symbols->epsilon();

        fst_->create_fst_transition(s0, s1, sym_y,   sym_i);   // y:i
        fst_->create_fst_transition(s1, s2, sym_3sg, sym_e);   // +3SG:e
        fst_->create_fst_transition(s2, s3, eps,     sym_s);   // ε:s
    }
};

// Y_FINAL +PAST: carry->carried  (y:i -> +PAST:e -> ε:d)
class YFinalPastRule : public MorphRule {
public:
    YFinalPastRule() : MorphRule("YFinalPast", "y→i, +PAST → ed (y-final verbs)") {
        restrict_to(StemClass::Y_FINAL);
    }
    void build_fst(SymbolTable* symbols) override {
        fst_ = std::make_unique<FST>(symbols);

        auto s0 = fst_->create_fst_state(false);
        auto s1 = fst_->create_fst_state(false);
        auto s2 = fst_->create_fst_state(false);
        auto s3 = fst_->create_fst_state(true);
        fst_->set_start_state(s0);

        auto sym_y    = symbols->get_or_create("y",     SymbolType::SURFACE);
        auto sym_i    = symbols->get_or_create("i",     SymbolType::SURFACE);
        auto sym_e    = symbols->get_or_create("e",     SymbolType::SURFACE);
        auto sym_d    = symbols->get_or_create("d",     SymbolType::SURFACE);
        auto sym_past = symbols->get_or_create("+PAST", SymbolType::LEXICAL);
        auto eps      = symbols->epsilon();

        fst_->create_fst_transition(s0, s1, sym_y,    sym_i);   // y:i
        fst_->create_fst_transition(s1, s2, sym_past, sym_e);   // +PAST:e
        fst_->create_fst_transition(s2, s3, eps,      sym_d);   // ε:d
    }
};

// DOUBLE_CONS +PAST: stop->stopped
class DoubleConsPastRule : public MorphRule {
public:
    DoubleConsPastRule() : MorphRule("DoubleConsPast", "+PAST → ed (double-cons verbs)") {
        restrict_to(StemClass::DOUBLE_CONS);
    }
    void build_fst(SymbolTable* symbols) override {
        fst_ = make_simple_affix_fst(symbols, "+PAST", "ed");
    }
};

// DOUBLE_CONS +PRES_PART: stop->stopping
class DoubleConsPresPartRule : public MorphRule {
public:
    DoubleConsPresPartRule() : MorphRule("DoubleConsPresPart", "+PRES_PART → ing (double-cons verbs)") {
        restrict_to(StemClass::DOUBLE_CONS);
    }
    void build_fst(SymbolTable* symbols) override {
        fst_ = make_simple_affix_fst(symbols, "+PRES_PART", "ing");
    }
};

// SILENT_E +PAST: hope->hoped
class SilentEPastRule : public MorphRule {
public:
    SilentEPastRule() : MorphRule("SilentEPast", "+PAST → ed (silent-e verbs)") {
        restrict_to(StemClass::SILENT_E);
    }
    void build_fst(SymbolTable* symbols) override {
        fst_ = make_simple_affix_fst(symbols, "+PAST", "ed");
    }
};

// SILENT_E +PRES_PART: hope->hoping
class SilentEPresPartRule : public MorphRule {
public:
    SilentEPresPartRule() : MorphRule("SilentEPresPart", "+PRES_PART → ing (silent-e verbs)") {
        restrict_to(StemClass::SILENT_E);
    }
    void build_fst(SymbolTable* symbols) override {
        fst_ = make_simple_affix_fst(symbols, "+PRES_PART", "ing");
    }
};

#endif // VERB_RULE_H
