#ifndef LEXEME_H
#define LEXEME_H

#include <string>
#include <unordered_map>
#include <optional>
#include "stem_class.h"

/**
 * PartOfSpeech: Grammatical category of a lexeme
 * 
 * Distinction from Stem Class:
 *   - POS: What kind of word? (noun, verb, adjective)
 *   - StemClass: How does it inflect? (regular, y-final, irregular)
 * 
 * These are orthogonal dimensions:
 *   - "fly" (noun, y-final): the insect, flies
 *   - "fly" (verb, y-final): the bird flies
 *   
 *   Same stem class, different POS -> different inflectional paradigms
 * 
 * Role in Morphology:
 *   POS determines which inflectional categories are available:
 *     - NOUN: number (singular/plural)
 *     - VERB: tense (present/past), aspect, agreement
 *     - ADJECTIVE: degree (positive/comparative/superlative)
 *     - ADVERB: usually uninflected in English
 * 
 * Design Note:
 *   This is a simplified POS system. Full NLP systems use fine-grained tags:
 *     - Penn Treebank: NN, NNS, NNP, NNPS (noun subtypes)
 *     - Universal Dependencies: NOUN, PROPN, PRON, etc.
 */
enum class PartOfSpeech {
    NOUN,
    VERB,
    ADJECTIVE,
    ADVERB
    // Could extend: PREPOSITION, DETERMINER, PRONOUN, CONJUNCTION, etc.
};

/**
 * pos_to_string: Convert POS to standard abbreviation
 * 
 * Uses common linguistic notation:
 *   N = Noun, V = Verb, ADJ = Adjective, ADV = Adverb
 * 
 * These abbreviations appear in morphological glosses:
 *   "cats" -> cat +N +PL
 *   "walked" -> walk +V +PAST
 * 
 * @param pos  Part of speech to convert
 * @return     Standard abbreviation string
 */
inline std::string pos_to_string(PartOfSpeech pos) {
    switch (pos) {
        case PartOfSpeech::NOUN: return "N";
        case PartOfSpeech::VERB: return "V";
        case PartOfSpeech::ADJECTIVE: return "ADJ";
        case PartOfSpeech::ADVERB: return "ADV";
        default: return "UNKNOWN";
    }
}

/**
 * Lexeme: Represents a single lexical entry (dictionary entry)
 * 
 * What is a Lexeme?
 *   In linguistics, a lexeme is an abstract unit of lexical meaning.
 *   It's distinguished from word forms (surface realizations).
 * 
 *   Example:
 *     Lexeme: WALK
 *     Word forms: walk, walks, walked, walking
 * 
 * Structure:
 *   A lexeme has:
 *     1. Stem: Base form (citation form)
 *     2. POS: Grammatical category
 *     3. Stem Class: Morphological behavior pattern
 *     4. Irregular Forms: Stored exceptions to productive rules
 * 
 * Role in the System:
 *   - Lexemes are stored in the lexicon FSA (trie structure)
 *   - Terminal states in the trie point to Lexeme objects
 *   - During analysis, we lookup stems and retrieve Lexeme information
 *   - The Lexeme tells us which rules to apply (via stem_class)
 * 
 * Regular vs. Irregular:
 *   Regular lexemes: Inflection computed by rules
 *     walk + +PAST -> walked (rule: +PAST -> ed)
 *   
 *   Irregular lexemes: Forms stored in irregular_forms_ map
 *     go + +PAST -> went (stored: {PAST: "went"})
 * 
 * Design Rationale:
 *   - Keeps irregular forms WITH the lexeme (lexical storage)
 *   - Avoids special-case code in morphological rules
 *   - Follows the principle: "Exceptional data belongs in data structures,
 *     not in code"
 * 
 * Example:
 *   Lexeme("go", VERB, IRR_GO, {
 *     {PAST, "went"},
 *     {PAST_PART, "gone"}
 *   })
 * 
 *   This stores the fact that "go" has suppletive past forms,
 *   WITHOUT needing special rules in the morphological processor.
 */
class Lexeme {
public:
    /**
     * Feature: Grammatical features that may have irregular forms
     * 
     * These are the inflectional categories where irregularity occurs:
     *   PLURAL: Irregular plural nouns (mouse->mice)
     *   PAST: Irregular past tense (go->went)
     *   PAST_PART: Irregular past participle (go->gone)
     *   PRES_PART: Irregular present participle (rare in English)
     *   THIRD_SG: Irregular 3rd person singular (be->is, have->has)
     * 
     * Note: Regular forms are NOT stored here; they're computed by rules.
     */
    enum class Feature {
        PLURAL,         // Noun plural: mouse->mice
        PAST,           // Verb past tense: go->went
        PAST_PART,      // Verb past participle: go->gone
        PRES_PART,      // Verb present participle: (rare irregulars)
        THIRD_SG,       // Verb 3sg present: be->is, have->has
        COMP,           // Comparative: bigger, happier
        SUP             // Superlative: biggest, happiest
    };
    
private:
    std::string stem_;                      // Base form: "walk", "mouse", "go"
    PartOfSpeech pos_;                      // Grammatical category
    StemClass stem_class_;                  // Morphological behavior class
    float weight_ = 1.0f;                   // Ambiguity ranking weight

    // Map: Feature -> Irregular surface form
    std::unordered_map<Feature, std::string> irregular_forms_;

public:
    /**
     * Constructor
     * 
     * BUG FIX: Parameter name was stem_class_ (shadowing member variable)
     * Fixed: Use stem_class as parameter, initialize stem_class_ member
     * 
     * @param stem        The base form (e.g., "walk", "cat")
     * @param pos         Part of speech category
     * @param stem_class  Morphological behavior class
     */
    Lexeme(const std::string& stem, PartOfSpeech pos, StemClass stem_class)
        : stem_(stem), pos_(pos), stem_class_(stem_class) {}
    
    // ========== Accessors ==========

    const std::string& stem() const { return stem_; }
    PartOfSpeech pos() const { return pos_; }
    StemClass stem_class() const { return stem_class_; }
    float weight() const { return weight_; }
    void set_weight(float w) { weight_ = w; }

    // Expose irregular forms map for building reverse index
    const std::unordered_map<Feature, std::string>& irregular_forms_map() const {
        return irregular_forms_;
    }
    
    // ========== Irregular Form Management ==========
    
    /**
     * add_irregular_form: Store an exceptional inflected form
     * 
     * Call this when a lexeme doesn't follow productive rules.
     * 
     * Example:
     *   auto mouse = Lexeme("mouse", NOUN, IRR_MOUSE);
     *   mouse.add_irregular_form(Feature::PLURAL, "mice");
     * 
     * Now when analyzing "mice", we can retrieve "mouse +PL"
     * without needing a rule for umlaut plurals.
     * 
     * @param feature  Which inflectional category (PLURAL, PAST, etc.)
     * @param form     The surface realization
     */
    void add_irregular_form(Feature feature, const std::string& form) {
        irregular_forms_[feature] = form;
    }
    
    /**
     * has_irregular_form: Check if a specific feature is irregular
     * 
     * @param feature  The feature to check
     * @return         true if form is stored, false if regular
     */
    bool has_irregular_form(Feature feature) const {
        return irregular_forms_.find(feature) != irregular_forms_.end();
    }
    
    /**
     * get_irregular_form: Retrieve the stored irregular form
     * 
     * Returns std::optional to handle the case where form doesn't exist.
     * 
     * Usage:
     *   if (auto form = lexeme.get_irregular_form(Feature::PAST)) {
     *     // Use form.value()
     *   } else {
     *     // Apply regular rule
     *   }
     * 
     * @param feature  The feature to retrieve
     * @return         Optional containing form if irregular, nullopt if regular
     */
    std::optional<std::string> get_irregular_form(Feature feature) const {
        auto it = irregular_forms_.find(feature);
        if (it != irregular_forms_.end()) {
            return it->second;
        }
        return std::nullopt;
    }
    
    // ========== Query Methods ==========
    
    /**
     * is_regular: Check if this lexeme follows productive rules
     * 
     * A lexeme is regular if:
     *   1. Its stem class is not IRR_* (checked by ::is_regular(stem_class_))
     *   2. AND it has no stored irregular forms
     * 
     * Examples:
     *   "cat" (REG, no irregular forms) -> regular
     *   "city" (Y_FINAL, no irregular forms) -> regular (orthographic variant)
     *   "mouse" (IRR_MOUSE, has {PLURAL: "mice"}) -> irregular
     *   "go" (IRR_GO, has {PAST: "went"}) -> irregular
     * 
     * @return true if fully regular, false if any irregularity
     */
    bool is_regular() const {
        return ::is_regular(stem_class_) && irregular_forms_.empty();
    }
    
    /**
     * is_irregular: Check if this lexeme has any irregularity
     * 
     * Opposite of is_regular().
     * 
     * @return true if irregular, false if regular
     */
    bool is_irregular() const {
        return !is_regular();
    }
    
    /**
     * to_string: Format lexeme for display
     * 
     * BUG FIXES:
     *   1. Missing () after const
     *   2. Wrong function name: stem_class_to_string_() should be stem_class_to_string()
     *   3. Extra space in format
     * 
     * Format: "stem +POS [STEM_CLASS]"
     * Example: "cat +N [REG]"
     *          "city +N [Y_FINAL]"
     * 
     * @return Human-readable representation
     */
    std::string to_string() const {
        return stem_ + " +" + pos_to_string(pos_) + " [" + 
               stem_class_to_string(stem_class_) + "]";
    }
};

/**
 * feature_to_tag: Convert Feature enum to morphological tag
 * 
 * Used in morphological glosses:
 *   mouse +N +PL -> "PL" comes from feature_to_tag(Feature::PLURAL)
 * 
 * Standard morphological notation:
 *   +PL = plural
 *   +PAST = past tense
 *   +3SG = 3rd person singular
 * 
 * @param f  Feature to convert
 * @return   Standard morphological tag string
 */
inline std::string feature_to_tag(Lexeme::Feature f) {
    switch (f) {
        case Lexeme::Feature::PLURAL:    return "+PL";
        case Lexeme::Feature::PAST:      return "+PAST";
        case Lexeme::Feature::PAST_PART: return "+PAST_PART";
        case Lexeme::Feature::PRES_PART: return "+PRES_PART";
        case Lexeme::Feature::THIRD_SG:  return "+3SG";
        case Lexeme::Feature::COMP:      return "+COMP";
        case Lexeme::Feature::SUP:       return "+SUP";
        default: return "+UNKNOWN";
    }
}

#endif // LEXEME_H