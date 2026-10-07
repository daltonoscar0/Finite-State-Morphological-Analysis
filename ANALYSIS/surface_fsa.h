#ifndef SURFACE_FSA_H
#define SURFACE_FSA_H

#include "../AUTOMATA/fsa.h"
#include <vector>
#include <memory>

/**
 * SurfaceFSA: Linear FSA that accepts exactly one string
 * 
 * Purpose:
 *   Represents the input word as a deterministic finite-state automaton.
 *   This FSA forms one component in the tri-partite composition:
 *     Surface FSA ⊗ Rule FSTs ⊗ Lexicon FSA -> Analysis
 * 
 * Structure:
 *   For input "cats", the SurfaceFSA looks like:
 *   
 *     (q0) --c--> (q1) --a--> (q2) --t--> (q3) --s--> (q4:final)
 *   
 *   - One linear path from start to final state
 *   - Each transition consumes exactly one symbol
 *   - Only the final state is accepting
 * 
 * Design Rationale:
 *   While this is a degenerate FSA (single path, deterministic), maintaining
 *   the FSA abstraction provides several benefits:
 *   
 *   1. Uniform interface: Can be composed with other FSAs/FSTs using standard operations
 *   2. Type safety: Clearly distinguished from raw strings in the type system
 *   3. Extensibility: Easy to generalize to multiple paths (for spelling variants, etc.)
 *   4. Theoretical clarity: Makes the FSA composition explicit in code
 * 
 * Relationship to Theory:
 *   In formal language theory, any finite language (including single-word languages)
 *   is regular and can be represented by a DFA. This class implements that DFA.
 *   
 *   L(SurfaceFSA("cat")) = {"cat"}  (language containing exactly one string)
 */
class SurfaceFSA : public FSA {
public:
    /**
     * Constructor
     * 
     * @param symbols  Pointer to global SymbolTable (required for FSA base class)
     */
    SurfaceFSA(SymbolTable* symbols) : FSA(symbols) {}
    
    /**
     * build_from_symbols: Construct linear FSA from symbol sequence
     * 
     * Algorithm:
     *   Given symbols [s₀, s₁, ..., sₙ₋₁]:
     *   1. Create n+1 states: q₀ (start), q₁, ..., qₙ (final)
     *   2. Create n transitions: qᵢ --sᵢ--> qᵢ₊₁ for i ∈ [0, n-1]
     *   3. Mark qₙ as the only final state
     * 
     * Edge Case:
     *   If symbols_seq is empty, create a single state that is both start and final.
     *   This represents the language L = {ε} (containing only the empty string).
     * 
     * Time Complexity: O(n) where n = |symbols_seq|
     * Space Complexity: O(n) for states and transitions
     * 
     * @param symbols_seq  Sequence of Symbol objects representing the input word
     * 
     * Example:
     *   symbols_seq = [Symbol('c'), Symbol('a'), Symbol('t')]
     *   Creates: (q0)-c->(q1)-a->(q2)-t->(q3:final)
     */
    void build_from_symbols(const std::vector<std::shared_ptr<Symbol>>& symbols_seq) {
        if (symbols_seq.empty()) {
            // Edge case: empty input
            // Create single state that accepts ε (empty string)
            auto s0 = create_state(true);  // Both start and final
            set_start_state(s0);
            return;
        }
        
        // Create state chain: need |symbols| + 1 states
        std::vector<State*> states;
        for (size_t i = 0; i <= symbols_seq.size(); i++) {
            bool is_final = (i == symbols_seq.size());  // Only last state is final
            states.push_back(create_state(is_final));
        }
        
        set_start_state(states[0]);
        
        // Create transitions linking consecutive states
        // Transition i connects states[i] -> states[i+1] via symbols_seq[i]
        for (size_t i = 0; i < symbols_seq.size(); i++) {
            create_transition(states[i], states[i + 1], symbols_seq[i]);
        }
    }
    
    /**
     * build_from_string: Convenience method to build FSA from raw string
     * 
     * This bypasses the Tokenizer and directly converts each character to a Symbol.
     * Useful for testing and debugging.
     * 
     * Process:
     *   1. Convert each character to a SURFACE Symbol
     *   2. Call build_from_symbols with the Symbol sequence
     * 
     * @param str  Input string (e.g., "cat")
     * 
     * Note: Assumes all characters in str are valid (lowercase letters).
     *       No validation is performed - caller must ensure valid input.
     */
    void build_from_string(const std::string& str) {
        std::vector<std::shared_ptr<Symbol>> syms;
        
        for (char c : str) {
            // Get or create SURFACE symbol for this character
            syms.push_back(symbols()->get_or_create(std::string(1, c), SymbolType::SURFACE));
        }
        
        build_from_symbols(syms);
    }
};

#endif // SURFACE_FSA_H