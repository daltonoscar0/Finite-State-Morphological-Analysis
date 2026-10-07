#ifndef DERIVATION_RULE_H
#define DERIVATION_RULE_H

#include "../LEXICON/stem_class.h"
#include "../LEXICON/lexeme.h"
#include <string>
#include <vector>

/**
 * DerivationRule: Encodes a derivational morphological process.
 * Unlike inflectional MorphRule, derivational rules change the POS.
 */
class DerivationRule {
public:
    virtual ~DerivationRule() = default;
    virtual const std::string& suffix() const = 0;
    virtual PartOfSpeech source_pos() const = 0;
    virtual PartOfSpeech derived_pos() const = 0;
    virtual bool applies_to(StemClass sc) const = 0;
    virtual bool needs_y_restoration() const { return false; }
};

// -ness: ADJ -> NOUN (happiness, darkness, weakness)
class NessRule : public DerivationRule {
    std::string suffix_{"ness"};
public:
    const std::string& suffix() const override { return suffix_; }
    PartOfSpeech source_pos() const override { return PartOfSpeech::ADJECTIVE; }
    PartOfSpeech derived_pos() const override { return PartOfSpeech::NOUN; }
    bool applies_to(StemClass sc) const override {
        return sc == StemClass::REG || sc == StemClass::Y_FINAL || sc == StemClass::SILENT_E;
    }
    bool needs_y_restoration() const override { return true; }
};

// -ly: ADJ -> ADV (quickly, slowly, happily)
class LyRule : public DerivationRule {
    std::string suffix_{"ly"};
public:
    const std::string& suffix() const override { return suffix_; }
    PartOfSpeech source_pos() const override { return PartOfSpeech::ADJECTIVE; }
    PartOfSpeech derived_pos() const override { return PartOfSpeech::ADVERB; }
    bool applies_to(StemClass sc) const override {
        return sc == StemClass::REG || sc == StemClass::Y_FINAL || sc == StemClass::SILENT_E;
    }
    bool needs_y_restoration() const override { return true; }
};

// -er (agentive): VERB -> NOUN (teacher, runner, writer)
class AgentiveRule : public DerivationRule {
    std::string suffix_{"er"};
public:
    const std::string& suffix() const override { return suffix_; }
    PartOfSpeech source_pos() const override { return PartOfSpeech::VERB; }
    PartOfSpeech derived_pos() const override { return PartOfSpeech::NOUN; }
    bool applies_to(StemClass sc) const override {
        return sc == StemClass::REG || sc == StemClass::SILENT_E ||
               sc == StemClass::DOUBLE_CONS || sc == StemClass::Y_FINAL;
    }
    bool needs_y_restoration() const override { return true; }
};

// -tion/-ion: VERB -> NOUN (action, creation)
class TionRule : public DerivationRule {
    std::string suffix_{"tion"};
public:
    const std::string& suffix() const override { return suffix_; }
    PartOfSpeech source_pos() const override { return PartOfSpeech::VERB; }
    PartOfSpeech derived_pos() const override { return PartOfSpeech::NOUN; }
    bool applies_to(StemClass sc) const override {
        return sc == StemClass::REG || sc == StemClass::SILENT_E;
    }
};

// -ment: VERB -> NOUN (movement, management, development)
class MentRule : public DerivationRule {
    std::string suffix_{"ment"};
public:
    const std::string& suffix() const override { return suffix_; }
    PartOfSpeech source_pos() const override { return PartOfSpeech::VERB; }
    PartOfSpeech derived_pos() const override { return PartOfSpeech::NOUN; }
    bool applies_to(StemClass sc) const override {
        return sc == StemClass::REG || sc == StemClass::SILENT_E;
    }
};

#endif // DERIVATION_RULE_H
