#ifndef STATE_H
#define STATE_H

#include <vector>
#include <memory>
#include "../SYMBOLS/symbol.h"

// Forward declarations to avoid circular dependencies
// These classes are defined in transition.h, which includes this file
class Transition;
class FSTTransition;

/**
 * State: Represents a single state (node) in a finite-state automaton
 * 
 * Conceptual Role:
 *   In formal automata theory, a state is a configuration point in the computation.
 *   The automaton transitions between states as it processes input symbols.
 * 
 * Graph Representation:
 *   - States are NODES in the automaton graph
 *   - Transitions are EDGES between states
 *   - Automaton structure: G = (V, E) where V = states, E = transitions
 * 
 * State Properties:
 *   1. ID: Unique integer identifier within the automaton
 *   2. Finality: Boolean flag indicating whether this is an accepting state
 *   3. Outgoing Transitions: List of edges emanating from this state
 * 
 * Memory Management:
 *   - The Automaton owns State objects (via unique_ptr)
 *   - State stores raw pointers to Transitions (also owned by Automaton)
 *   - This avoids circular shared_ptr dependencies that would cause memory leaks
 * 
 * Design Pattern:
 *   This follows the "non-owning observer" pattern for transitions.
 *   State can read/traverse transitions but doesn't manage their lifetime.
 */
class State {
private:
    int id_;                            // Unique identifier within the automaton
    bool is_final_;                     // True if this is an accepting/final state
    
    // Outgoing transitions from this state
    // Raw pointers because Automaton owns the actual Transition objects
    // This prevents shared_ptr cycles: Automaton->State, Automaton->Transition, State->Transition
    std::vector<Transition*> outgoing_;
    
public:
    /**
     * Constructor
     * 
     * @param id        Unique integer ID for this state
     * @param is_final  Whether this state is accepting (default: false)
     * 
     * Note: The ID should be assigned by the Automaton to ensure uniqueness.
     */
    State(int id, bool is_final = false)
        : id_(id), is_final_(is_final) {}
    
    // ========== Accessors ==========
    
    /**
     * Get the unique ID of this state
     * 
     * State IDs are used for:
     *   - Identifying states in algorithms (e.g., visited sets)
     *   - Debugging and visualization
     *   - Maintaining state ordering
     */
    int id() const { return id_; }
    
    /**
     * Check if this is a final (accepting) state
     * 
     * In automata theory:
     *   - A state q ∈ F (the set of final states) accepts input
     *   - Input is accepted if: after consuming all symbols, we're in a final state
     * 
     * @return true if this state is accepting, false otherwise
     */
    bool is_final() const { return is_final_; }
    
    /**
     * Mark this state as final or non-final
     * 
     * Useful for:
     *   - Dynamic automaton construction
     *   - Modifying automata after creation
     *   - Converting NFAs to DFAs (subset construction)
     * 
     * @param final  New finality status
     */
    void set_final(bool final) { is_final_ = final; }
    
    // ========== Transition Management ==========
    
    /**
     * Add an outgoing transition from this state
     * 
     * Process:
     *   This state becomes the SOURCE of the transition.
     *   The transition's target() method returns the DESTINATION state.
     * 
     * Memory Safety:
     *   The Automaton must ensure the Transition object outlives this State.
     *   We store a raw pointer, so no ownership transfer occurs.
     * 
     * @param trans  Raw pointer to a Transition owned by the Automaton
     * 
     * Example:
     *   State* q0 = automaton.create_state();
     *   State* q1 = automaton.create_state();
     *   Transition* t = automaton.create_transition(q0, q1, symbol_a);
     *   q0->add_transition(t);  // q0 now has outgoing transition to q1
     */
    void add_transition(Transition* trans) {
        outgoing_.push_back(trans);
    }
    
    /**
     * Get all outgoing transitions from this state
     * 
     * Used for:
     *   - Automaton traversal (finding next possible states)
     *   - Graph algorithms (BFS, DFS)
     *   - Determinism checking (ensure ≤1 transition per symbol)
     * 
     * @return const reference to vector of transition pointers
     */
    const std::vector<Transition*>& outgoing() const {
        return outgoing_;
    }
    
    // ========== Query Methods ==========
    
    /**
     * Count the number of outgoing transitions
     * 
     * Useful for:
     *   - Checking if state is a dead end (num_outgoing() == 0)
     *   - Determinism analysis (for DFAs, should be ≤|Σ|)
     *   - Graph metrics (out-degree)
     */
    size_t num_outgoing() const { return outgoing_.size(); }
    
    /**
     * Check if this state has any outgoing transitions
     * 
     * @return true if at least one transition exists, false if this is a dead end
     */
    bool has_outgoing() const { return !outgoing_.empty(); }
};

/**
 * FSTState: State specialized for finite-state transducers
 * 
 * Extension of State for FSTs (Finite-State Transducers).
 * 
 * FST vs FSA:
 *   - FSA: Accepts/rejects input strings (recognition)
 *   - FST: Maps input strings to output strings (transduction)
 * 
 * Additional Capability:
 *   FSTs can emit output symbols:
 *   1. On transitions: transition labeled a:b reads 'a', writes 'b'
 *   2. On final states: emit additional symbols when reaching acceptance
 * 
 * Final Output:
 *   Some FST formalisms allow states to emit output upon reaching them.
 *   This is stored in final_output_ and appended to the transduction result.
 * 
 * Example:
 *   An FST that adds a suffix:
 *     Regular transitions build output
 *     Final state emits "+PAST" as final_output
 * 
 * Current Implementation:
 *   Most output happens on transitions (FSTTransition handles input:output pairs).
 *   Final output is supported but rarely used in this system.
 *   This class exists primarily for type safety and future extensibility.
 */
class FSTState : public State {
private:
    // Output symbols emitted when this final state is reached
    // Empty for most states; used for specialized transduction patterns
    std::vector<std::shared_ptr<Symbol>> final_output_;
    
public:
    /**
     * Constructor
     * 
     * @param id        Unique state ID
     * @param is_final  Whether this is an accepting state
     * 
     * Inherits all State functionality, adds final output capability.
     */
    FSTState(int id, bool is_final = false)
        : State(id, is_final) {}
    
    // ========== Final Output Management ==========
    
    /**
     * Set the output symbols emitted when reaching this final state
     * 
     * Use case:
     *   Some morphological rules emit features only at the end:
     *   walk + [walk_state] -> [final_state with output "+V +PAST"]
     * 
     * @param output  Vector of symbols to emit upon reaching this state
     */
    void set_final_output(const std::vector<std::shared_ptr<Symbol>>& output) {
        final_output_ = output;
    }
    
    /**
     * Get the final output symbols (if any)
     * 
     * During transduction, if we reach this state:
     *   total_output = transition_outputs + final_output
     * 
     * @return const reference to final output symbol vector
     */
    const std::vector<std::shared_ptr<Symbol>>& final_output() const {
        return final_output_;
    }
    
    /**
     * Check if this state has final output
     * 
     * @return true if final_output is non-empty, false otherwise
     */
    bool has_final_output() const {
        return !final_output_.empty();
    }
};

#endif // STATE_H