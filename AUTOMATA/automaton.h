#ifndef AUTOMATON_H
#define AUTOMATON_H

#include <vector>
#include <memory>
#include <unordered_map>
#include "state.h"
#include "transition.h"
#include "../SYMBOLS/symbol.h"

/**
 * Automaton: Abstract base class for finite-state machines
 * 
 * Purpose:
 *   Provides the foundational structure and operations for all automata types.
 *   Concrete implementations (FSA, FST) inherit from this class.
 * 
 * Formal Definition:
 *   A finite automaton is a 5-tuple M = (Q, Σ, δ, q₀, F) where:
 *     Q  = finite set of states (states_)
 *     Σ  = finite alphabet (managed by symbol_table_)
 *     δ  = transition function (represented by Transition objects)
 *     q₀ = start/initial state (start_state_)
 *     F  = set of final/accepting states (states where is_final() == true)
 * 
 * Responsibilities:
 *   1. Memory Management:
 *      - Owns all State objects (via unique_ptr)
 *      - Owns all Transition objects (via unique_ptr)
 *      - References SymbolTable (non-owning pointer)
 *   
 *   2. Factory Methods:
 *      - Provides create_state() and create_transition()
 *      - Assigns unique IDs to states
 *      - Maintains object lifecycle
 *   
 *   3. Structure Validation:
 *      - Ensures automaton has required components
 *      - Provides is_valid() check
 * 
 * Design Pattern:
 *   This follows the "Abstract Factory" pattern. Subclasses can override
 *   factory methods to create specialized states/transitions (e.g., FSTState).
 * 
 * Inheritance Hierarchy:
 *   Automaton (abstract)
 *     ├── FSA (deterministic acceptor)
 *     └── FST (nondeterministic transducer)
 */
class Automaton {
protected:
    // ========== State Storage ==========
    
    // All states owned by this automaton
    // unique_ptr ensures automatic cleanup when automaton is destroyed
    std::vector<std::unique_ptr<State>> states_;
    
    // Pointer to the designated start state (non-owning)
    // This must be one of the states in states_ vector
    State* start_state_;
    
    // ========== Transition Storage ==========
    
    // All transitions owned by this automaton
    // Transitions are edges in the automaton graph
    std::vector<std::unique_ptr<Transition>> transitions_;
    
    // ========== External References ==========
    
    // Non-owning pointer to global symbol table
    // The symbol table must outlive this automaton
    // Shared across all automata in the system
    SymbolTable* symbol_table_;
    
    // ========== State ID Management ==========
    
    // Counter for assigning unique IDs to new states
    // Incremented each time create_state() is called
    int next_state_id_;
    
public:
    /**
     * Constructor
     * 
     * @param symbol_table  Pointer to the global SymbolTable
     *                      (must outlive this Automaton)
     * 
     * Initializes:
     *   - start_state to nullptr (must be set explicitly)
     *   - next_state_id to 0
     *   - Empty state and transition vectors
     */
    Automaton(SymbolTable* symbol_table)
        : start_state_(nullptr), 
          symbol_table_(symbol_table),
          next_state_id_(0) {}
    
    /**
     * Virtual destructor for proper polymorphic cleanup
     * 
     * When deleting an Automaton* pointer that points to a derived class,
     * this ensures the derived class destructor is called.
     * 
     * Cleanup happens automatically via unique_ptr:
     *   - states_ vector destruction -> all States destroyed
     *   - transitions_ vector destruction -> all Transitions destroyed
     */
    virtual ~Automaton() = default;
    
    // ========== Factory Methods ==========
    
    /**
     * Create a new state and add it to this automaton
     * 
     * Process:
     *   1. Allocate new State with unique ID
     *   2. Store in states_ vector (transfer ownership)
     *   3. Return raw pointer for use in transition creation
     * 
     * Memory Management:
     *   - Automaton owns the State (via unique_ptr in states_)
     *   - Caller receives raw pointer for immediate use
     *   - State is automatically destroyed when automaton is destroyed
     * 
     * @param is_final  Whether this state should be accepting (default: false)
     * @return          Raw pointer to the newly created State
     * 
     * Example:
     *   State* q0 = automaton.create_state(false);  // Non-final
     *   State* q1 = automaton.create_state(true);   // Final/accepting
     */
    State* create_state(bool is_final = false) {
        // Create State with unique ID, then increment counter
        auto state = std::make_unique<State>(next_state_id_++, is_final);
        State* state_ptr = state.get();  // Get raw pointer before moving
        states_.push_back(std::move(state));  // Transfer ownership to vector
        return state_ptr;
    }
    
    /**
     * Set the start state of the automaton
     * 
     * The start state (q₀) is where computation begins.
     * 
     * Requirements:
     *   - state must be one of the states created by this automaton
     *   - Calling this multiple times replaces the previous start state
     * 
     * @param state  Pointer to a State owned by this automaton
     * 
     * Example:
     *   State* q0 = automaton.create_state();
     *   automaton.set_start_state(q0);
     */
    void set_start_state(State* state) {
        start_state_ = state;
    }
    
    /**
     * Get the start state
     * 
     * @return Pointer to the start state, or nullptr if not set
     */
    State* start_state() const {
        return start_state_;
    }
    
    /**
     * Create a transition and add it to the automaton
     * 
     * Process:
     *   1. Create Transition object connecting source -> target via input symbol
     *   2. Store in transitions_ vector (automaton owns it)
     *   3. Register with source state's outgoing transition list
     *   4. Return raw pointer for reference
     * 
     * This creates a directed edge: source --(input)--> target
     * 
     * @param source  Source state (where transition originates)
     * @param target  Destination state (where transition leads)
     * @param input   Symbol that must be matched to traverse
     * @return        Raw pointer to the created Transition
     * 
     * Example:
     *   State* q0 = automaton.create_state();
     *   State* q1 = automaton.create_state();
     *   auto sym_a = symbols.get_or_create("a", SymbolType::SURFACE);
     *   
     *   automaton.create_transition(q0, q1, sym_a);
     *   // Now: q0 --(a)--> q1
     */
    Transition* create_transition(State* source, State* target, 
                                  std::shared_ptr<Symbol> input) {
        auto trans = std::make_unique<Transition>(target, input);
        Transition* trans_ptr = trans.get();
        transitions_.push_back(std::move(trans));  // Automaton owns it
        source->add_transition(trans_ptr);          // Source state references it
        return trans_ptr;
    }
    
    // ========== Query Methods ==========
    
    /**
     * Get the number of states in this automaton
     * 
     * Useful for:
     *   - Complexity analysis
     *   - Debugging
     *   - Statistics
     * 
     * @return Number of states in states_ vector
     */
    size_t num_states() const { return states_.size(); }
    
    /**
     * Get the number of transitions in this automaton
     * 
     * @return Number of transitions in transitions_ vector
     */
    size_t num_transitions() const { return transitions_.size(); }
    
    /**
     * Check if a start state has been set
     * 
     * @return true if start_state_ is non-null, false otherwise
     */
    bool has_start_state() const { return start_state_ != nullptr; }
    
    /**
     * Get the symbol table used by this automaton
     * 
     * @return Non-owning pointer to the SymbolTable
     */
    SymbolTable* symbols() const { return symbol_table_; }
    
    // ========== Abstract Interface ==========
    
    /**
     * Check if the automaton accepts an input string
     * 
     * PURE VIRTUAL - must be implemented by subclasses
     * 
     * Different automaton types have different acceptance semantics:
     *   - FSA: Deterministic traversal, single path
     *   - FST: Nondeterministic traversal, multiple paths with output
     * 
     * @param input  Vector of Symbol objects representing the input string
     * @return       true if input is in the language L(M), false otherwise
     */
    virtual bool accepts(const std::vector<std::shared_ptr<Symbol>>& input) const = 0;
    
    /**
     * Check if the automaton is structurally valid
     * 
     * Basic validity requirements:
     *   1. Must have at least one state
     *   2. Must have a designated start state
     * 
     * Note: This doesn't check for:
     *   - Unreachable states
     *   - Dead states (non-final with no path to final)
     *   - Determinism properties
     * 
     * @return true if automaton meets minimum structural requirements
     */
    bool is_valid() const {
        if (!has_start_state()) return false;
        if (states_.empty()) return false;
        return true;
    }
    
protected:
    /**
     * Find a state by its unique ID
     * 
     * Helper method for internal use (e.g., during deserialization).
     * 
     * Complexity: O(n) where n = number of states
     *   (could be optimized with a hash map if needed frequently)
     * 
     * @param id  The unique ID to search for
     * @return    Pointer to the State with that ID, or nullptr if not found
     */
    State* find_state(int id) const {
        for (const auto& state : states_) {
            if (state->id() == id) {
                return state.get();
            }
        }
        return nullptr;
    }
};

#endif // AUTOMATON_H