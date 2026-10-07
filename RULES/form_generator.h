#ifndef FORM_GENERATOR_H
#define FORM_GENERATOR_H

#include "../LEXICON/lexeme.h"
#include "../LEXICON/stem_class.h"
#include "derivation_rule.h"
#include "orthography.h"

#include <memory>
#include <string>
#include <vector>

/**
 * form_generator.h: Surface forms for a lexeme, with provenance
 *
 * The inflectional FSTs in RULES/ are built for recognition and expose no
 * generation entry point, so the forms here are produced from the lexeme's
 * stem class, which is the same orthography the analyzer reverses during
 * analysis. Irregular forms come straight out of the lexeme's stored map and
 * derivational forms from the DerivationRule objects the configuration
 * registers, so those two groups come from the existing rule data directly.
 *
 * This is the generation counterpart of analysis, and it exists so that the
 * analyzer can be asked to read its own output back: anything generated here
 * and then rejected by Analyzer::analyze is a gap in the rules or in the
 * lexicon, not a property of real text. Both the property test in
 * test_segmentation.cpp and the analyzer's --selfcheck mode call it, so the
 * two can never drift apart.
 *
 * Every form carries the lemma, part of speech, stem class and the feature it
 * was generated for, which is what makes the --selfcheck report readable.
 */

/** GeneratedForm: one surface form plus where it came from */
struct GeneratedForm {
    std::string surface;     // The generated surface string
    std::string lemma;       // Stem of the lexeme it came from
    PartOfSpeech pos;        // Part of speech of that lexeme
    StemClass stem_class;    // Stem class of that lexeme
    std::string origin;      // "citation", "+PL", "deriv -ness", "prefix un-", ...

    GeneratedForm(const std::string& surf, const std::shared_ptr<Lexeme>& lex,
                  const std::string& org)
        : surface(surf), lemma(lex->stem()), pos(lex->pos()),
          stem_class(lex->stem_class()), origin(org) {}
};

/**
 * generate_forms: Expand one lexeme into its surface forms
 *
 * The set covers the citation form, the inflections its stem class and part of
 * speech allow, every stored irregular form, every applicable derivation, and
 * two prefixed variants. The prefixed variants are not claims about English
 * (there is no word "reanimal"); they exercise the prefix piece on real stems.
 *
 * Three things are deliberately not generated, because the lexicon does not
 * determine them and inventing them would mean asserting that a non-word is
 * English:
 *
 *   1. A regular form for a feature the lexeme stores irregularly. "break"
 *      holds "broke", so no "breaked"; "far" holds "farther", so no "farer".
 *      This is the generation side of the blocking the analyzer applies.
 *
 *   2. A vowel-initial suffix on an irregular stem that might double its final
 *      consonant. "put" and "begin" would give "puting" and "begining", and
 *      the stem class of an irregular lexeme does not record whether doubling
 *      applies, so those forms are skipped. The -s forms are still generated:
 *      a plain -s never doubles anything, so "puts" and "begins" are certain.
 *
 *   3. A y-to-i variant on a stem that is not Y_FINAL. "delay" is a regular
 *      verb that happens to end in y, so its agent noun is "delayer", not
 *      "delaier", and only the Y_FINAL class undergoes the alternation.
 *
 * @param lex          The lexeme to expand
 * @param deriv_rules  The derivational rules the configuration registered
 * @return             Generated forms, each tagged with its provenance
 */
inline std::vector<GeneratedForm> generate_forms(
        const std::shared_ptr<Lexeme>& lex,
        const std::vector<std::shared_ptr<DerivationRule>>& deriv_rules) {

    const std::string s = lex->stem();
    const StemClass sc  = lex->stem_class();
    std::vector<GeneratedForm> forms;

    forms.emplace_back(s, lex, "citation");

    if (s.empty()) return forms;

    const std::string no_last = s.substr(0, s.size() - 1);
    const char last = s.back();

    // How suffixes attach: the stated class for a regular lexeme, the spelling
    // for an irregular one, whose class is spent on the irregularity.
    const StemClass eff = orthography::effective_class(*lex);

    // An irregular stem that might double its final consonant leaves -ing and
    // -ed undetermined, as above.
    const bool doubling_unknown = !::is_regular(sc) &&
                                  orthography::may_double_final_consonant(s);

    // add: generate one form unless the lexicon already stores that feature
    auto add = [&](const std::string& surface, const std::string& tag) {
        if (orthography::stores_feature(*lex, tag)) return;
        forms.emplace_back(surface, lex, tag);
    };

    // UNKNOWN states that no inflectional paradigm was established: an
    // uncountable noun, a non-gradable adjective. No rule matches it, so
    // nothing is generated for it either.
    const PartOfSpeech paradigm = (eff == StemClass::UNKNOWN)
                                      ? PartOfSpeech::FUNCTION
                                      : lex->pos();

    switch (paradigm) {
        case PartOfSpeech::NOUN:
            if (eff == StemClass::SIBILANT) add(s + "es", "+PL");
            else if (eff == StemClass::Y_FINAL) add(no_last + "ies", "+PL");
            else add(s + "s", "+PL");
            break;

        case PartOfSpeech::VERB:
            if (eff == StemClass::SIBILANT) {
                add(s + "es", "+3SG");
                if (!doubling_unknown) {
                    add(s + "ed", "+PAST");
                    add(s + "ing", "+PRES_PART");
                }
            } else if (eff == StemClass::Y_FINAL) {
                add(no_last + "ies", "+3SG");
                add(no_last + "ied", "+PAST");
                add(s + "ing", "+PRES_PART");
            } else if (eff == StemClass::DOUBLE_CONS) {
                add(s + "s", "+3SG");
                add(s + last + "ed", "+PAST");
                add(s + last + "ing", "+PRES_PART");
            } else if (eff == StemClass::SILENT_E) {
                add(s + "s", "+3SG");
                if (!doubling_unknown) {
                    add(s + "d", "+PAST");
                    add(no_last + "ed", "+PAST");
                    add(no_last + "ing", "+PRES_PART");
                }
            } else {
                add(s + "s", "+3SG");
                if (!doubling_unknown) {
                    add(s + "ed", "+PAST");
                    add(s + "ing", "+PRES_PART");
                }
            }
            break;

        case PartOfSpeech::ADJECTIVE:
            if (eff == StemClass::Y_FINAL) {
                add(no_last + "ier", "+COMP");
                add(no_last + "iest", "+SUP");
            } else if (eff == StemClass::DOUBLE_CONS) {
                add(s + last + "er", "+COMP");
                add(s + last + "est", "+SUP");
            } else if (eff == StemClass::SILENT_E) {
                add(no_last + "er", "+COMP");
                add(no_last + "est", "+SUP");
            } else if (!doubling_unknown) {
                add(s + "er", "+COMP");
                add(s + "est", "+SUP");
            }
            break;

        case PartOfSpeech::ADVERB:
        case PartOfSpeech::FUNCTION:   // also the UNKNOWN class, see above
        default:
            break;
    }

    // Stored irregular forms: mice, went, children, better
    for (const auto& [feat, form] : lex->irregular_forms_map()) {
        forms.emplace_back(form, lex, "irregular " + feature_to_tag(feat));
    }

    // Derivational forms, from the registered rules. The class tests mirror
    // the ones the analyzer applies when it strips these suffixes.
    for (const auto& rule : deriv_rules) {
        if (lex->pos() != rule->source_pos()) continue;
        if (!rule->applies_to(sc)) continue;
        const std::string tag = "deriv -" + rule->suffix();
        forms.emplace_back(s + rule->suffix(), lex, tag);
        if (rule->needs_y_restoration() && sc == StemClass::Y_FINAL && last == 'y') {
            forms.emplace_back(no_last + "i" + rule->suffix(), lex, tag);
        }
        if (sc == StemClass::SILENT_E && last == 'e') {
            forms.emplace_back(no_last + rule->suffix(), lex, tag);
        }
    }

    // Prefixed variants, exercising the prefix piece on real stems. A
    // derivational prefix does not attach to a closed-class word, and the
    // analyzer declines to strip one from it, so none is generated either.
    // The analyzer also declines to leave a remainder shorter than two
    // characters, so a one-letter stem gets no prefixed variant: a built
    // lexicon contains the single letters as nouns, and "una" is not a word.
    if (lex->pos() != PartOfSpeech::FUNCTION && s.size() >= 2) {
        forms.emplace_back("un" + s, lex, "prefix un-");
        forms.emplace_back("re" + s, lex, "prefix re-");
    }

    return forms;
}

#endif // FORM_GENERATOR_H
