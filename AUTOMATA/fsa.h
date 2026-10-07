#ifndef FSA_H
#define FSA_H

#include "automaton.h"
#include <unordered_set>

/**
 * FSA: Finite-State Acceptor (Deterministic)
 * 
 * Purpose:
 *   A deterministic finite automaton that accepts or rejects input strings.
 *   Used for pattern matching and language recognition.
 * 
 * Formal Definition:
 *   An FSA is a 5-tuple (Q, Σ, δ, q₀, F) where:
 *     δ: Q × Σ -> Q  (deterministic transition function)
 *   
 *   Contrast with NFA:
 *     δ: Q × (Σ ∪ {ε}) -> P(Q)  (nondeterministic, returns set of states)
 * 
 * Properties:
 *   1. Deterministic: At most one transition per (state, symbol) pair
 *   2. No epsilon transitions (or they're handled transparently)
 *   3. Single accepting path for any input
 * 
 * Language:
 *   L(A) = {w ∈ Σ* | δ*(q₀, w) ∈ F}
 *   
 *   Where δ* is the extended transition function:
 *     δ*(q, ε) = q
 *     δ*(q, wa) = δ(δ*(q, w), a)
 * 
 * Use Cases in This System:
 *   1. Lexicon Trie: FSA where paths spell out valid stems
 *   2. Surface FSA: Linear FSA accepting exactly the input word
 *   3. Pattern Matching: Checking if string matches a regular pattern
 * 
 * Complexity:
 *   - Acceptance check: O(n) where n = input length (linear scan)
 *   - Space: O(|Q| + |δ|) where |Q| = states, |δ| = transitions
 * 
 * Comparison with FST:
 *   - FSA: Recognition only (yes/no answer)
 *   - FST: Transduction (maps input -> output)
 */
class FSA : public Automaton {
public:
    /**
     * Constructor
     * 
     * @param symbol_table  Pointer to global SymbolTable
     * 
     * Inherits all Automaton infrastructure and specializes to deterministic acceptance.
     */
    FSA(SymbolTable* symbol_table) : Automaton(symbol_table) {}
    
    /**
     * accepts: Check if this FSA accepts the input string
     * 
     * Algorithm (deterministic traversal):
     *   1. Start at q₀ (start state)
     *   2. For each input symbol sᵢ:
     *      a. Find transition where input matches sᵢ
     *      b. If no transition exists -> reject immediately
     *      c. Otherwise, move to target state
     *   3. After consuming all input:
     *      - Accept if current state ∈ F (is final)
     *      - Reject otherwise
     * 
     * Determinism:
     *   We assume at most one matching transition per (state, symbol).
     *   If multiple match (shouldn't happen in DFA), we take the first.
     * 
     * Time Complexity: O(n × t) where:
     *   - n = input length
     *   - t = average transitions per state (usually small, O(|Σ|))
     *   
     *   For practical purposes: O(n) since t is bounded by alphabet size
     * 
     * @param input  Vector of Symbol objects to process
     * @return       true if input ∈ L(this FSA), false otherwise
     * 
     * Example:
     *   FSA for L = {w | w contains "cat"}
     *     accepts([c,a,t]) -> true
     *     accepts([d,o,g]) -> false
     */
    bool accepts(const std::vector<std::shared_ptr<Symbol>>& input) const override {
        // Structural validity check
        if (!is_valid()) return false;
        
        State* current = start_state_;
        
        // Process each symbol in sequence
        for (const auto& symbol : input) {
            State* next = nullptr;
            
            // Find the unique matching transition (deterministic)
            for (Transition* trans : current->outgoing()) {
                if (trans->matches(symbol)) {
                    next = trans->target();
                    break;  // Found unique transition, stop searching
                }
            }
            
            if (next == nullptr) {
                // No valid transition -> string not in language
                return false;
            }
            
            current = next;
        }
        
        // Accept if we end in a final state
        return current->is_final();
    }
    
    /**
     * step: Take a single transition in the automaton
     * 
     * Lower-level operation for incremental traversal.
     * Useful when building complex algorithms that need fine-grained control.
     * 
     * Process:
     *   Given current state and input symbol, find the next state.
     * 
     * @param current  The current state
     * @param symbol   The input symbol to process
     * @return         Next state if transition exists, nullptr otherwise
     * 
     * Example Usage:
     *   State* q = fsa.start_state();
     *   q = fsa.step(q, symbol_c);  // Advance on 'c'
     *   q = fsa.step(q, symbol_a);  // Advance on 'a'
     *   q = fsa.step(q, symbol_t);  // Advance on 't'
     *   if (q && q->is_final()) { accept; }
     */
    State* step(State* current, std::shared_ptr<Symbol> symbol) const {
        for (Transition* trans : current->outgoing()) {
            if (trans->matches(symbol)) {
                return trans->target();
            }
        }
        return nullptr;  // No matching transition found
    }
    
    /**
     * epsilon_closure: Compute ε-closure of a state
     * 
     * Definition:
     *   ε-closure(q) = {q' | q ->* q' via zero or more ε-transitions}
     * 
     * Purpose:
     *   In NFAs with ε-transitions, we can spontaneously move between states.
     *   The ε-closure tells us all states reachable without consuming input.
     * 
     * Current Implementation:
     *   Since we're using this for DFAs (no ε-transitions), the closure
     *   of any state is just {state} itself.
     * 
     * Future Extension:
     *   If ε-transitions are added, implement BFS/DFS:
     *   
     *   algorithm epsilon_closure(state):
     *     closure = {state}
     *     stack = [state]
     *     while stack not empty:
     *       q = stack.pop()
     *       for each ε-transition from q to q':
     *         if q' not in closure:
     *           closure.add(q')
     *           stack.push(q')
     *     return closure
     * 
     * @param state  The state whose ε-closure to compute
     * @return       Set of states reachable via ε-transitions
     */
    std::unordered_set<State*> epsilon_closure(State* state) const {
        std::unordered_set<State*> closure;
        closure.insert(state);
        
        // TODO: If epsilon transitions are added, implement BFS/DFS here
        // For now, just return the state itself
        
        return closure;
    }
    
    /**
     * final_states: Get all accepting states in this FSA
     * 
     * Utility method for:
     *   - Debugging (visualize accepting configurations)
     *   - Algorithms (NFA->DFA conversion, minimization)
     *   - Analysis (count number of accepting states)
     * 
     * Time Complexity: O(|Q|) where |Q| = number of states
     * 
     * @return Vector of pointers to all states where is_final() == true
     */
    std::vector<State*> final_states() const {
        std::vector<State*> finals;
        
        for (const auto& state : states_) {
            if (state->is_final()) {
                finals.push_back(state.get());
            }
        }
        
        return finals;
    }
};

#endif // FSA_H