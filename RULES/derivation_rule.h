#ifndef DERIVATION_RULE_H
#define DERIVATION_RULE_H

#include "../LEXICON/stem_class.h"
#include "../LEXICON/lexeme.h"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

/**
 * DerivationRule: Encodes a derivational morphological process.
 * Unlike inflectional MorphRule, derivational rules change the POS.
 *
 * A rule states four things: the suffix, the parts of speech it attaches to,
 * the part of speech it produces, and the stem classes it accepts. The
 * analyzer strips the suffix off a surface word and requires the remainder to
 * be a lexeme satisfying all four, which is what keeps precision up: no rule
 * ever fires on a stem the lexicon does not hold. See docs/LEXICON.md.
 */
class DerivationRule {
public:
    virtual ~DerivationRule() = default;
    virtual const std::string& suffix() const = 0;

    /**
     * source_pos: The part of speech this rule is named for
     *
     * A rule that attaches to more than one, like -ful on both "care" and
     * "thank", reports the first of them here and answers accepts_pos for the
     * rest. Only reporting reads this; every test goes through accepts_pos.
     */
    virtual PartOfSpeech source_pos() const = 0;

    /** accepts_pos: Does this rule attach to a stem of this part of speech? */
    virtual bool accepts_pos(PartOfSpeech pos) const { return pos == source_pos(); }

    /** source_poses: Every part of speech this rule attaches to */
    virtual const std::vector<PartOfSpeech>& source_poses() const = 0;

    virtual PartOfSpeech derived_pos() const = 0;
    virtual bool applies_to(StemClass sc) const = 0;
    virtual bool needs_y_restoration() const { return false; }

    /**
     * min_stem_length: Shortest stem the suffix may attach to, in characters
     *
     * A one or two letter "stem" in a mined lexicon is an artifact rather than
     * a word, and a short suffix will find one at the end of almost anything.
     * -y is the extreme case and takes four: without it "army" comes apart as
     * "arm" plus -y.
     */
    virtual size_t min_stem_length() const { return 1; }
};

/**
 * SuffixDerivation: one derivational suffix, stated as data
 *
 * The first five rules below were a class each, which is the right shape for
 * five and the wrong shape for thirty: every one of them would be the same
 * twelve lines with four values changed. They are a table instead, and the
 * table is the documentation: suffix, what it attaches to, what it produces,
 * which stem classes, and whether a y-to-i alternation can be involved.
 */
class SuffixDerivation : public DerivationRule {
private:
    std::string suffix_;
    std::vector<PartOfSpeech> sources_;
    PartOfSpeech derived_;
    std::vector<StemClass> classes_;   // empty means every class
    bool y_restoration_;
    size_t min_stem_;

public:
    /**
     * @param suffix         The surface suffix, without a hyphen
     * @param sources        Parts of speech it attaches to, most central first
     * @param derived        The part of speech it produces
     * @param classes        Stem classes it accepts, empty for all of them
     * @param y_restoration  Whether a Y_FINAL stem can surface with i
     * @param min_stem       Shortest stem in characters
     */
    SuffixDerivation(const std::string& suffix,
                     const std::vector<PartOfSpeech>& sources,
                     PartOfSpeech derived,
                     const std::vector<StemClass>& classes,
                     bool y_restoration,
                     size_t min_stem)
        : suffix_(suffix), sources_(sources), derived_(derived),
          classes_(classes), y_restoration_(y_restoration),
          min_stem_(min_stem) {}

    const std::string& suffix() const override { return suffix_; }
    PartOfSpeech source_pos() const override { return sources_.front(); }

    bool accepts_pos(PartOfSpeech pos) const override {
        return std::find(sources_.begin(), sources_.end(), pos) != sources_.end();
    }

    const std::vector<PartOfSpeech>& source_poses() const override {
        return sources_;
    }

    PartOfSpeech derived_pos() const override { return derived_; }

    bool applies_to(StemClass sc) const override {
        if (classes_.empty()) return true;
        return std::find(classes_.begin(), classes_.end(), sc) != classes_.end();
    }

    bool needs_y_restoration() const override { return y_restoration_; }
    size_t min_stem_length() const override { return min_stem_; }
};

/**
 * english_derivation_rules: The suffixes the analyzer strips
 *
 * The first five are the original inventory and are listed first and
 * unchanged, stem classes included, because they are what the default output
 * was measured on and a reading may not be withdrawn from a word that already
 * had one. The rest were added with the derivation fixes.
 *
 * Three things to read out of the table.
 *
 * **The new rules accept every stem class.** The stem class says how
 * INFLECTION attaches: whether the plural is -s or -es, whether the past
 * doubles a consonant. Derivation attaches to the stem itself, so the class is
 * not the question, and the orthography a derivational suffix does need (the
 * silent e, the consonant y) is readable off the spelling. That is what
 * `orthography::derivational_class` reads, and it is what lets `child|hood`
 * work from an IRR_CHILD lexeme and `nation|al` from one whose class is
 * UNKNOWN, which is 28% of the adjectives in a built lexicon.
 *
 * **Several suffixes attach to more than one part of speech.** -ism and -ist
 * take a noun or an adjective (`capital|ism`, `real|ism`), -ize and -ify take
 * either as well (`modern|ize`, `class|ify`), and -ful takes a verb as readily
 * as a noun: `care|ful` and `thank|ful` are the same suffix, and `thank` is a
 * verb in every lexicon this project builds.
 *
 * **-ion, -ation and -sion are one rule with three spellings, not three
 * rules.** `act|ion` and `creat|ion` need the bare -ion, `inform|ation` and
 * `organiz|ation` need the long form, and `confus|ion` needs -sion. They
 * compete: "action" is "ac" plus -tion and "act" plus -ion, both readings are
 * produced, and weight settles it on the frequency of the two stems, which is
 * how `act|ion` wins. Nothing was removed to make room, so no word that had an
 * analysis lost it.
 */
inline std::vector<std::shared_ptr<DerivationRule>> english_derivation_rules() {
    const PartOfSpeech N   = PartOfSpeech::NOUN;
    const PartOfSpeech V   = PartOfSpeech::VERB;
    const PartOfSpeech ADJ = PartOfSpeech::ADJECTIVE;
    const PartOfSpeech ADV = PartOfSpeech::ADVERB;

    // The classes the original five accept, kept exactly as they were
    const std::vector<StemClass> ORIGINAL = {
        StemClass::REG, StemClass::Y_FINAL, StemClass::SILENT_E,
        StemClass::UNKNOWN};
    const std::vector<StemClass> ORIGINAL_ER = {
        StemClass::REG, StemClass::SILENT_E, StemClass::DOUBLE_CONS,
        StemClass::Y_FINAL, StemClass::UNKNOWN};
    const std::vector<StemClass> ORIGINAL_NO_Y = {
        StemClass::REG, StemClass::SILENT_E, StemClass::UNKNOWN};
    const std::vector<StemClass> ANY = {};

    auto rule = [](const std::string& suffix,
                   const std::vector<PartOfSpeech>& sources,
                   PartOfSpeech derived,
                   const std::vector<StemClass>& classes,
                   bool y_restoration,
                   size_t min_stem) {
        return std::static_pointer_cast<DerivationRule>(
            std::make_shared<SuffixDerivation>(suffix, sources, derived,
                                               classes, y_restoration,
                                               min_stem));
    };

    return {
        // The original five: -ness, -ly, -er, -tion, -ment
        rule("ness",  {ADJ},      N,   ORIGINAL,       true,  1),
        rule("ly",    {ADJ},      ADV, ORIGINAL,       true,  1),
        rule("er",    {V},        N,   ORIGINAL_ER,    true,  1),
        rule("tion",  {V},        N,   ORIGINAL_NO_Y,  false, 1),
        rule("ment",  {V},        N,   ORIGINAL_NO_Y,  false, 1),

        // Adjective-forming
        rule("able",  {V},        ADJ, ANY, true,  3),
        rule("al",    {N},        ADJ, ANY, true,  3),
        rule("ful",   {N, V},     ADJ, ANY, true,  3),
        rule("less",  {N},        ADJ, ANY, true,  3),
        rule("ous",   {N},        ADJ, ANY, true,  3),
        rule("ic",    {N},        ADJ, ANY, true,  3),
        rule("ive",   {V},        ADJ, ANY, true,  3),
        rule("ish",   {N, ADJ},   ADJ, ANY, true,  3),
        rule("y",     {N},        ADJ, ANY, true,  4),

        // Noun-forming
        rule("ity",   {ADJ},      N,   ANY, true,  3),
        rule("ism",   {N, ADJ},   N,   ANY, true,  3),
        rule("ist",   {N, ADJ},   N,   ANY, true,  3),
        rule("ion",   {V},        N,   ANY, true,  3),
        rule("ation", {V},        N,   ANY, true,  3),
        rule("ance",  {V},        N,   ANY, true,  3),
        rule("ence",  {V},        N,   ANY, true,  3),
        rule("hood",  {N, ADJ},   N,   ANY, true,  3),
        rule("ship",  {N},        N,   ANY, true,  3),
        rule("dom",   {N, ADJ},   N,   ANY, true,  3),

        // Verb-forming
        rule("ize",   {ADJ, N},   V,   ANY, true,  3),
        rule("ise",   {ADJ, N},   V,   ANY, true,  4),
        rule("ify",   {ADJ, N},   V,   ANY, true,  3),
        rule("en",    {ADJ},      V,   ANY, true,  3),
    };
}

#endif // DERIVATION_RULE_H
