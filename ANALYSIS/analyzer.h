#ifndef ANALYZER_H
#define ANALYZER_H

#include "analysis.h"
#include "surface_fsa.h"
#include "tokenizer.h"
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

    // English prefixes for prefix stripping
    static const std::vector<std::string>& english_prefixes() {
        static const std::vector<std::string> prefixes = {
            "un", "re", "pre", "dis", "mis", "over", "under",
            "non", "anti", "de", "out", "up", "sub", "inter", "co"
        };
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
                    if (lexeme->stem_class() == StemClass::Y_FINAL) {
                        // Noun plural: city -> cities (suffix = "es")
                        if (lexeme->pos() == PartOfSpeech::NOUN && suffix == "es") {
                            Analysis a(y_stem, PartOfSpeech::NOUN, {"+PL"});
                            a.set_weight(lexeme->weight());
                            results.push_back(a);
                        }
                        // Verb 3SG: carry -> carries (suffix = "es")
                        else if (lexeme->pos() == PartOfSpeech::VERB) {
                            if (suffix == "es") {
                                Analysis a(y_stem, PartOfSpeech::VERB, {"+3SG"});
                                a.set_weight(lexeme->weight());
                                results.push_back(a);
                            }
                            if (suffix == "ed") {
                                Analysis a1(y_stem, PartOfSpeech::VERB, {"+PAST"});
                                a1.set_weight(lexeme->weight());
                                results.push_back(a1);
                                Analysis a2(y_stem, PartOfSpeech::VERB, {"+PAST_PART"});
                                a2.set_weight(lexeme->weight());
                                results.push_back(a2);
                            }
                        }
                        // Adjective comparative/superlative: happy -> happier/happiest
                        else if (lexeme->pos() == PartOfSpeech::ADJECTIVE) {
                            if (suffix == "er") {
                                Analysis a(y_stem, PartOfSpeech::ADJECTIVE, {"+COMP"});
                                a.set_weight(lexeme->weight());
                                results.push_back(a);
                            }
                            if (suffix == "est") {
                                Analysis a(y_stem, PartOfSpeech::ADJECTIVE, {"+SUP"});
                                a.set_weight(lexeme->weight());
                                results.push_back(a);
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
                                Analysis a1(undoubled, PartOfSpeech::VERB, {"+PAST"});
                                a1.set_weight(lex->weight());
                                results.push_back(a1);
                                Analysis a2(undoubled, PartOfSpeech::VERB, {"+PAST_PART"});
                                a2.set_weight(lex->weight());
                                results.push_back(a2);
                            } else if (suffix == "ing") {
                                Analysis a(undoubled, PartOfSpeech::VERB, {"+PRES_PART"});
                                a.set_weight(lex->weight());
                                results.push_back(a);
                            }
                        }
                        if (lex->pos() == PartOfSpeech::ADJECTIVE) {
                            if (suffix == "er") {
                                Analysis a(undoubled, PartOfSpeech::ADJECTIVE, {"+COMP"});
                                a.set_weight(lex->weight());
                                results.push_back(a);
                            }
                            if (suffix == "est") {
                                Analysis a(undoubled, PartOfSpeech::ADJECTIVE, {"+SUP"});
                                a.set_weight(lex->weight());
                                results.push_back(a);
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
                    if (lex->stem_class() == StemClass::SILENT_E) {
                        if (lex->pos() == PartOfSpeech::VERB) {
                            if (suffix == "ing") {
                                Analysis a(e_stem, PartOfSpeech::VERB, {"+PRES_PART"});
                                a.set_weight(lex->weight());
                                results.push_back(a);
                            }
                            if (suffix == "ed") {
                                Analysis a1(e_stem, PartOfSpeech::VERB, {"+PAST"});
                                a1.set_weight(lex->weight());
                                results.push_back(a1);
                                Analysis a2(e_stem, PartOfSpeech::VERB, {"+PAST_PART"});
                                a2.set_weight(lex->weight());
                                results.push_back(a2);
                            }
                        }
                        if (lex->pos() == PartOfSpeech::ADJECTIVE) {
                            if (suffix == "er") {
                                Analysis a(e_stem, PartOfSpeech::ADJECTIVE, {"+COMP"});
                                a.set_weight(lex->weight());
                                results.push_back(a);
                            }
                            if (suffix == "est") {
                                Analysis a(e_stem, PartOfSpeech::ADJECTIVE, {"+SUP"});
                                a.set_weight(lex->weight());
                                results.push_back(a);
                            }
                        }
                    }
                }
            }
        }

        // STEP 6: Derivational suffix stripping pass
        for (const auto& drule : deriv_rules_) {
            const std::string& suf = drule->suffix();
            if (normalized.size() > suf.size() &&
                normalized.substr(normalized.size() - suf.size()) == suf) {
                std::string base_candidate = normalized.substr(0, normalized.size() - suf.size());

                // Direct lookup
                auto base_lexemes = lexicon_->lookup_all(base_candidate);
                for (auto& lex : base_lexemes) {
                    if (lex->pos() == drule->source_pos() && drule->applies_to(lex->stem_class())) {
                        Analysis a(lex->stem(), lex->pos(), {});
                        a.set_derivation("-" + suf, drule->derived_pos());
                        a.set_weight(lex->weight() * 0.9f);
                        results.push_back(a);
                    }
                }

                // Y-restoration: "happi" -> "happy"
                if (drule->needs_y_restoration() && !base_candidate.empty() && base_candidate.back() == 'i') {
                    std::string y_base = base_candidate;
                    y_base.back() = 'y';
                    auto y_lexemes = lexicon_->lookup_all(y_base);
                    for (auto& lex : y_lexemes) {
                        if (lex->stem_class() == StemClass::Y_FINAL &&
                            lex->pos() == drule->source_pos() && drule->applies_to(lex->stem_class())) {
                            Analysis a(lex->stem(), lex->pos(), {});
                            a.set_derivation("-" + suf, drule->derived_pos());
                            a.set_weight(lex->weight() * 0.9f);
                            results.push_back(a);
                        }
                    }
                }

                // SILENT_E restoration: "hop" -> "hope" + "ing" -> derivation
                {
                    std::string e_base = base_candidate + "e";
                    auto e_lexemes = lexicon_->lookup_all(e_base);
                    for (auto& lex : e_lexemes) {
                        if (lex->stem_class() == StemClass::SILENT_E &&
                            lex->pos() == drule->source_pos() && drule->applies_to(lex->stem_class())) {
                            Analysis a(lex->stem(), lex->pos(), {});
                            a.set_derivation("-" + suf, drule->derived_pos());
                            a.set_weight(lex->weight() * 0.9f);
                            results.push_back(a);
                        }
                    }
                }
            }
        }

        // STEP 5: Prefix stripping pass
        if (allow_prefix) {
            for (const auto& prefix : english_prefixes()) {
                if (normalized.size() > prefix.size() &&
                    normalized.substr(0, prefix.size()) == prefix) {
                    std::string remainder = normalized.substr(prefix.size());
                    if (remainder.size() >= 2) {
                        auto base = analyze(remainder, false);
                        for (auto& ba : base) {
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

        // STEP 9: Deduplicate (keep first occurrence / highest weight if unordered)
        // Use a seen-set approach since duplicates may not be adjacent
        {
            std::vector<Analysis> deduped;
            for (auto& r : results) {
                bool found = false;
                for (const auto& d : deduped) {
                    if (r == d) { found = true; break; }
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
    std::vector<std::string> match_suffix_to_features(const std::string& suffix,
                                                       std::shared_ptr<Lexeme> lexeme) {
        std::vector<std::string> features;
        auto sc = lexeme->stem_class();

        if (lexeme->pos() == PartOfSpeech::NOUN) {
            if (suffix == "s") {
                features.push_back("+PL");
            } else if (suffix == "es") {
                features.push_back("+PL");
            }
        } else if (lexeme->pos() == PartOfSpeech::VERB) {
            if (suffix == "s" && sc == StemClass::REG) {
                features.push_back("+3SG");
            } else if (suffix == "es" && sc == StemClass::SIBILANT) {
                features.push_back("+3SG");
            } else if (suffix == "ed" && (sc == StemClass::REG || sc == StemClass::SILENT_E)) {
                features.push_back("+PAST");
                features.push_back("+PAST_PART");
            } else if (suffix == "ing" && (sc == StemClass::REG || sc == StemClass::Y_FINAL || sc == StemClass::SIBILANT)) {
                features.push_back("+PRES_PART");
            }
        } else if (lexeme->pos() == PartOfSpeech::ADJECTIVE) {
            if (suffix == "er" && sc == StemClass::REG) {
                features.push_back("+COMP");
            } else if (suffix == "est" && sc == StemClass::REG) {
                features.push_back("+SUP");
            }
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
