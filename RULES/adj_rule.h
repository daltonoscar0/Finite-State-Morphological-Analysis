#ifndef ADJ_RULE_H
#define ADJ_RULE_H

#include "morph_rule.h"

/**
 * Adjective degree rules
 */

// Regular +COMP: tall->taller
class RegularCompRule : public MorphRule {
public:
    RegularCompRule() : MorphRule("RegularComp", "+COMP → er (regular adjectives)") {
        restrict_to(StemClass::REG);
    }
    void build_fst(SymbolTable* symbols) override {
        fst_ = make_simple_affix_fst(symbols, "+COMP", "er");
    }
};

// Regular +SUP: tall->tallest
class RegularSupRule : public MorphRule {
public:
    RegularSupRule() : MorphRule("RegularSup", "+SUP → est (regular adjectives)") {
        restrict_to(StemClass::REG);
    }
    void build_fst(SymbolTable* symbols) override {
        fst_ = make_simple_affix_fst(symbols, "+SUP", "est");
    }
};

// Y_FINAL +COMP: happy->happier  (y:i -> +COMP:e -> ε:r)
class YFinalCompRule : public MorphRule {
public:
    YFinalCompRule() : MorphRule("YFinalComp", "y→i, +COMP → er (y-final adjectives)") {
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
        auto sym_r    = symbols->get_or_create("r",     SymbolType::SURFACE);
        auto sym_comp = symbols->get_or_create("+COMP", SymbolType::LEXICAL);
        auto eps      = symbols->epsilon();

        fst_->create_fst_transition(s0, s1, sym_y,    sym_i);   // y:i
        fst_->create_fst_transition(s1, s2, sym_comp, sym_e);   // +COMP:e
        fst_->create_fst_transition(s2, s3, eps,      sym_r);   // ε:r
    }
};

// Y_FINAL +SUP: happy->happiest  (y:i -> +SUP:e -> ε:s -> ε:t)
class YFinalSupRule : public MorphRule {
public:
    YFinalSupRule() : MorphRule("YFinalSup", "y→i, +SUP → est (y-final adjectives)") {
        restrict_to(StemClass::Y_FINAL);
    }
    void build_fst(SymbolTable* symbols) override {
        fst_ = std::make_unique<FST>(symbols);

        auto s0 = fst_->create_fst_state(false);
        auto s1 = fst_->create_fst_state(false);
        auto s2 = fst_->create_fst_state(false);
        auto s3 = fst_->create_fst_state(false);
        auto s4 = fst_->create_fst_state(true);
        fst_->set_start_state(s0);

        auto sym_y   = symbols->get_or_create("y",    SymbolType::SURFACE);
        auto sym_i   = symbols->get_or_create("i",    SymbolType::SURFACE);
        auto sym_e   = symbols->get_or_create("e",    SymbolType::SURFACE);
        auto sym_s   = symbols->get_or_create("s",    SymbolType::SURFACE);
        auto sym_t   = symbols->get_or_create("t",    SymbolType::SURFACE);
        auto sym_sup = symbols->get_or_create("+SUP", SymbolType::LEXICAL);
        auto eps     = symbols->epsilon();

        fst_->create_fst_transition(s0, s1, sym_y,   sym_i);   // y:i
        fst_->create_fst_transition(s1, s2, sym_sup, sym_e);   // +SUP:e
        fst_->create_fst_transition(s2, s3, eps,     sym_s);   // ε:s
        fst_->create_fst_transition(s3, s4, eps,     sym_t);   // ε:t
    }
};

#endif // ADJ_RULE_H
