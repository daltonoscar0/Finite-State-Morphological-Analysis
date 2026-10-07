#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "../SYMBOLS/symbol.h"
#include <vector>
#include <string>
#include <memory>
#include <cctype>

/**
 * Tokenizer: Converts raw input strings to symbol sequences
 * 
 * Purpose:
 *   This is a simplified tokenizer for the morphological analyzer.
 *   It performs character-to-symbol conversion for single words.
 *   
 * Relationship to Phase 1 Tokenizer:
 *   - Phase 1 (previous project): Character-level FSM that identifies token boundaries
 *     in raw text (handles contractions, abbreviations, hyphenation, etc.)
 *     Repository: https://github.com/daltonoscar0/Rule-Based-Tokenizer-and-Sentence-Segmenter-with-C-core
 *   
 *   - Phase 2 (this tokenizer): Word-level converter that transforms individual word
 *     tokens into sequences of abstract Symbol objects for FSA processing
 * 
 * Design Rationale:
 *   We maintain a clean separation between string processing (this layer) and 
 *   finite-state traversal (analyzer layer). This tokenizer sits at the system boundary,
 *   handling encoding issues and normalization before symbols enter the FSA machinery.
 * 
 * Integration Example:
 *   Phase 1 output: [cities] WORD, [are] WORD, [amazing] WORD
 *   Phase 2 takes: "cities" -> [c, i, t, i, e, s] as Symbol objects
 *   Morphology produces: city +N +PL
 */
class Tokenizer {
private:
    SymbolTable* symbols_;      // Non-owning pointer to central symbol registry
    bool normalize_case_;       // Whether to convert input to lowercase
    
public:
    /**
     * Constructor
     * 
     * @param symbols      Pointer to the global SymbolTable (not owned by this object)
     * @param normalize_case  If true, converts all input to lowercase (default: true)
     * 
     * Note: The SymbolTable must outlive this Tokenizer instance.
     */
    Tokenizer(SymbolTable* symbols, bool normalize_case = true)
        : symbols_(symbols), normalize_case_(normalize_case) {}
    
    /**
     * Tokenize: Convert a string to a sequence of Symbol objects
     * 
     * Algorithm:
     *   1. For each character in input:
     *      a. Optionally normalize to lowercase
     *      b. Validate character is in [a-z] range
     *      c. Look up corresponding Symbol in SymbolTable
     *   2. Return sequence of Symbols (or empty vector if invalid input)
     * 
     * Assumptions:
     *   - Input is a single word (no spaces)
     *   - Only lowercase ASCII letters are valid
     *   - Invalid characters cause the entire tokenization to fail
     * 
     * @param input  A single word string (e.g., "cities")
     * @return       Vector of Symbol pointers, or empty vector if input contains invalid chars
     * 
     * Example:
     *   tokenize("cat") -> [Symbol('c'), Symbol('a'), Symbol('t')]
     *   tokenize("cat!") -> [] (invalid character '!')
     */
    std::vector<std::shared_ptr<Symbol>> tokenize(const std::string& input) const {
        std::vector<std::shared_ptr<Symbol>> result;
        
        for (char c : input) {
            // Normalize to lowercase if configured
            char ch = normalize_case_ ? std::tolower(c) : c;
            
            // Validate: only lowercase letters allowed in current implementation
            // This restriction simplifies the lexicon and FSA machinery
            if (ch < 'a' || ch > 'z') {
                return {};  // Invalid input - abort and return empty
            }
            
            // Get or create a SURFACE symbol for this character
            // The SymbolTable ensures each character maps to a unique Symbol object
            auto symbol = symbols_->get_or_create(
                std::string(1, ch),      // Convert char to string
                SymbolType::SURFACE      // Mark as surface (orthographic) symbol
            );
            result.push_back(symbol);
        }
        
        return result;
    }
    
    /**
     * Validate: Check if a string contains only valid characters
     * 
     * This is useful for pre-checking input without actually tokenizing.
     * 
     * @param input  String to validate
     * @return       true if all characters are in [a-z] (after normalization), false otherwise
     */
    bool is_valid(const std::string& input) const {
        for (char c : input) {
            char ch = normalize_case_ ? std::tolower(c) : c;
            if (ch < 'a' || ch > 'z') {
                return false;
            }
        }
        return true;
    }
};

#endif // TOKENIZER_H