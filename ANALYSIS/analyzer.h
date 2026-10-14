#ifndef ANALYZER_H
#define ANALYZER_H

#include "analysis.h"
#include "surface_fsa.h"
#include "tokenizer.h"
#include "../LEXICON/closed_class.h"
#include "../LEXICON/lexicon_fsa.h"
#include "../RULES/morph_rule.h"
#include <vector>
#include <memory>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <tuple>
#include <algorithm>
#include <cctype>
#include <string>

#include "../RULES/derivation_rule.h"
#include "../RULES/orthography.h"
#include "../RULES/prefix_rule.h"

/**
 * AnalysisState: Represents a configuration during parallel FSA traversal
 */
struct AnalysisState {
    State* surface_state;
    State* lexicon_state;
    std::vector<std::shared_ptr<Symbol>> lexical_output;

    AnalysisState(State* surf, State* lex)
        : surface_state(surf), lexicon_state(lex) {}

    std::string hash() const {
        return std::to_string(reinterpret_cast<uintptr_t>(surface_state)) + "," +
               std::to_string(reinterpret_cast<uintptr_t>(lexicon_state)) + "," +
               std::to_string(lexical_output.size());
    }
};

/**
 * Analyzer: Morphological analysis engine
 */
class Analyzer {
private:
    SymbolTable* symbols_;
    LexiconFSA* lexicon_;
    std::vector<std::shared_ptr<MorphRule>> rules_;
    Tokenizer tokenizer_;
    std::vector<std::shared_ptr<DerivationRule>> deriv_rules_;

    /**
     * closed_class_: The function-word list, or null when it was not supplied
     *
     * Read by one condition on one rule: a `-y` reading of a closed-class word
     * is not produced. The list can only withdraw a derivational reading, so a
     * caller that does not supply it gets exactly the analyses it got before.
     * See LEXICON/closed_class.h for why the file is read as a word list
     * rather than reached through --lexicon.
     */
    const ClosedClassSet* closed_class_ = nullptr;

    // Irregular reverse index: surface form -> [(lexeme, feature)]
    std::unordered_map<std::string,
        std::vector<std::pair<std::shared_ptr<Lexeme>, Lexeme::Feature>>>
        irregular_index_;

public:
    Analyzer(SymbolTable* symbols, LexiconFSA* lexicon)
        : symbols_(symbols), lexicon_(lexicon), tokenizer_(symbols) {}

    void add_rule(std::shared_ptr<MorphRule> rule) {
        if (!rule->is_built()) {
            rule->build_fst(symbols_);
        }
        rules_.push_back(rule);
    }

    void add_derivation_rule(std::shared_ptr<DerivationRule> rule) {
        deriv_rules_.push_back(rule);
    }

    /**
     * set_closed_class: Supply the function-word list, which is not owned
     *
     * @param words  The list, or nullptr to analyze without it
     */
    void set_closed_class(const ClosedClassSet* words) {
        closed_class_ = words;
    }

    /**
     * derivation_rules: The registered derivational rules
     *
     * Exposed so that form generation (RULES/form_generator.h) can derive its
     * forms from the same rule objects the analyzer strips with, rather than
     * from a second hard-coded list that could drift out of step.
     */
    const std::vector<std::shared_ptr<DerivationRule>>& derivation_rules() const {
        return deriv_rules_;
    }

    // Build reverse index from irregular forms to their lexemes
    void build_irregular_index() {
        for (const auto& lex : lexicon_->all_lexemes()) {
            for (const auto& [feat, form] : lex->irregular_forms_map()) {
                irregular_index_[form].push_back({lex, feat});
            }
        }
    }

    /**
     * is_irregular_form: Did an analysis come from a stored irregular form?
     *
     * The irregular pre-pass in analyze() produces analyses straight out of
     * each lexeme's irregular_forms_ map (went -> go +V +PAST), which share no
     * predictable surface material with the lemma. The surface segmentation
     * output needs to tell those apart from rule-derived analyses so it can
     * keep them as a single piece. This is a read-only query; it does not
     * affect analysis.
     *
     * @param surface_lower  The lowercased surface word
     * @param stem           Lemma of the analysis in question
     * @param features       Feature tags of the analysis in question
     * @return               true if the irregular index holds this pairing
     */
    bool is_irregular_form(const std::string& surface_lower,
                           const std::string& stem,
                           const std::vector<std::string>& features) const {
        if (features.size() != 1) return false;
        auto it = irregular_index_.find(surface_lower);
        if (it == irregular_index_.end()) return false;
        for (const auto& [lex, feat] : it->second) {
            if (lex->stem() == stem && feature_to_tag(feat) == features[0]) {
                return true;
            }
        }
        return false;
    }

    /**
     * stores_surface: Is this spelling a stored irregular form of some lexeme?
     *
     * Weaker than is_irregular_form, which asks whether one particular
     * analysis came out of the index. This asks only whether the lexicon holds
     * the word at all, which is what the guard on --derived-first needs: if
     * "broken" is listed as the participle of "break", then no reading of
     * "broken" that the rules happen to build should be ranked above it.
     *
     * @param surface_lower  The lowercased surface word
     */
    bool stores_surface(const std::string& surface_lower) const {
        return irregular_index_.find(surface_lower) != irregular_index_.end();
    }

    // English prefixes for prefix stripping, in the order they are tried.
    // The forms come from the one inventory in RULES/prefix_rule.h, which also
    // records what each prefix attaches to. Stripping does not consult the
    // categories: a prefix is tried against every analysis of the remainder
    // and the ranking settles which reading wins, so the lexical output is the
    // same as it was when this list was written out here.
    static const std::vector<std::string>& english_prefixes() {
        static const std::vector<std::string> prefixes = [] {
            std::vector<std::string> forms;
            for (const auto& rule : english_prefix_rules()) {
                forms.push_back(rule.form);
            }
            return forms;
        }();
        return prefixes;
    }

    /**
     * analyze: Find all morphological analyses for an input word
     *
     * @param input         A single word string
     * @param allow_prefix  Whether to try prefix stripping (default true)
     */
    std::vector<Analysis> analyze(const std::string& input, bool allow_prefix = true) {
        std::vector<Analysis> results;

        // STEP 8: Case normalization
        std::string normalized = input;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        // STEP 3: Irregular pre-pass
        auto irr_it = irregular_index_.find(normalized);
        if (irr_it != irregular_index_.end()) {
            for (const auto& [lex, feat] : irr_it->second) {
                std::string tag = feature_to_tag(feat);
                Analysis a(lex->stem(), lex->pos(), {tag});
                a.set_weight(lex->weight());
                results.push_back(a);
            }
        }

        // Tokenize normalized input
        auto symbols = tokenizer_.tokenize(normalized);
        if (symbols.empty()) {
            return results;
        }

        // Try all possible stem lengths
        for (size_t stem_len = 1; stem_len <= symbols.size(); stem_len++) {

            std::string potential_stem;
            for (size_t i = 0; i < stem_len; i++) {
                potential_stem += symbols[i]->repr();
            }

            std::string suffix;
            for (size_t i = stem_len; i < symbols.size(); i++) {
                suffix += symbols[i]->repr();
            }

            // Direct lexicon lookup (multi-POS via lookup_all)
            auto lexemes = lexicon_->lookup_all(potential_stem);
            for (auto& lexeme : lexemes) {
                auto analyses = analyze_suffix(suffix, lexeme);
                results.insert(results.end(), analyses.begin(), analyses.end());
            }

            // Y->I alternation reversal: "citi" -> "city"
            if (stem_len > 0 && !potential_stem.empty() && potential_stem.back() == 'i') {
                std::string y_stem = potential_stem;
                y_stem.back() = 'y';

                auto y_lexemes = lexicon_->lookup_all(y_stem);
                for (auto& lexeme : y_lexemes) {
                    if (orthography::effective_class(*lexeme) == StemClass::Y_FINAL) {
                        // Noun plural: city -> cities (suffix = "es")
                        if (lexeme->pos() == PartOfSpeech::NOUN && suffix == "es") {
                            emit(results, lexeme, y_stem, PartOfSpeech::NOUN, "+PL");
                        }
                        // Verb 3SG: carry -> carries (suffix = "es")
                        else if (lexeme->pos() == PartOfSpeech::VERB) {
                            if (suffix == "es") {
                                emit(results, lexeme, y_stem, PartOfSpeech::VERB, "+3SG");
                            }
                            if (suffix == "ed") {
                                emit(results, lexeme, y_stem, PartOfSpeech::VERB, "+PAST");
                                emit(results, lexeme, y_stem, PartOfSpeech::VERB, "+PAST_PART");
                            }
                        }
                        // Adjective comparative/superlative: happy -> happier/happiest
                        else if (lexeme->pos() == PartOfSpeech::ADJECTIVE) {
                            if (suffix == "er") {
                                emit(results, lexeme, y_stem, PartOfSpeech::ADJECTIVE, "+COMP");
                            }
                            if (suffix == "est") {
                                emit(results, lexeme, y_stem, PartOfSpeech::ADJECTIVE, "+SUP");
                            }
                        }
                    }
                }
            }

            // DOUBLE_CONS reversal: "stopp" -> "stop" (doubled final consonant)
            if (stem_len >= 2 && potential_stem[stem_len-1] == potential_stem[stem_len-2]) {
                std::string undoubled = potential_stem.substr(0, stem_len - 1);
                auto dc_lexemes = lexicon_->lookup_all(undoubled);
                for (auto& lex : dc_lexemes) {
                    if (lex->stem_class() == StemClass::DOUBLE_CONS) {
                        if (lex->pos() == PartOfSpeech::VERB) {
                            if (suffix == "ed") {
                                emit(results, lex, undoubled, PartOfSpeech::VERB, "+PAST");
                                emit(results, lex, undoubled, PartOfSpeech::VERB, "+PAST_PART");
                            } else if (suffix == "ing") {
                                emit(results, lex, undoubled, PartOfSpeech::VERB, "+PRES_PART");
                            }
                        }
                        if (lex->pos() == PartOfSpeech::ADJECTIVE) {
                            if (suffix == "er") {
                                emit(results, lex, undoubled, PartOfSpeech::ADJECTIVE, "+COMP");
                            }
                            if (suffix == "est") {
                                emit(results, lex, undoubled, PartOfSpeech::ADJECTIVE, "+SUP");
                            }
                        }
                    }
                }
            }

            // SILENT_E reversal: "hop" + "ing" -> "hope" + "ing" (restore 'e')
            {
                std::string e_stem = potential_stem + "e";
                auto se_lexemes = lexicon_->lookup_all(e_stem);
                for (auto& lex : se_lexemes) {
                    if (orthography::effective_class(*lex) == StemClass::SILENT_E) {
                        if (lex->pos() == PartOfSpeech::VERB) {
                            if (suffix == "ing") {
                                emit(results, lex, e_stem, PartOfSpeech::VERB, "+PRES_PART");
                            }
                            if (suffix == "ed") {
                                emit(results, lex, e_stem, PartOfSpeech::VERB, "+PAST");
                                emit(results, lex, e_stem, PartOfSpeech::VERB, "+PAST_PART");
                            }
                        }
                        if (lex->pos() == PartOfSpeech::ADJECTIVE) {
                            if (suffix == "er") {
                                emit(results, lex, e_stem, PartOfSpeech::ADJECTIVE, "+COMP");
                            }
                            if (suffix == "est") {
                                emit(results, lex, e_stem, PartOfSpeech::ADJECTIVE, "+SUP");
                            }
                        }
                    }
                }
            }
        }

        // STEP 6: Derivational suffix stripping pass
        analyze_derivational(normalized, results);

        // STEP 5: Prefix stripping pass
        if (allow_prefix) {
            for (const auto& prefix : english_prefixes()) {
                if (normalized.size() > prefix.size() &&
                    normalized.substr(0, prefix.size()) == prefix) {
                    std::string remainder = normalized.substr(prefix.size());
                    if (remainder.size() >= 2) {
                        auto base = analyze(remainder, false);
                        for (auto& ba : base) {
                            // A derivational prefix attaches to an open-class
                            // stem. Without this test "unto" comes apart as
                            // un- plus the preposition "to", and "upon" as
                            // up- plus "on". No lexeme is FUNCTION unless a
                            // closed-class file was loaded, so nothing the
                            // default lexicon analyzes is affected.
                            if (ba.pos() == PartOfSpeech::FUNCTION) continue;
                            if (!ba.has_prefix()) {
                                Analysis prefixed = ba;
                                prefixed.set_prefix(prefix);
                                results.push_back(prefixed);
                            }
                        }
                    }
                }
            }
        }

        // STEP 9: Deduplicate, keeping the highest weight of each analysis.
        // Duplicates may not be adjacent, so this is a scan rather than a
        // neighbour comparison.
        //
        // Keeping the highest weight rather than the first one matters as soon
        // as more than one lexicon is loaded. The same stem then arrives twice,
        // once from the built-in lists at the default weight of 1.0 and once
        // from a generated file at its frequency-derived weight, and the
        // built-in lists are loaded first. Keeping the first occurrence threw
        // the frequency away: "running" lost to its own stored noun entry
        // because "run +V +PRES_PART" was pinned at 1.0. With the default
        // lexicon alone every weight is 1.0, so nothing changes there.
        {
            std::vector<Analysis> deduped;
            for (auto& r : results) {
                bool found = false;
                for (auto& d : deduped) {
                    if (r == d) {
                        if (r.weight() > d.weight()) d.set_weight(r.weight());
                        found = true;
                        break;
                    }
                }
                if (!found) deduped.push_back(r);
            }
            results = std::move(deduped);
        }

        // Sort by weight descending (most likely first)
        std::sort(results.begin(), results.end(),
            [](const Analysis& a, const Analysis& b) {
                return a.weight() > b.weight();
            });

        return results;
    }

private:
    // ── Derivation ─────────────────────────────────────────────────────────

    /**
     * MAX_DERIV_LAYERS: How many derivational suffixes may stack
     *
     * Two, plus an inflection outside them: nation|al|ity, care|less|ness,
     * thank|ful|ly, nation|al|iti|es. Every layer multiplies the weight by 0.9,
     * so a third would score 0.73 and lose to almost anything, and each layer
     * also multiplies the chances of a spurious reading: a word that ends in
     * two known suffixes by accident is far more common than one that ends in
     * three on purpose. docs/LEXICON.md counts what the limit reaches.
     */
    static const size_t MAX_DERIV_LAYERS = 2;

    /**
     * MIN_STACK_ROOT: Shortest root a second derivational suffix may sit on
     *
     * Two suffixes on a three-letter root is where stacking stops being
     * morphology: "repetitious" came out as re|pet|it|ious, because "pet" plus
     * -ity plus -ous happens to spell it. Four letters keeps every real case
     * (care|less|ness, thank|ful|ly, nation|al|ity) and costs the nonsense.
     */
    static const size_t MIN_STACK_ROOT = 4;

    /**
     * MIN_STACK_SUFFIX: Shortest suffix that may take part in a chain
     *
     * -y is the only suffix of one letter, and in a chain it spells almost
     * anything: "family" came out as fame plus -y plus -ly, which is
     * orthographically perfect and morphologically nonsense. Chains of two
     * real suffixes are all two letters or longer (-al -ity, -less -ness,
     * -ful -ly), so -y is kept to a single layer, where it still gives
     * `cloud|y` and `risk|y`.
     */
    static const size_t MIN_STACK_SUFFIX = 2;

    /** DerivedBase: a lexeme plus the derivational suffixes stacked on it */
    struct DerivedBase {
        std::shared_ptr<Lexeme> root;
        // One entry per layer, innermost first: the suffix, and the part of
        // speech that layer produces.
        std::vector<std::pair<std::string, PartOfSpeech>> layers;
        float weight = 0.0f;

        PartOfSpeech derived_pos() const { return layers.back().second; }
    };

    /** StemCandidate: a spelling to look up, and the alternation it undoes */
    struct StemCandidate {
        std::string stem;
        // The orthographic class the stem has to belong to for this candidate
        // to be the right reading, or UNKNOWN for no requirement.
        StemClass required = StemClass::UNKNOWN;
    };

    /**
     * candidate_fits: Does this lexeme really undergo the alternation?
     *
     * Either the spelling or the stated stem class may say so, which is the
     * union of the two tests this pass used to apply, so no reading it ever
     * produced is withdrawn. The spelling half is what the class cannot
     * supply: "base" is listed as a regular noun, because its plural is
     * "bases" and nothing about the plural involves the silent e, and
     * "bas|ic" needs that e restored all the same.
     *
     * Consonant doubling is the exception and still needs the class. Whether a
     * stem doubles depends on stress, which the spelling does not show, so
     * "run" gives "runner" because the lexicon says DOUBLE_CONS and "open"
     * does not give "openner" because it does not. That is decision 1 in
     * docs/LEXICON.md, applied to derivation.
     */
    static bool candidate_fits(const StemCandidate& cand, const Lexeme& lex) {
        switch (cand.required) {
            case StemClass::Y_FINAL:
                return orthography::has_consonant_y(cand.stem) ||
                       lex.stem_class() == StemClass::Y_FINAL;
            case StemClass::SILENT_E:
                return orthography::has_silent_e(cand.stem) ||
                       orthography::derivational_class(lex) == StemClass::SILENT_E;
            case StemClass::DOUBLE_CONS:
                return lex.stem_class() == StemClass::DOUBLE_CONS;
            default:
                return true;
        }
    }

    /**
     * stem_candidates: Spellings a derivational base could have in the lexicon
     *
     * The suffix is stripped off a surface word, and what is left may not be
     * how the lexicon spells the stem, because attaching the suffix changed
     * the spelling. The three alternations are the same ones inflection uses,
     * and the reversal is the same reversal: "happi" was "happy", "us" was
     * "use", "runn" was "run".
     *
     * Silent e is restored for the vowel-initial suffixes, which are the ones
     * that delete it, and otherwise only for the words English really does
     * spell that way: `argu|ment`, `judg|ment`, `whol|ly`. The pass used to
     * restore it for every suffix, which is where `on|ly` came from, off "one"
     * plus -ly, and `Mar|y` off "mare". Narrowing it withdrew 227 readings
     * from the regression baseline, ten of which were real words and are now
     * the exception list in orthography.h and 217 of which were forms like
     * "hopness" and "safly" that no English text contains. docs/LEXICON.md
     * decision 30 lists them.
     *
     * Consonant doubling is restricted to the vowel-initial suffixes, where it
     * is the only place it happens: "run" plus -er is "runner" and "run" plus
     * -ness is not "runnness".
     */
    static std::vector<StemCandidate> stem_candidates(const std::string& base,
                                                      const DerivationRule& rule) {
        std::vector<StemCandidate> out;
        out.push_back({base, StemClass::UNKNOWN});

        // happi -> happy
        if (rule.needs_y_restoration() && !base.empty() && base.back() == 'i') {
            std::string y = base;
            y.back() = 'y';
            out.push_back({y, StemClass::Y_FINAL});
        }

        // us -> use
        if (orthography::takes_vowel_initial_suffix(rule.suffix()) ||
            orthography::deletes_e_before_consonant(base + rule.suffix())) {
            out.push_back({base + "e", StemClass::SILENT_E});
        }

        // runn -> run
        if (orthography::takes_vowel_initial_suffix(rule.suffix()) &&
            base.size() >= 2 && base[base.size() - 1] == base[base.size() - 2]) {
            out.push_back({base.substr(0, base.size() - 1),
                           StemClass::DOUBLE_CONS});
        }
        return out;
    }

    /**
     * stem_at_least_as_frequent: Is the stem no rarer than the whole word?
     *
     * The frequency condition DerivationRule::FREQUENT_STEM states, asked of
     * the two lexemes involved. A derivation that the usage supports has the
     * stem at least as common as the derivative, because the derivative is one
     * of the things the stem is used for: `cloud` outranks `cloudy`, `rain`
     * outranks `rainy`. Where the derivative is the commoner word the boundary
     * is not where the language is any more.
     *
     * An unknown weight passes rather than fails, which is decision 32: the
     * condition is there to act on evidence, and the built-in lists carry no
     * weights at all. A whole word that is not a lexeme passes for the same
     * reason, since there is nothing to compare.
     *
     * @param stem  The lexeme the suffix would attach to
     * @param form  The derived surface form, lowercased
     */
    bool stem_at_least_as_frequent(const Lexeme& stem,
                                   const std::string& form) const {
        if (!stem.weight_known()) return true;
        bool found = false;
        float whole = 0.0f;
        for (const auto& lex : lexicon_->lookup_all(form)) {
            if (!lex->weight_known()) continue;
            if (!found || lex->weight() > whole) {
                whole = lex->weight();
                found = true;
            }
        }
        if (!found) return true;
        return stem.weight() >= whole;
    }

    /**
     * derive_bases: Every way `form` can be read as a stem plus suffixes
     *
     * Recursive, so that a base can itself be derived: "nationality" is
     * "national" plus -ity, and "national" is "nation" plus -al. The recursion
     * is bounded by MAX_DERIV_LAYERS, and the inner reading has to produce a
     * part of speech the outer suffix attaches to, which is what stops
     * `care|less|ness` from also being read as a noun plus -less plus -ness.
     *
     * A derived base is not in the lexicon, so there is nothing to read its
     * orthography off but its spelling. That is enough: a derived stem stores
     * no irregular form and its final consonant does not double, because the
     * suffix it already carries holds the stress.
     *
     * @param form   The surface form to decompose, lowercased
     * @param depth  How many layers are already stacked, 0 at the top
     */
    std::vector<DerivedBase> derive_bases(const std::string& form,
                                          size_t depth) const {
        std::vector<DerivedBase> out;
        if (depth >= MAX_DERIV_LAYERS) return out;

        for (const auto& drule : deriv_rules_) {
            const std::string& suf = drule->suffix();
            if (form.size() <= suf.size()) continue;
            if (form.compare(form.size() - suf.size(), suf.size(), suf) != 0) {
                continue;
            }
            const std::string base = form.substr(0, form.size() - suf.size());

            // A closed-class word has no morphology, so a suffix that carries
            // the condition does not read one. Only -y does: see the
            // Restriction values in RULES/derivation_rule.h.
            if (drule->has(DerivationRule::NOT_CLOSED_CLASS) &&
                closed_class_ && closed_class_->contains(form)) {
                continue;
            }

            for (const auto& cand : stem_candidates(base, *drule)) {
                // The base is a lexeme: the bottom of the chain
                for (const auto& lex : lexicon_->lookup_all(cand.stem)) {
                    if (!drule->accepts_pos(lex->pos())) continue;
                    if (!drule->applies_to(lex->stem_class())) continue;
                    if (lex->stem().size() < drule->min_stem_length()) continue;
                    if (!candidate_fits(cand, *lex)) continue;
                    if (drule->has(DerivationRule::FREQUENT_STEM) &&
                        !stem_at_least_as_frequent(*lex, form)) {
                        continue;
                    }
                    DerivedBase db;
                    db.root = lex;
                    db.layers.push_back({suf, drule->derived_pos()});
                    db.weight = lex->weight() * 0.9f;
                    out.push_back(db);
                }

                // The base is itself derived: one layer further down. A derived
                // form is not in the lexicon, so its orthography comes from
                // its spelling, and its final consonant does not double,
                // because the suffix it already carries holds the stress.
                if (cand.required == StemClass::DOUBLE_CONS) continue;
                if (suf.size() < MIN_STACK_SUFFIX) continue;
                if (cand.required != StemClass::UNKNOWN &&
                    orthography::implied_class(cand.stem) != cand.required) {
                    continue;
                }
                if (cand.stem.size() < drule->min_stem_length()) continue;
                for (auto& inner : derive_bases(cand.stem, depth + 1)) {
                    if (!drule->accepts_pos(inner.derived_pos())) continue;
                    if (inner.root->stem().size() < MIN_STACK_ROOT) continue;
                    bool short_layer = false;
                    for (const auto& layer : inner.layers) {
                        if (layer.first.size() < MIN_STACK_SUFFIX) {
                            short_layer = true;
                        }
                    }
                    if (short_layer) continue;
                    DerivedBase db;
                    db.root = inner.root;
                    db.layers = inner.layers;
                    db.layers.push_back({suf, drule->derived_pos()});
                    db.weight = inner.weight * 0.9f;
                    out.push_back(db);
                }
            }
        }
        return out;
    }

    /** DerivedSurface: a derived form's spelling, and how it was reached */
    struct DerivedSurface {
        std::string form;
        // Which alternation the inflectional suffix outside the chain caused.
        // NONE means the suffix was simply appended to the derived form.
        enum class Alternation { NONE, Y_TO_I, E_DELETION } alternation;
    };

    /**
     * derived_surfaces: Spellings of the derived form inside an inflected word
     *
     * `head` is the surface up to the inflectional suffix, so "nationaliti"
     * for "nationalities" and "nationaliz" for "nationalizing". The derived
     * form that was inflected is therefore "nationality" or "nationalize", and
     * recovering it is the same reversal the inflectional passes do on a real
     * lexeme.
     */
    static std::vector<DerivedSurface> derived_surfaces(const std::string& head,
                                                        const std::string& infl) {
        std::vector<DerivedSurface> out;
        out.push_back({head, DerivedSurface::Alternation::NONE});
        if (infl.empty()) return out;

        if (!head.empty() && head.back() == 'i') {
            std::string y = head;
            y.back() = 'y';
            if (orthography::has_consonant_y(y)) {
                out.push_back({y, DerivedSurface::Alternation::Y_TO_I});
            }
        }
        const std::string e = head + "e";
        if (orthography::has_silent_e(e)) {
            out.push_back({e, DerivedSurface::Alternation::E_DELETION});
        }
        return out;
    }

    /**
     * derived_inflection: The features an inflectional suffix marks on a
     * derived form
     *
     * The derived form has no lexicon entry, so its orthography comes from its
     * spelling and nothing blocks the productive rule, which is the one real
     * difference from match_suffix_to_features: a derived stem never stores an
     * irregular plural or past.
     *
     * A derived adjective gets no comparative or superlative. English forms
     * those with "more" and "most" once a suffix is involved: not "nationaler"
     * or "basicer" or "carefuler". This is also why a built lexicon classes 28%
     * of its adjectives UNKNOWN, with no synthetic comparative listed. Where a
     * derived adjective does take -er, as "cloudy" gives "cloudier", the
     * derived form is a lexeme in its own right and the ordinary inflectional
     * path reaches it. Leaving the two in cost three readings on the regression
     * baseline, all of them a tie at 0.9 that the piece-count tie-break then
     * settled the wrong way: "noticer" came out as "not|ic|er" and "signaler"
     * as "sign|al|er".
     *
     * @param form         The derived form, as it would be spelled
     * @param alternation  Which reversal produced that spelling
     * @param pos          The part of speech the outermost suffix produced
     * @param infl         The inflectional suffix as it appears on the surface
     */
    static std::vector<std::string> derived_inflection(
            const std::string& form, DerivedSurface::Alternation alternation,
            PartOfSpeech pos, const std::string& infl) {
        const StemClass sc = orthography::implied_class(form);
        const bool noun = pos == PartOfSpeech::NOUN;
        const bool verb = pos == PartOfSpeech::VERB;

        switch (alternation) {
            case DerivedSurface::Alternation::NONE:
                if (noun) {
                    if (sc == StemClass::SIBILANT) {
                        if (infl == "es") return {"+PL"};
                    } else if (sc != StemClass::Y_FINAL) {
                        if (infl == "s") return {"+PL"};
                    }
                } else if (verb) {
                    if (infl == "s" && (sc == StemClass::REG ||
                                        sc == StemClass::SILENT_E)) {
                        return {"+3SG"};
                    }
                    if (infl == "es" && sc == StemClass::SIBILANT) return {"+3SG"};
                    if (infl == "d" && sc == StemClass::SILENT_E) {
                        return {"+PAST", "+PAST_PART"};
                    }
                    if (infl == "ed" && (sc == StemClass::REG ||
                                         sc == StemClass::SIBILANT)) {
                        return {"+PAST", "+PAST_PART"};
                    }
                    if (infl == "ing" && sc != StemClass::SILENT_E) {
                        return {"+PRES_PART"};
                    }
                }
                break;

            case DerivedSurface::Alternation::Y_TO_I:
                if (noun && infl == "es") return {"+PL"};
                if (verb && infl == "es") return {"+3SG"};
                if (verb && infl == "ed") return {"+PAST", "+PAST_PART"};
                break;

            case DerivedSurface::Alternation::E_DELETION:
                if (verb && infl == "ing") return {"+PRES_PART"};
                if (verb && infl == "ed") return {"+PAST", "+PAST_PART"};
                break;
        }
        return {};
    }

    /**
     * analyze_derivational: Every derivational reading of one word
     *
     * Up to two derivational suffixes, and an inflectional suffix outside
     * them: "governments" is govern plus -ment plus +PL, "nationalities" is
     * nation plus -al plus -ity plus +PL. The inflectional suffix is at most
     * three characters, which is every one English has.
     *
     * @param normalized  The lowercased input word
     * @param results     Accumulator for the word's analyses
     */
    void analyze_derivational(const std::string& normalized,
                              std::vector<Analysis>& results) const {
        const size_t max_infl = 3;

        for (size_t tail = 0; tail <= max_infl; tail++) {
            if (tail + 1 >= normalized.size()) break;
            const std::string head = normalized.substr(0, normalized.size() - tail);
            const std::string infl = normalized.substr(normalized.size() - tail);

            for (const auto& surface : derived_surfaces(head, infl)) {
                for (const auto& db : derive_bases(surface.form, 0)) {
                    const auto tags = derived_inflection(
                        surface.form, surface.alternation, db.derived_pos(),
                        infl);
                    if (tail > 0 && tags.empty()) continue;

                    // +PAST and +PAST_PART are two analyses of one form
                    const size_t count = tags.empty() ? 1 : tags.size();
                    for (size_t i = 0; i < count; i++) {
                        Analysis a(db.root->stem(), db.root->pos(),
                                   tags.empty()
                                       ? std::vector<std::string>{}
                                       : std::vector<std::string>{tags[i]});
                        for (const auto& layer : db.layers) {
                            a.add_derivation("-" + layer.first, layer.second);
                        }
                        a.set_weight(db.weight);
                        results.push_back(a);
                    }
                }
            }
        }
    }

    /**
     * emit: Record one inflected analysis unless the lexicon blocks it
     *
     * The reversal passes in analyze() reach a lexeme through an orthographic
     * alternation and then assert a feature. A lexeme that stores that feature
     * irregularly blocks the productive rule, exactly as in
     * match_suffix_to_features below: "choose" reverses its silent e to give
     * "choosing", while "choosed" is blocked by the stored "chose".
     *
     * @param results  Accumulator for the word's analyses
     * @param lexeme   The lexeme the reversal found
     * @param stem     The restored lemma to report
     * @param pos      Part of speech of the analysis
     * @param tag      The feature tag the rule produces
     */
    static void emit(std::vector<Analysis>& results,
                     const std::shared_ptr<Lexeme>& lexeme,
                     const std::string& stem, PartOfSpeech pos,
                     const std::string& tag) {
        if (orthography::stores_feature(*lexeme, tag)) return;
        Analysis a(stem, pos, {tag});
        a.set_weight(lexeme->weight());
        results.push_back(a);
    }

    /**
     * match_suffix_to_features: Which features does this surface suffix mark?
     *
     * The stem class decides HOW a suffix attaches, not WHETHER the feature
     * exists, so the test on it names the classes whose surface form really is
     * stem + suffix. A silent-e verb takes a plain -s ("hopes"), so does a
     * doubling verb ("stops"), a sibilant verb takes a plain -ed ("fixed"),
     * and an irregular verb still inflects regularly wherever it stores
     * nothing ("breaks", "breaking", "goes"). The classes whose surface form
     * is something else are still excluded, and they are reached by the
     * reversal passes in analyze() instead: Y_FINAL through "ies" and "ied",
     * DOUBLE_CONS through the doubled consonant, SILENT_E through the deleted
     * e.
     *
     * Lexical storage blocks the productive rule for the feature it stores.
     * "break" holds "broke" and "broken", so -ed yields nothing; it holds no
     * third person singular, so -s yields +3SG. Noun plurals are the one
     * exception: that path accepts -s on any noun, including the zero-plural
     * and umlaut classes, and narrowing it would withdraw analyses that the
     * output contract fixes in place. See docs/LEXICON.md.
     */
    std::vector<std::string> match_suffix_to_features(const std::string& suffix,
                                                       std::shared_ptr<Lexeme> lexeme) {
        std::vector<std::string> features;
        // Irregular lexemes spend stem_class() on the irregularity, so their
        // suffixation is read off the spelling instead. Regular lexemes keep
        // the class they state.
        const StemClass sc = orthography::effective_class(*lexeme);

        // Classes whose plain -s is the real third person singular. Y_FINAL
        // surfaces as "ies" and SIBILANT as "es", so both are left out here.
        const bool plain_s = sc == StemClass::REG ||
                             sc == StemClass::SILENT_E ||
                             sc == StemClass::DOUBLE_CONS;

        // An irregular stem that might double its final consonant does not
        // record whether it does, so a vowel-initial suffix on the undoubled
        // stem is not accepted: "rid" gives "ridding", so "riding" must stay
        // the participle of "ride" alone. A plain -s doubles nothing, so
        // "rids" is unaffected. See orthography.h.
        const bool doubling_unknown =
            !::is_regular(lexeme->stem_class()) &&
            orthography::may_double_final_consonant(lexeme->stem());

        if (lexeme->pos() == PartOfSpeech::NOUN) {
            // UNKNOWN is the one class that blocks this, and it is the only
            // place anything blocks it: see the note on the noun plural path
            // above. A generated lexicon writes UNKNOWN when the source lists
            // no plural at all, which is how an uncountable noun arrives, and
            // no hand-written entry uses it.
            if (sc != StemClass::UNKNOWN && (suffix == "s" || suffix == "es")) {
                features.push_back("+PL");
            }
        } else if (lexeme->pos() == PartOfSpeech::VERB) {
            if (suffix == "s" && plain_s) {
                features.push_back("+3SG");
            } else if (suffix == "es" && sc == StemClass::SIBILANT) {
                features.push_back("+3SG");
            } else if (suffix == "ed" && !doubling_unknown &&
                       (sc == StemClass::REG ||
                        sc == StemClass::SILENT_E ||
                        sc == StemClass::SIBILANT)) {
                features.push_back("+PAST");
                features.push_back("+PAST_PART");
            } else if (suffix == "ing" && !doubling_unknown &&
                       (sc == StemClass::REG ||
                        sc == StemClass::Y_FINAL ||
                        sc == StemClass::SIBILANT)) {
                features.push_back("+PRES_PART");
            }
        } else if (lexeme->pos() == PartOfSpeech::ADJECTIVE) {
            if (suffix == "er" && sc == StemClass::REG) {
                features.push_back("+COMP");
            } else if (suffix == "est" && sc == StemClass::REG) {
                features.push_back("+SUP");
            }
        }

        // Blocking, except on the noun plural path noted above
        if (lexeme->pos() != PartOfSpeech::NOUN) {
            std::vector<std::string> kept;
            for (const auto& tag : features) {
                if (!orthography::stores_feature(*lexeme, tag)) kept.push_back(tag);
            }
            features = std::move(kept);
        }

        return features;
    }

    std::vector<Analysis> analyze_suffix(const std::string& suffix,
                                         std::shared_ptr<Lexeme> lexeme) {
        std::vector<Analysis> results;

        if (suffix.empty()) {
            Analysis a(lexeme->stem(), lexeme->pos(), {});
            a.set_weight(lexeme->weight());
            results.push_back(a);
            return results;
        }

        auto features = match_suffix_to_features(suffix, lexeme);

        if (!features.empty()) {
            // Each feature gets its own analysis (for +PAST vs +PAST_PART)
            if (features.size() == 1) {
                Analysis a(lexeme->stem(), lexeme->pos(), features);
                a.set_weight(lexeme->weight());
                results.push_back(a);
            } else {
                for (const auto& feat : features) {
                    Analysis a(lexeme->stem(), lexeme->pos(), {feat});
                    a.set_weight(lexeme->weight());
                    results.push_back(a);
                }
            }
        }

        return results;
    }
};

#endif // ANALYZER_H
