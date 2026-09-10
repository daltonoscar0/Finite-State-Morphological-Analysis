#ifndef TRANSITION_H
#define TRANSITION_H

#include <memory>
#include "../SYMBOLS/symbol.h"

// Forward declaration to avoid circular dependency
// State is defined in state.h, which includes this file
class State;

/**
 * Transition: Base class for edges in finite-state automata
 * 
 * Conceptual Role:
 *   A transition is a directed edge in the automaton graph that defines
 *   state changes based on input symbols.
 * 
 * Formal Definition:
 *   δ(q, a) = q'  means "from state q, on input a, go to state q'"
 * 
 * Components:
 *   1. Source state: Implicit (stored by State's outgoing_ vector)
 *   2. Target state: Explicit (this->target_)
 *   3. Input symbol: What must be matched to traverse (this->input_)
 * 
 * Graph Structure:
 *   State q1 --(input='a')--> State q2
 *   
 *   This Transition object represents the edge, storing:
 *     - target_ = q2
 *     - input_ = Symbol('a')
 *   
 *   The source (q1) is implicit because q1.outgoing_ contains this Transition.
 * 
 * Epsilon Transitions:
 *   - Transitions where input is ε (epsilon symbol)
 *   - Can be taken without consuming input
 *   - Used in NFAs but avoided in DFAs
 * 
 * Design Rationale:
 *   Base class uses inheritance to allow specialization:
 *     - Transition: For FSAs (single input alphabet)
 *     - FSTTransition: For FSTs (input AND output alphabets)
 */
class Transition {
protected:
    State* target_;                    // Destination state (raw pointer - automaton owns it)
    std::shared_ptr<Symbol> input_;    // Symbol required to traverse this transition
    
public:
    /**
     * Constructor
     * 
     * @param target  Pointer to the destination state
     * @param input   Symbol that must be matched to take this transition
     * 
     * Memory Management:
     *   - target is a raw pointer (Automaton owns the State)
     *   - input is a shared_ptr (SymbolTable owns the Symbol, but many can reference it)
     */
    Transition(State* target, std::shared_ptr<Symbol> input)
        : target_(target), input_(input) {}
    
    /**
     * Virtual destructor for proper cleanup of derived classes
     * 
     * Enables polymorphic deletion: delete base_ptr will call derived destructor
     */
    virtual ~Transition() = default;
    
    // ========== Accessors ==========
    
    /**
     * Get the target (destination) state of this transition
     * 
     * @return Raw pointer to the state reached by taking this transition
     */
    State* target() const { return target_; }
    
    /**
     * Get the input symbol required to traverse this transition
     * 
     * @return Shared pointer to the Symbol that must be matched
     */
    std::shared_ptr<Symbol> input() const { return input_; }
    
    // ========== Query Methods ==========
    
    /**
     * Check if this is an epsilon (ε) transition
     * 
     * Epsilon transitions:
     *   - Can be taken without consuming input
     *   - Used for nondeterminism and structural purposes
     *   - Require special handling in traversal algorithms
     * 
     * @return true if input symbol is epsilon, false otherwise
     */
    bool is_epsilon() const { return input_->is_epsilon(); }
    
    /**
     * Check if a given symbol matches this transition's input
     * 
     * Matching Rules:
     *   1. If this is an epsilon transition, always returns true
     *      (epsilon can be taken anytime, consuming no input)
     *   2. Otherwise, compares symbol == input_ for equality
     * 
     * Virtual:
     *   Allows FSTTransition to override with more complex matching
     *   (e.g., matching both input and output tapes)
     * 
     * @param symbol  The symbol to check against this transition's input
     * @return        true if the symbol matches and transition can be taken
     * 
     * Example:
     *   Transition with input_='a'
     *     matches(Symbol('a')) -> true
     *     matches(Symbol('b')) -> false
     *     matches(Symbol::epsilon()) -> false (not an epsilon transition)
     *   
     *   Transition with input_=epsilon
     *     matches(Symbol('a')) -> true (epsilon always matches)
     *     matches(Symbol('b')) -> true
     */
    virtual bool matches(std::shared_ptr<Symbol> symbol) const {
        if (input_->is_epsilon()) {
            // Epsilon transitions always match (but consume no input)
            // This allows spontaneous state changes
            return true;
        }
        // Non-epsilon: require exact symbol match
        return *input_ == *symbol;
    }
};

/**
 * FSTTransition: Transducer transition with input->output mapping
 * 
 * Purpose:
 *   FSTs (Finite-State Transducers) map input strings to output strings.
 *   Each transition reads an input symbol and optionally writes an output symbol.
 * 
 * Two-Tape Model:
 *   FSTs operate on two tapes simultaneously:
 *     - Lower tape (input): The string being read
 *     - Upper tape (output): The string being written
 * 
 * Transition Notation:
 *   a:b means "read 'a' from input, write 'b' to output"
 * 
 * Special Cases:
 *   - a:b   (read a, write b)     - substitution
 *   - a:ε   (read a, write nothing) - deletion
 *   - ε:b   (read nothing, write b) - insertion
 *   - ε:ε   (epsilon on both tapes) - spontaneous transition
 * 
 * Morphological Examples:
 *   +PL:s        Read "+PL" feature, write "s" suffix
 *   y:i          Read "y", write "i" (orthographic alternation)
 *   e:ε          Read "e", write nothing (e-deletion before -ing)
 * 
 * Design Note:
 *   We model output as a single symbol for simplicity. Multi-symbol output
 *   is achieved by chaining transitions:
 *     state1 --(+PL:e)--> state2 --(ε:s)--> state3
 *   This reads "+PL" and writes "es"
 */
class FSTTransition : public Transition {
private:
    std::shared_ptr<Symbol> output_;    // Symbol written to output tape
    
public:
    /**
     * Constructor
     * 
     * @param target  Destination state
     * @param input   Symbol read from input tape (may be epsilon)
     * @param output  Symbol written to output tape (may be epsilon)
     * 
     * Examples:
     *   FSTTransition(q1, Symbol('a'), Symbol('b'))  // a:b
     *   FSTTransition(q1, Symbol('+PL'), Symbol('s')) // +PL:s
     *   FSTTransition(q1, Symbol::epsilon(), Symbol('e')) // ε:e (insertion)
     */
    FSTTransition(State* target, 
                  std::shared_ptr<Symbol> input,
                  std::shared_ptr<Symbol> output)
        : Transition(target, input), output_(output) {}
    
    // ========== Output Accessor ==========
    
    /**
     * Get the output symbol written when taking this transition
     * 
     * @return Shared pointer to the output Symbol
     */
    std::shared_ptr<Symbol> output() const { return output_; }
    
    // ========== Query Methods ==========
    
    /**
     * Check if this transition produces output
     * 
     * Returns false for deletion transitions (input:ε)
     * Returns true for substitution/insertion (input:symbol)
     * 
     * @return true if output is non-epsilon, false if output is ε
     */
    bool has_output() const { return !output_->is_epsilon(); }
    
    /**
     * Check if both input and output are epsilon
     * 
     * Pure epsilon transitions (ε:ε) allow state changes without
     * consuming input or producing output.
     * 
     * Use cases:
     *   - Structural transitions in FST composition
     *   - Connecting sub-automata
     *   - NFA to DFA conversion
     * 
     * @return true if both tapes have epsilon, false otherwise
     */
    bool is_epsilon_both() const {
        return input_->is_epsilon() && output_->is_epsilon();
    }
    
    /**
     * Check if both input and output match given symbols
     * 
     * Useful for:
     *   - FST composition algorithms
     *   - Finding specific input:output mappings
     *   - Debugging transduction paths
     * 
     * @param in   Symbol to match against input
     * @param out  Symbol to match against output
     * @return     true if both match, false otherwise
     * 
     * Example:
     *   FSTTransition with (+PL:s)
     *     matches_pair(Symbol("+PL"), Symbol("s")) -> true
     *     matches_pair(Symbol("+PL"), Symbol("es")) -> false
     */
    bool matches_pair(std::shared_ptr<Symbol> in, std::shared_ptr<Symbol> out) const {
        return *input_ == *in && *output_ == *out;
    }
};

#endif // TRANSITION_H