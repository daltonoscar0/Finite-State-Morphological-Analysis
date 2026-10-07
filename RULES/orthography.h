#ifndef ORTHOGRAPHY_H
#define ORTHOGRAPHY_H

#include "../LEXICON/lexeme.h"
#include "../LEXICON/stem_class.h"

#include <string>

/**
 * orthography.h: Spelling-derived orthographic class for irregular lexemes
 *
 * StemClass carries two different kinds of information in one field. For a
 * regular lexeme it says how suffixes attach: Y_FINAL, SILENT_E, SIBILANT,
 * DOUBLE_CONS. For an irregular one it says which forms are stored instead:
 * IRR_GO, IRR_SING, IRR_BE. A verb can need both at once. "choose" stores
 * "chose" and "chosen", and it also ends in a silent e, so its present
 * participle is "choosing"; "go" stores "went" and also takes -es, so its
 * third person singular is "goes". With one field, the irregularity wins and
 * the orthography is lost, which is why the analyzer used to reject "chooses",
 * "choosing", "goes" and "breaking".
 *
 * Rather than widen the data model and the TSV format with a second class
 * column, the orthography is read back off the spelling, which is where it was
 * visible all along. This applies only to lexemes whose stem class is one of
 * the IRR_* values: a regular lexeme already states its class, and that stated
 * class is always used unchanged.
 *
 * Consonant doubling is deliberately not inferred. Whether a stem doubles its
 * final consonant depends on stress ("begin" gives "beginning", "listen" does
 * not give "listenning"), which the spelling does not show, so an irregular
 * stem that might double is left as REG here and the callers that care take
 * the ambiguity into account.
 */
namespace orthography {

/** is_vowel: One of the five written vowels. y is handled separately. */
inline bool is_vowel(char c) {
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

/**
 * takes_es: Does stem + -s surface as "es"?
 *
 * The sibilant graphemes need an epenthetic vowel (kiss, box, buzz, church,
 * dish), and so do verb stems in -o: go gives goes, do gives does.
 */
inline bool takes_es(const std::string& stem) {
    if (stem.empty()) return false;
    const char last = stem.back();
    if (last == 's' || last == 'z' || last == 'x' || last == 'o') return true;
    if (stem.size() >= 2) {
        const std::string tail = stem.substr(stem.size() - 2);
        if (tail == "ch" || tail == "sh") return true;
    }
    return false;
}

/**
 * has_silent_e: Does the stem end in a silent e that a vowel-initial suffix
 * deletes?
 *
 * The e has to follow a consonant: "choose" gives "choosing", but "see" gives
 * "seeing" and "free" gives "freeing", where the e is part of a vowel digraph.
 * Two-letter stems are excluded for the same reason, so "be" gives "being".
 */
inline bool has_silent_e(const std::string& stem) {
    if (stem.size() < 3) return false;
    if (stem.back() != 'e') return false;
    return !is_vowel(stem[stem.size() - 2]);
}

/**
 * has_consonant_y: Does the stem end in consonant + y, where y becomes i?
 *
 * "carry" gives "carried", but "play" gives "played" and "buy" gives "buys",
 * because there the y follows a vowel.
 */
inline bool has_consonant_y(const std::string& stem) {
    if (stem.size() < 2) return false;
    if (stem.back() != 'y') return false;
    return !is_vowel(stem[stem.size() - 2]);
}

/**
 * may_double_final_consonant: Could a vowel-initial suffix double the final
 * consonant?
 *
 * True for a stem ending in single consonant, single vowel, consonant, which
 * is the shape where doubling is possible: put, get, win, begin, forget. It is
 * only a possibility, not a prediction, because stress decides: "begin" gives
 * "beginning" and "open" gives "opening". Callers use it to tell "the
 * orthography is not determined here" from "the orthography is plain".
 */
inline bool may_double_final_consonant(const std::string& stem) {
    if (stem.size() < 3) return false;
    const char c1 = stem[stem.size() - 1];  // final consonant
    const char c2 = stem[stem.size() - 2];  // the vowel
    const char c3 = stem[stem.size() - 3];  // the consonant before it
    if (is_vowel(c1) || c1 == 'y' || c1 == 'w' || c1 == 'x') return false;
    if (!is_vowel(c2)) return false;
    if (is_vowel(c3)) return false;
    return true;
}

/**
 * implied_class: The orthographic class a spelling implies
 *
 * Used only where the stated stem class is irregular and therefore says
 * nothing about suffixation.
 */
inline StemClass implied_class(const std::string& stem) {
    if (takes_es(stem))       return StemClass::SIBILANT;
    if (has_silent_e(stem))   return StemClass::SILENT_E;
    if (has_consonant_y(stem)) return StemClass::Y_FINAL;
    return StemClass::REG;
}

/**
 * effective_class: How suffixes attach to this lexeme's stem
 *
 * A regular lexeme states its own class and keeps it. An irregular one has
 * spent the field on its irregularity, so the class is read off the spelling.
 * Every analysis path that asks "does this class take this suffix" uses this
 * rather than Lexeme::stem_class(), which leaves regular lexemes behaving
 * exactly as before and lets irregular ones inflect regularly where English
 * does.
 */
inline StemClass effective_class(const Lexeme& lex) {
    const StemClass stated = lex.stem_class();
    if (::is_regular(stated)) return stated;
    return implied_class(lex.stem());
}

/**
 * tag_to_feature: The Lexeme::Feature a morphological tag names
 *
 * Needed so that a productive rule can ask whether the lexeme already stores
 * this feature, which blocks the rule: "break" stores "broke" for +PAST, so
 * "breaked" must not be analyzed, while it stores nothing for +3SG, so
 * "breaks" must be.
 *
 * @param tag      A feature tag such as "+PL" or "+PAST"
 * @param out      Set to the matching feature when the tag is known
 * @return         true when the tag names a storable feature
 */
inline bool tag_to_feature(const std::string& tag, Lexeme::Feature& out) {
    if (tag == "+PL")        { out = Lexeme::Feature::PLURAL;    return true; }
    if (tag == "+PAST")      { out = Lexeme::Feature::PAST;      return true; }
    if (tag == "+PAST_PART") { out = Lexeme::Feature::PAST_PART; return true; }
    if (tag == "+PRES_PART") { out = Lexeme::Feature::PRES_PART; return true; }
    if (tag == "+3SG")       { out = Lexeme::Feature::THIRD_SG;  return true; }
    if (tag == "+COMP")      { out = Lexeme::Feature::COMP;      return true; }
    if (tag == "+SUP")       { out = Lexeme::Feature::SUP;       return true; }
    return false;
}

/**
 * stores_feature: Does the lexeme hold an irregular form for this tag?
 *
 * @param lex  The lexeme a productive rule is about to apply to
 * @param tag  The feature tag the rule would produce
 */
inline bool stores_feature(const Lexeme& lex, const std::string& tag) {
    Lexeme::Feature feat;
    if (!tag_to_feature(tag, feat)) return false;
    return lex.has_irregular_form(feat);
}

}  // namespace orthography

#endif // ORTHOGRAPHY_H
