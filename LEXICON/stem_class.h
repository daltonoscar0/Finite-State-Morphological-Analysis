#ifndef STEM_CLASS_H
#define STEM_CLASS_H

#include <string>

/**
 * StemClass: Defines morphological behavior categories for lexemes
 * 
 * Conceptual Role:
 *   Stem classes capture morphophonological patterns - how stems behave
 *   under inflection. This is distinct from part-of-speech.
 * 
 * Why Stem Classes Matter:
 *   Different stems undergo different morphological processes:
 *     - "cat" + plural -> "cats" (regular suffixation)
 *     - "city" + plural -> "cities" (y->i alternation)
 *     - "mouse" + plural -> "mice" (irregular umlaut)
 * 
 *   Without stem classes, we'd need:
 *     1. Separate rules for each word (doesn't scale)
 *     2. Or complex conditional logic in rules (not finite-state)
 * 
 *   With stem classes, we capture generalizations:
 *     - Y_FINAL class -> apply y->i rule before +PL
 *     - IRR_MOUSE class -> use lexically stored form
 * 
 * Design Rationale:
 *   - Stem classes are NOT part-of-speech (though often correlated)
 *     * "fly" is Y_FINAL whether noun or verb
 *     * "walk" is REG whether noun or verb
 *   
 *   - They encode phonological/orthographic properties
 *     * Y_FINAL: phonotactic constraint on /i/ -> /aj/ before plural
 *     * SIBILANT: phonological requirement for epenthetic vowel
 *   
 *   - They enable rule selection and blocking
 *     * REG class -> apply regular rules
 *     * IRR_* classes -> block regular rules, use lexicon
 * 
 * Linguistic Background:
 *   This approach follows the "lexical phonology" framework:
 *     - Lexemes are marked with diacritic features
 *     - Rules apply or block based on these features
 *     - Captures generalizations without overgenerating
 * 
 * Examples for English:
 *   REG         - Regular stems (cat, dog, walk)
 *   Y_FINAL     - Stems ending in 'y' (city, fly, carry)
 *   SIBILANT    - Stems ending in sibilants (kiss, buzz, church)
 *   DOUBLE_CONS - Stems requiring consonant doubling (stop, hop)
 *   IRR_*       - Irregular forms (go, mouse, child)
 */
enum class StemClass {
    // ========== Regular Stems ==========
    // Follow standard inflectional rules without exceptions
    REG,
    
    // ========== Orthographic Classes ==========
    // Require special orthographic adjustments
    
    Y_FINAL,        // Stems ending in consonant+y
                    // Examples: city->cities, fly->flies, carry->carries
                    // Rule: y->i before suffixes beginning with vowel
    
    SIBILANT,       // Stems ending in sibilants: /s/, /z/, /ʃ/, /ʒ/, /tʃ/, /dʒ/
                    // Examples: kiss->kisses, buzz->buzzes, church->churches
                    // Rule: Require epenthetic -e- before -s (phonotactic constraint)
    
    DOUBLE_CONS,    // Stems with short vowel + single consonant
                    // Examples: stop->stopped, hop->hopping
                    // Rule: Double final consonant before vowel-initial suffix
                    // (Prevents syllable structure change)
    
    SILENT_E,       // Stems ending in silent 'e'
                    // Examples: hope->hoping, make->making
                    // Rule: Delete 'e' before vowel-initial suffix
    
    // ========== Irregular Noun Classes (English) ==========
    // Suppletive or ablaut plurals that don't follow productive rules
    
    IRR_MOUSE,      // Umlaut plurals (vowel change)
                    // Examples: mouse->mice, louse->lice
                    // Historical: Germanic i-mutation (fronting)
    
    IRR_CHILD,      // -ren plurals (archaic)
                    // Examples: child->children, ox->oxen (overlaps with IRR_OX)
                    // Historical: Old English weak declension
    
    IRR_SHEEP,      // Zero-marked plurals (no overt suffix)
                    // Examples: sheep->sheep, deer->deer, fish->fish
                    // Often: mass nouns or animals
    
    IRR_OX,         // -en plurals
                    // Examples: ox->oxen, brother->brethren (archaic)
    
    // ========== Irregular Verb Classes (English) ==========
    // Suppletive or ablaut past tenses
    
    IRR_GO,         // Fully suppletive (unrelated stems)
                    // go->went (historical: 'wend' supplanted 'go' in past)
    
    IRR_BE,         // Multiple suppletive forms
                    // be->am/is/are/was/were
                    // Most irregular verb in English
    
    IRR_HAVE,       // Minor irregularities
                    // have->has (not *haves), had (not *haved)
    
    IRR_DO,         // Vowel change + consonant alternation
                    // do->does /dʌ/->/dʌz/ (not *dos)
    
    IRR_SING,       // Ablaut class: /ɪ/->/æ/->/ʌ/
                    // sing->sang->sung
                    // Also: ring, spring, drink
    
    IRR_RING,       // Ablaut class: /ɪ/->/æ/->/ʌ/ (same as SING)
                    // Separated for potential future distinction
    
    IRR_SWIM,       // Ablaut class: /ɪ/->/æ/->/ʌ/
                    // swim->swam->swum
    
    // ========== Extensibility ==========
    UNKNOWN         // Default for unclassified stems
};

/**
 * stem_class_to_string: Convert stem class to human-readable label
 * 
 * Used for debugging, logging, and error messages.
 * 
 * @param sc  The StemClass to convert
 * @return    String representation (e.g., "Y_FINAL", "IRR_MOUSE")
 */
inline std::string stem_class_to_string(StemClass sc) {
    switch (sc) {
        case StemClass::REG: return "REG";
        case StemClass::Y_FINAL: return "Y_FINAL";
        case StemClass::SIBILANT: return "SIBILANT";
        case StemClass::DOUBLE_CONS: return "DOUBLE_CONS";
        case StemClass::SILENT_E: return "SILENT_E";
        case StemClass::IRR_MOUSE: return "IRR_MOUSE";
        case StemClass::IRR_CHILD: return "IRR_CHILD";
        case StemClass::IRR_SHEEP: return "IRR_SHEEP";
        case StemClass::IRR_OX: return "IRR_OX";
        case StemClass::IRR_GO: return "IRR_GO";
        case StemClass::IRR_BE: return "IRR_BE";
        case StemClass::IRR_HAVE: return "IRR_HAVE";
        case StemClass::IRR_DO: return "IRR_DO";
        case StemClass::IRR_SING: return "IRR_SING";
        case StemClass::IRR_RING: return "IRR_RING";
        case StemClass::IRR_SWIM: return "IRR_SWIM";
        case StemClass::UNKNOWN: return "UNKNOWN";
        default: return "UNDEFINED";
    }
}

/**
 * Query Functions: Classify stem classes by type
 * 
 * These predicates enable rule application logic:
 *   if (is_irregular(stem_class)) {
 *     use_lexical_form();
 *   } else {
 *     apply_productive_rule();
 *   }
 */

/**
 * is_irregular: Check if stem has irregular inflection
 * 
 * Irregular stems don't follow productive rules. Instead:
 *   - They have forms stored in the lexicon
 *   - Regular rules are blocked for these stems
 * 
 * Implementation:
 *   All IRR_* classes are between IRR_MOUSE and IRR_SWIM in the enum.
 *   We check if sc falls in this range.
 * 
 * BUG FIX: Original code had `&&` instead of `<=`
 *   Wrong: sc >= IRR_MOUSE && IRR_SWIM
 *   Fixed: sc >= IRR_MOUSE && sc <= IRR_SWIM
 * 
 * @param sc  Stem class to check
 * @return    true if irregular, false if regular or orthographic
 */
inline bool is_irregular(StemClass sc) {
    return sc >= StemClass::IRR_MOUSE && sc <= StemClass::IRR_SWIM;
}

/**
 * is_regular: Check if stem follows regular inflection
 * 
 * Regular stems include:
 *   - REG: truly regular (cat, dog)
 *   - Y_FINAL, SIBILANT, etc.: orthographic variants of regular
 * 
 * They all use productive morphological rules (not lexical storage).
 * 
 * @param sc  Stem class to check
 * @return    true if regular/orthographic, false if irregular
 */
inline bool is_regular(StemClass sc) {
    return !is_irregular(sc);
}

/**
 * requires_special_orthography: Check if stem needs orthographic adjustments
 * 
 * Some regular stems require orthographic rules before affixation:
 *   - Y_FINAL: y->i
 *   - SIBILANT: insert epenthetic 'e'
 *   - DOUBLE_CONS: double final consonant
 *   - SILENT_E: delete final 'e'
 * 
 * Used to determine which orthographic FST to apply.
 * 
 * @param sc  Stem class to check
 * @return    true if orthographic adjustments needed
 */
inline bool requires_special_orthography(StemClass sc) {
    return sc == StemClass::Y_FINAL ||
           sc == StemClass::SIBILANT ||
           sc == StemClass::DOUBLE_CONS ||
           sc == StemClass::SILENT_E;
}

#endif // STEM_CLASS_H