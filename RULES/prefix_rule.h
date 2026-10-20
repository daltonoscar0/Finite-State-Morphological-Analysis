#ifndef PREFIX_RULE_H
#define PREFIX_RULE_H

#include "../LEXICON/lexeme.h"

#include <string>
#include <vector>

/**
 * prefix_rule.h: The prefix inventory, and what each prefix attaches to
 *
 * Prefix stripping used to work from a bare list of fifteen strings, with no
 * record of what each one can attach to. That was enough for the analyzer,
 * which tries a prefix against every analysis of the remainder and lets the
 * ranking sort it out, and it was not enough for the guard on the surface
 * segmentation: without the part of speech there is no way to say that "co-"
 * makes a noun out of a noun while "re-" makes a verb out of a verb, so
 * nothing stopped a prefix reading that no rule of English would license.
 *
 * The inventory here is the same fifteen prefixes in the same order, with the
 * open classes each attaches to written down. The categories are the ones the
 * prefix takes as input:
 *
 *   un-     unhappy, unkind (adjective), undo, unlock (verb),
 *           unhappiness, untruth, unrest (noun)
 *   re-     rewrite, rebuild, reread (verb)
 *   pre-    prewar (noun), preheat (verb), premature (adjective)
 *   dis-    disagree (verb), dishonest (adjective), disadvantage (noun)
 *   mis-    misspell (verb), misfortune (noun)
 *   over-   overeat (verb), overactive (adjective), overcoat (noun)
 *   under-  underestimate (verb), underpaid (adjective), underdog (noun)
 *   non-    nonsense (noun), nonverbal (adjective)
 *   anti-   antifreeze (noun), antisocial (adjective)
 *   de-     deactivate, decode (verb)
 *   out-    outperform, outlive (verb)
 *   up-     upgrade, uphold (verb)
 *   sub-    subgroup (noun), sublet (verb), subhuman (adjective)
 *   inter-  interact (verb), international (adjective)
 *   co-     coauthor (noun), coexist (verb)
 *
 * No prefix here attaches to an adverb, and none attaches to a function word,
 * which is what Analyzer::analyze already tested for separately so that "unto"
 * did not come apart as un- plus "to".
 *
 * The analyzer still strips a prefix without consulting the categories, so the
 * lexical analysis output is unchanged by this file. Only the guard in
 * ANALYSIS/segmentation.h reads them.
 */
struct PrefixRule {
    std::string form;
    std::vector<PartOfSpeech> attaches;
};

/** english_prefix_rules: the inventory, in the order prefixes are tried */
inline const std::vector<PrefixRule>& english_prefix_rules() {
    static const std::vector<PrefixRule> rules = {
        {"un",    {PartOfSpeech::ADJECTIVE, PartOfSpeech::VERB,
                   PartOfSpeech::NOUN}},
        {"re",    {PartOfSpeech::VERB}},
        {"pre",   {PartOfSpeech::NOUN, PartOfSpeech::VERB,
                   PartOfSpeech::ADJECTIVE}},
        {"dis",   {PartOfSpeech::VERB, PartOfSpeech::ADJECTIVE,
                   PartOfSpeech::NOUN}},
        {"mis",   {PartOfSpeech::VERB, PartOfSpeech::NOUN}},
        {"over",  {PartOfSpeech::VERB, PartOfSpeech::ADJECTIVE,
                   PartOfSpeech::NOUN}},
        {"under", {PartOfSpeech::VERB, PartOfSpeech::ADJECTIVE,
                   PartOfSpeech::NOUN}},
        {"non",   {PartOfSpeech::NOUN, PartOfSpeech::ADJECTIVE}},
        {"anti",  {PartOfSpeech::NOUN, PartOfSpeech::ADJECTIVE}},
        {"de",    {PartOfSpeech::VERB}},
        {"out",   {PartOfSpeech::VERB}},
        {"up",    {PartOfSpeech::VERB}},
        {"sub",   {PartOfSpeech::NOUN, PartOfSpeech::VERB,
                   PartOfSpeech::ADJECTIVE}},
        {"inter", {PartOfSpeech::VERB, PartOfSpeech::ADJECTIVE}},
        {"co",    {PartOfSpeech::NOUN, PartOfSpeech::VERB}}
    };
    return rules;
}

/**
 * prefix_attaches: Does `prefix` take a base of this part of speech?
 *
 * A prefix the inventory does not list attaches to nothing, so a reading
 * carrying one is not licensed. Nothing produces such a reading today, since
 * the analyzer strips from the same inventory.
 */
inline bool prefix_attaches(const std::string& prefix, PartOfSpeech pos) {
    for (const auto& rule : english_prefix_rules()) {
        if (rule.form != prefix) continue;
        for (const auto& allowed : rule.attaches) {
            if (allowed == pos) return true;
        }
        return false;
    }
    return false;
}

#endif // PREFIX_RULE_H
