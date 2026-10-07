#ifndef FST_H
#define FST_H

#include "automaton.h"
#include "state.h"
#include "transition.h"
#include <unordered_set>
#include <queue>
#include <tuple>

/**
 * FST: Finite-State Transducer
 * 
 * Purpose:
 *   A (possibly nondeterministic) finite-state transducer that implements
 *   a regular relation mapping input strings to output strings.
 * 
 * Formal Definition:
 *   An FST defines a relation R ⊆ Σ* × Γ* where:
 *     Σ* = set of all strings over input alphabet
 *     Γ* = set of all strings over output alphabet
 *   
 *   For each input string w ∈ Σ*, the FST may produce:
 *     - Zero outputs (reject/no translation)
 *     - One output (deterministic transduction)
 *     - Multiple outputs (nondeterministic transduction)
 * 
 * Two-Tape Model:
 *   FSTs process two tapes simultaneously:
 *     Lower tape (input):  The string being READ
 *     Upper tape (output): The string being WRITTEN
 *   
 *   Each transition is labeled a:b meaning "read a, write b"
 * 
 * Properties:
 *   1. Nondeterministic: Multiple transitions may match the same input
 *   2. Epsilon transitions: Can have ε on input, output, or both tapes
 *   3. Multiple outputs: Same input can transduce to different outputs
 * 
 * Use Cases in Morphology:
 *   1. Morphological Rules:
 *      +PL:s           (plural feature -> 's' suffix)
 *      y:i / _+PL      (y->i before plural)
 *   
 *   2. Orthographic Alternations:
 *      e:ε / _+ing     (delete 'e' before -ing)
 *      p:pp / _+ed     (double 'p' before -ed)
 *   
 *   3. Phonological Rules:
 *      /k/:s / _+PL    (voicing assimilation)
 * 
 * Complexity:
 *   - Transduction: O(2^n × m) worst case for NFA with n states, m input length
 *   - In practice: Usually linear or near-linear due to limited nondeterminism
 */
class FST : public Automaton {
public:
    /**
     * Constructor
     * 
     * @param symbol_table  Pointer to global SymbolTable
     * 
     * Inherits Automaton infrastructure and specializes to transduction.
     */
    FST(SymbolTable* symbol_table) : Automaton(symbol_table) {}
    
    /**
     * create_fst_state: Create a state with FST-specific capabilities
     * 
     * Overrides base create_state() to produce FSTState objects instead
     * of plain State objects.
     * 
     * FSTState adds:
     *   - final_output: symbols emitted upon reaching final state
     * 
     * @param is_final  Whether this state is accepting
     * @return          Pointer to newly created FSTState
     */
    FSTState* create_fst_state(bool is_final = false) {
        auto state = std::make_unique<FSTState>(next_state_id_++, is_final);
        FSTState* state_ptr = state.get();
        states_.push_back(std::move(state));
        return state_ptr;
    }
    
    /**
     * create_fst_transition: Create a transition with input:output pair
     * 
     * Creates an FSTTransition that reads input symbol and writes output symbol.
     * 
     * Transition Semantics:
     *   - input:output  (read input, write output)
     *   - input:ε       (read input, write nothing - deletion)
     *   - ε:output      (read nothing, write output - insertion)
     *   - ε:ε           (spontaneous transition, no I/O)
     * 
     * @param source  Source state
     * @param target  Destination state
     * @param input   Symbol to read from input tape (may be epsilon)
     * @param output  Symbol to write to output tape (may be epsilon)
     * @return        Pointer to created FSTTransition
     * 
     * Example:
     *   // Create transition: y:i (orthographic alternation)
     *   auto y_sym = symbols->get_or_create("y", SymbolType::SURFACE);
     *   auto i_sym = symbols->get_or_create("i", SymbolType::SURFACE);
     *   create_fst_transition(q0, q1, y_sym, i_sym);
     */
    FSTTransition* create_fst_transition(State* source, State* target,
                                         std::shared_ptr<Symbol> input,
                                         std::shared_ptr<Symbol> output) {
        auto trans = std::make_unique<FSTTransition>(target, input, output);
        FSTTransition* trans_ptr = trans.get();
        transitions_.push_back(std::move(trans));
        source->add_transition(trans_ptr);
        return trans_ptr;
    }
    
    /**
     * accepts: Check if FST accepts input (ignoring output)
     * 
     * Tests whether there exists ANY path through the FST that:
     *   1. Consumes all input symbols
     *   2. Reaches a final state
     * 
     * This is the "recognition" capability of an FST.
     * It doesn't compute the output - use transduce() for that.
     * 
     * Algorithm (BFS with nondeterminism):
     *   1. Start with configuration (q₀, position=0)
     *   2. Explore all reachable configurations:
     *      - Try each transition from current state
     *      - Epsilon input -> don't advance position
     *      - Matching input -> advance position
     *   3. Accept if we reach (q_final, position=|input|)
     * 
     * Nondeterminism Handling:
     *   Multiple transitions may be possible at any point.
     *   We explore ALL possible paths using BFS.
     *   Visited set prevents infinite loops on epsilon cycles.
     * 
     * Time Complexity: O(|Q| × |input|) with visited set pruning
     * 
     * @param input  Vector of input symbols
     * @return       true if ∃ accepting path, false otherwise
     */
    bool accepts(const std::vector<std::shared_ptr<Symbol>>& input) const override {
        if (!is_valid()) return false;
        
        // BFS queue: (state, input_position)
        std::queue<std::pair<State*, size_t>> frontier;
        frontier.push({start_state_, 0});
        
        // Visited configurations to avoid cycles
        // Configuration = (state_id, input_position)
        std::unordered_set<std::string> visited;
        
        while (!frontier.empty()) {
            auto [current, pos] = frontier.front();
            frontier.pop();
            
            // Generate unique configuration key
            std::string config = std::to_string(current->id()) + "," + std::to_string(pos);
            
            if (visited.count(config)) continue;  // Already explored
            visited.insert(config);
            
            // Check for accepting configuration
            if (pos == input.size() && current->is_final()) {
                return true;  // Found accepting path!
            }
            
            // Explore all outgoing transitions
            for (Transition* trans_base : current->outgoing()) {
                FSTTransition* trans = static_cast<FSTTransition*>(trans_base);
                
                if (trans->input()->is_epsilon()) {
                    // Epsilon input: take transition without consuming input
                    frontier.push({trans->target(), pos});
                } 
                else if (pos < input.size() && trans->matches(input[pos])) {
                    // Matching input: consume symbol and take transition
                    frontier.push({trans->target(), pos + 1});
                }
            }
        }
        
        return false;  // No accepting path found
    }
    
    /**
     * TransductionResult: Output of a single transduction path
     * 
     * Represents one possible input->output mapping through the FST.
     * 
     * Fields:
     *   - output: Sequence of output symbols produced
     *   - success: Whether this represents a valid transduction
     */
    struct TransductionResult {
        std::vector<std::shared_ptr<Symbol>> output;
        bool success;
    };
    
    /**
     * transduce: Map input string to ALL possible output strings
     * 
     * This is the core transduction operation. It computes the relation:
     *   R(input) = {output | (input, output) ∈ R}
     * 
     * Where R is the regular relation defined by this FST.
     * 
     * Algorithm (BFS with output accumulation):
     *   1. Start with configuration (q₀, pos=0, output=[])
     *   2. For each configuration (q, pos, out):
     *      a. If pos==|input| and q is final -> add out to results
     *      b. For each transition from q:
     *         - Check if input side matches
     *         - Accumulate output symbol (if non-epsilon)
     *         - Create new configuration with updated pos and output
     *   3. Return all accumulated outputs
     * 
     * Nondeterminism:
     *   Multiple paths may produce different outputs for the same input.
     *   We collect ALL of them.
     * 
     * Example:
     *   FST with rules: y:i / _+es, +PL:es
     *   Input: [y, +PL]
     *   Output: [{[i, e, s], true}]  (one result: "ies")
     * 
     * Cycle Detection:
     *   Configuration = (state, input_pos, output_length)
     *   This prevents infinite loops on epsilon cycles.
     * 
     * Time Complexity: O(2^n × m) worst case, but usually much better
     *   - n = number of states
     *   - m = input length
     * 
     * @param input  Vector of input symbols
     * @return       Vector of all possible transduction results
     */
    std::vector<TransductionResult> transduce(
        const std::vector<std::shared_ptr<Symbol>>& input) const {
        
        std::vector<TransductionResult> results;
        
        if (!is_valid()) return results;
        
        /**
         * SearchState: Configuration during transduction
         * 
         * Represents a snapshot of computation:
         *   - state: Current state in FST
         *   - input_pos: How much input we've consumed
         *   - output: What we've written to output tape so far
         */
        struct SearchState {
            State* state;
            size_t input_pos;
            std::vector<std::shared_ptr<Symbol>> output;
        };
        
        // BFS queue
        std::queue<SearchState> frontier;
        frontier.push({start_state_, 0, {}});
        
        // Visited set: (state_id, input_pos, output_length)
        // We track output length to avoid infinite epsilon output loops
        std::unordered_set<std::string> visited;
        
        while (!frontier.empty()) {
            SearchState current = frontier.front();
            frontier.pop();
            
            // Configuration key for cycle detection
            std::string config = std::to_string(current.state->id()) + "," +
                                 std::to_string(current.input_pos) + "," +
                                 std::to_string(current.output.size());
            
            if (visited.count(config)) continue;
            visited.insert(config);
            
            // Check for accepting configuration
            if (current.input_pos == input.size() && current.state->is_final()) {
                results.push_back({current.output, true});
                continue;  // Don't stop - keep searching for more solutions
            }
            
            // Explore all transitions from current state
            for (Transition* trans_base : current.state->outgoing()) {
                FSTTransition* trans = static_cast<FSTTransition*>(trans_base);
                
                bool can_take = false;
                std::vector<std::shared_ptr<Symbol>> new_output = current.output;
                size_t new_pos = current.input_pos;
                
                // Check if we can take this transition
                if (trans->input()->is_epsilon()) {
                    // Epsilon input: always takeable, doesn't consume input
                    can_take = true;
                    // new_pos stays the same
                } 
                else if (current.input_pos < input.size() && 
                         trans->matches(input[current.input_pos])) {
                    // Non-epsilon input that matches current symbol
                    can_take = true;
                    new_pos = current.input_pos + 1;  // Consume one symbol
                }
                
                // If transition is takeable, process output side
                if (can_take) {
                    if (!trans->output()->is_epsilon()) {
                        // Non-epsilon output: append to output tape
                        new_output.push_back(trans->output());
                    }
                    // Epsilon output: write nothing (new_output unchanged)
                    
                    // Add new configuration to frontier
                    frontier.push({trans->target(), new_pos, new_output});
                }
            }
        }
        
        return results;
    }
};

#endif // FST_H