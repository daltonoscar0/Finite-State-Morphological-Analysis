#ifndef MORPH_RULE_H
#define MORPH_RULE_H

#include "../AUTOMATA/fst.h"
#include "../LEXICON/stem_class.h"
#include <string>
#include <memory>
#include <unordered_set>

/**
 * MorphRule: Abstract base class for morphological rules
 *
 * Purpose:
 *   Encapsulates a morphological generalization as a finite-state transducer.
 *   Each rule implements a regular relation between lexical and surface forms.
 *
 * Theoretical Foundation:
 *   In finite-state morphology (Beesley & Karttunen), morphological rules are
 *   formalized as finite-state transducers (FSTs) that map between:
 *     - Lexical representations (abstract): stem + grammatical features
 *     - Surface representations (concrete): actual word forms
 *
 *   Example:
 *     Lexical:  cat + +PL
 *     Surface:  c a t s
 *     Rule FST: +PL -> s
 *
 * Rule Types in English Morphology:
 *
 *   1. Affixation Rules (concatenative morphology):
 *      - Add suffixes/prefixes to stems
 *      - Examples:
 *        * +PL -> s        (cat -> cats)
 *        * +PAST -> ed     (walk -> walked)
 *        * +ING -> ing     (walk -> walking)
 *
 *   2. Orthographic Rules (surface realization):
 *      - Adjust spelling at morph boundaries
 *      - Examples:
 *        * y -> i / _ +PL   (city -> cities)
 *        * e -> ø / _ +ing  (hope -> hoping)
 *        * C -> CC / VC_+V  (stop -> stopped)
 *
 *   3. Phonological Rules (if modeling sound):
 *      - Handle sound changes
 *      - Examples:
 *        * /s/ -> /z/ / [+voice]_  (cats /kæts/ vs dogs /dɔgz/)
 *      - Not implemented in this system (orthographic only)
 *
 * Design Rationale:
 *   - Each rule is fundamentally an FST (maintains finite-state property)
 *   - Rules are restricted by stem class (enables/blocks rule application)
 *   - Rules have linguistic interpretations (not arbitrary string rewrites)
 *   - Rules can be composed (though we simulate composition here)
 *
 * Stem Class Restriction:
 *   Not all rules apply to all stems. Examples:
 *     - Regular plural (s) applies to REG class
 *     - Y-plural (ies) applies to Y_FINAL class
 *     - Irregular plural doesn't apply to regular stems
 *
 *   This is encoded in applicable_classes_ set.
 *
 * Architecture:
 *   - MorphRule is abstract (cannot be instantiated)
 *   - Concrete subclasses implement build_fst() to construct their FST
 *   - FST is built lazily (only when needed)
 */
class MorphRule {
protected:
    std::string name_;                  // Human-readable rule name
    std::string description_;           // Linguistic description
    std::unique_ptr<FST> fst_;          // The FST implementing this rule

    // Stem class restrictions
    // Empty set = applies to all stem classes
    // Non-empty = only applies to classes in the set
    std::unordered_set<StemClass> applicable_classes_;

public:
    /**
     * Constructor
     *
     * @param name         Rule name (e.g., "RegularPlural")
     * @param description  Linguistic description (e.g., "+PL -> s")
     *
     * Note: FST is not built here - it's created lazily via build_fst()
     */
    MorphRule(const std::string& name, const std::string& description)
        : name_(name), description_(description) {}

    /**
     * Virtual destructor for proper polymorphic cleanup
     */
    virtual ~MorphRule() = default;

    // ========== Accessors ==========

    const std::string& name() const { return name_; }
    const std::string& description() const { return description_; }
    FST* fst() const { return fst_.get(); }

    // ========== Applicability Checking ==========

    /**
     * applies_to: Check if this rule applies to a given stem class
     *
     * Logic:
     *   - If applicable_classes_ is empty -> rule is universal (applies to all)
     *   - Otherwise -> rule applies only if sc is in the set
     *
     * Example:
     *   RegularPluralRule has applicable_classes_ = {REG}
     *   applies_to(REG) -> true
     *   applies_to(Y_FINAL) -> false
     *
     * @param sc  Stem class to check
     * @return    true if rule can apply, false otherwise
     */
    bool applies_to(StemClass sc) const {
        if (applicable_classes_.empty()) return true;  // Universal rule
        return applicable_classes_.count(sc) > 0;
    }

    /**
     * restrict_to: Limit rule to a single stem class
     *
     * @param sc  Stem class this rule should apply to
     */
    void restrict_to(StemClass sc) {
        applicable_classes_.insert(sc);
    }

    /**
     * restrict_to: Limit rule to multiple stem classes
     *
     * @param classes  Vector of stem classes this rule applies to
     */
    void restrict_to(const std::vector<StemClass>& classes) {
        for (auto sc : classes) {
            applicable_classes_.insert(sc);
        }
    }

    // ========== Abstract Interface ==========

    /**
     * build_fst: Construct the FST for this rule
     *
     * PURE VIRTUAL - must be implemented by subclasses
     *
     * @param symbols  Pointer to global SymbolTable for creating symbols
     */
    virtual void build_fst(SymbolTable* symbols) = 0;

    /**
     * is_built: Check if FST has been constructed
     *
     * @return true if build_fst() has been called, false otherwise
     */
    bool is_built() const { return fst_ != nullptr; }
};

/**
 * make_simple_affix_fst: Helper for creating standard affixation FSTs
 *
 * Pattern: lexical_tag -> surface_affix
 *
 * Examples:
 *   make_simple_affix_fst(symbols, "+PL", "s")
 *     Creates: (start) --+PL:s--> (final)
 *
 *   make_simple_affix_fst(symbols, "+PL", "es")
 *     Creates: (start) --+PL:e--> (1) --ε:s--> (final)
 *
 * @param symbols       Global symbol table
 * @param lexical_tag   The lexical feature (e.g., "+PL", "+PAST")
 * @param surface_affix The surface string to output (e.g., "s", "ed", "es")
 * @return              unique_ptr to the constructed FST
 */
inline std::unique_ptr<FST> make_simple_affix_fst(
    SymbolTable* symbols,
    const std::string& lexical_tag,
    const std::string& surface_affix) {

    auto fst = std::make_unique<FST>(symbols);

    auto s0 = fst->create_fst_state(false);
    auto s1 = fst->create_fst_state(true);
    fst->set_start_state(s0);

    auto lex_sym = symbols->get_or_create(lexical_tag, SymbolType::LEXICAL);

    if (surface_affix.empty()) {
        auto eps = symbols->epsilon();
        fst->create_fst_transition(s0, s1, lex_sym, eps);
    }
    else if (surface_affix.length() == 1) {
        auto surf_sym = symbols->get_or_create(
            std::string(1, surface_affix[0]), SymbolType::SURFACE);
        fst->create_fst_transition(s0, s1, lex_sym, surf_sym);
    }
    else {
        // Multi-character affix: first char consumed with tag, rest via epsilon
        auto first_sym = symbols->get_or_create(
            std::string(1, surface_affix[0]), SymbolType::SURFACE);

        State* prev = s0;
        State* curr = fst->create_fst_state(false);
        fst->create_fst_transition(prev, curr, lex_sym, first_sym);
        prev = curr;

        auto eps = symbols->epsilon();
        for (size_t i = 1; i < surface_affix.length(); i++) {
            auto sym = symbols->get_or_create(
                std::string(1, surface_affix[i]), SymbolType::SURFACE);

            if (i == surface_affix.length() - 1) {
                fst->create_fst_transition(prev, s1, eps, sym);
            } else {
                curr = fst->create_fst_state(false);
                fst->create_fst_transition(prev, curr, eps, sym);
                prev = curr;
            }
        }
    }

    return fst;
}

#endif // MORPH_RULE_H
