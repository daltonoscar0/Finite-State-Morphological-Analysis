#ifndef LEXICON_FSA_H
#define LEXICON_FSA_H

#include "../AUTOMATA/fsa.h"
#include "lexeme.h"
#include <memory>
#include <unordered_map>
#include <iostream>

/**
 * LexiconFSA: Trie-structured finite-state automaton for lexicon storage
 * 
 * Purpose:
 *   Stores all lexemes (dictionary entries) as paths through a shared-prefix trie.
 *   This enables efficient prefix-based lookup during morphological analysis.
 * 
 * Data Structure: Trie (Prefix Tree)
 *   A trie is a tree where:
 *     - Each edge is labeled with a character
 *     - Paths from root spell out strings
 *     - Terminal nodes mark complete entries
 * 
 * Example Trie for ["cat", "cats", "car", "card"]:
 * 
 *                    (root)
 *                      |
 *                      c
 *                      |
 *                      a
 *                    /   \
 *                   t     r
 *                  [cat]  |
 *                   |     |
 *                   s     d
 *                 [cats] [card]
 * 
 * Where [word] indicates a final state annotated with lexeme information.
 * 
 * Space Efficiency:
 *   Shared prefixes are stored once:
 *     "cat", "cats", "caterpillar" -> "cat" prefix shared
 *   
 *   For a lexicon of 100,000 words:
 *     - Naive storage: ~100K separate entries
 *     - Trie storage: ~50-70K nodes (significant savings)
 * 
 * Time Complexity:
 *   - Insertion: O(m) where m = word length
 *   - Lookup: O(m) where m = word length
 *   - Both are independent of lexicon size!
 * 
 * Why FSA?
 *   The trie IS a deterministic finite-state acceptor:
 *     - States = nodes in the trie
 *     - Transitions = character-labeled edges
 *     - Final states = terminal nodes (words in lexicon)
 *     - Language: L(trie) = {all words in lexicon}
 * 
 * Role in Morphological Analysis:
 *   During analysis of "cities":
 *     1. Try stem "c" -> not in lexicon
 *     2. Try stem "ci" -> not in lexicon
 *     3. Try stem "cit" -> not in lexicon
 *     4. Try stem "citi" -> not in lexicon (but check i->y)
 *     5. Try stem "city" -> FOUND (via y->i reversal)
 *   
 *   The trie makes these lookups efficient O(m) instead of O(n)
 *   where n = lexicon size.
 */

/**
 * LexiconState: Metadata for terminal states in the lexicon FSA
 * 
 * Purpose:
 *   Terminal states (final states) in the lexicon trie need additional information:
 *     - What lexeme does this path represent?
 *     - What is its part of speech?
 *     - What is its stem class?
 * 
 * Design:
 *   We can't modify the base State class (would bloat all states).
 *   Instead, we maintain a parallel map: State* -> LexiconState metadata.
 *   Only terminal states have entries in this map.
 * 
 * Example:
 *   State 42 (final) in the trie spells "cat"
 *   state_annotations_[State42] -> LexiconState with lexeme "cat +N [REG]"
 */
struct LexiconState {
    State* state;
    std::vector<std::shared_ptr<Lexeme>> lexemes;  // All lexemes at this state (multi-POS)

    LexiconState(State* s) : state(s) {}

    bool is_terminal() const { return !lexemes.empty(); }

    // Returns first lexeme's POS (backward compat)
    PartOfSpeech pos() const {
        return lexemes.empty() ? PartOfSpeech::NOUN : lexemes[0]->pos();
    }

    StemClass stem_class() const {
        return lexemes.empty() ? StemClass::UNKNOWN : lexemes[0]->stem_class();
    }
};

/**
 * LexiconFSA: The lexicon automaton itself
 * 
 * Inheritance:
 *   Extends FSA (deterministic finite-state acceptor)
 *   Adds lexicon-specific operations (insert, lookup, annotations)
 */
class LexiconFSA : public FSA {
private:
    // Map from FSA states to lexeme metadata
    // Only terminal states have entries
    // Non-owning State* keys, owned shared_ptr values
    std::unordered_map<State*, std::shared_ptr<LexiconState>> state_annotations_;

    // Storage for all lexemes (for ownership and iteration)
    std::vector<std::shared_ptr<Lexeme>> lexemes_;

    // Single-byte surface symbols, cached by byte value.
    //
    // The trie walks one byte at a time, and routing every one of those bytes
    // through the symbol table means building a one-character std::string and
    // hashing it. Loading a 500k-entry lexicon walks about four million bytes,
    // and analysis walks a quadratic number of them per word, because it tries
    // every stem length and looks up four candidate stems for each. Caching
    // the 256 possible symbols turns that into an array index.
    //
    // Filled lazily, so no symbol is created that the lexicon does not use.
    // The symbol table keys its registry on the representation alone, so a
    // one-byte representation names exactly one symbol and this cache cannot
    // disagree with it.
    mutable std::shared_ptr<Symbol> byte_symbol_[256];

    /**
     * child: The trie node reached from `from` on the symbol with this ID
     *
     * FSA::step does the same walk through the virtual Transition::matches,
     * which takes its argument by value and so pays a reference count update
     * per transition examined. The trie is walked often enough that this
     * matters: comparing symbol IDs directly is the same test with none of
     * that cost. Nothing else about the traversal changes, and FSA::step is
     * left alone for the rule FSTs, which need the virtual dispatch.
     *
     * @param from       Source state
     * @param symbol_id  ID of the symbol to consume
     * @return           Target state, or null if there is no such edge
     */
    static State* child(State* from, int symbol_id) {
        for (Transition* trans : from->outgoing()) {
            if (trans->input_id() == symbol_id) return trans->target();
        }
        return nullptr;
    }

    /** symbol_for: The surface symbol for a byte, creating it if needed */
    const std::shared_ptr<Symbol>& symbol_for(unsigned char c) {
        auto& slot = byte_symbol_[c];
        if (!slot) {
            slot = symbols()->get_or_create(std::string(1, static_cast<char>(c)),
                                            SymbolType::SURFACE);
        }
        return slot;
    }

    /**
     * cached_symbol: The surface symbol for a byte, or null if unknown
     *
     * A byte that no lexeme contains has no symbol and no trie edge, so a null
     * result means the lookup cannot succeed. Only non-null results are
     * cached, since a later insertion can introduce the symbol.
     */
    const Symbol* cached_symbol(unsigned char c) const {
        auto& slot = byte_symbol_[c];
        if (!slot) {
            slot = symbols()->lookup(std::string(1, static_cast<char>(c)));
        }
        return slot.get();
    }

public:
    /**
     * Constructor
     *
     * @param symbol_table  Global symbol table (passed to FSA base class)
     */
    LexiconFSA(SymbolTable* symbol_table) : FSA(symbol_table) {}

    /**
     * insert_lexeme: Add a lexeme to the trie
     * 
     * Algorithm (standard trie insertion):
     *   1. If trie is empty, create root state
     *   2. Start at root
     *   3. For each character c in stem:
     *      a. If transition for c exists, follow it
     *      b. Otherwise, create new state and transition for c
     *   4. Mark final state as terminal and attach lexeme metadata
     * 
     * Example:
     *   Inserting "cat" into empty trie:
     *     Create: root --c--> state1 --a--> state2 --t--> state3[cat]
     *   
     *   Then inserting "car":
     *     Reuse: root --c--> state1 --a--> state2
     *     Create: state2 --r--> state4[car]
     * 
     * Time Complexity: O(m) where m = stem length
     * Space Complexity: O(m) new states in worst case (no prefix sharing)
     * 
     * @param lexeme  Shared pointer to the Lexeme to insert
     */
    void insert_lexeme(std::shared_ptr<Lexeme> lexeme) {
        // Store lexeme for ownership and iteration
        lexemes_.push_back(lexeme);

        // Ensure trie has a root
        if (!has_start_state()) {
            set_start_state(create_state(false));  // Root is not terminal
        }

        State* current = start_state();
        const std::string& stem = lexeme->stem();

        // Traverse/create path for each character
        for (char c : stem) {
            const auto& symbol = symbol_for(static_cast<unsigned char>(c));

            // Check if transition already exists
            State* next = child(current, symbol->id());

            if (next == nullptr) {
                // No existing path - create new state and transition
                next = create_state(false);
                create_transition(current, next, symbol);
            }

            current = next;
        }

        // Mark final state and push lexeme into vector (supports multi-POS).
        // Annotations exist for terminal states only: a 500k-entry lexicon has
        // about 1.5 million trie nodes and only 500k of them carry lexemes, so
        // giving every interior node an annotation object cost three times the
        // memory and the allocations for nothing. Interior states have no entry
        // and get_annotation returns null for them, which is what it has always
        // done for a state it does not know.
        current->set_final(true);
        auto& annotation = state_annotations_[current];
        if (!annotation) annotation = std::make_shared<LexiconState>(current);
        annotation->lexemes.push_back(lexeme);
    }
    
    /**
     * lookup: Find lexeme for a given stem string
     * 
     * Algorithm (standard trie lookup):
     *   1. Start at root
     *   2. For each character c in stem:
     *      a. Follow transition labeled c
     *      b. If no such transition, stem not in lexicon -> return null
     *   3. If we reach a final state, return its lexeme
     *   4. Otherwise (reached non-terminal state), return null
     * 
     * Example:
     *   lookup("cat"):
     *     root --c--> state1 --a--> state2 --t--> state3
     *     state3 is final -> return lexeme("cat")
     *   
     *   lookup("ca"):
     *     root --c--> state1 --a--> state2
     *     state2 is NOT final -> return null
     *   
     *   lookup("dog") [not in trie]:
     *     root --d--> ??? (no transition)
     *     return null
     * 
     * Time Complexity: O(m) where m = stem length
     * 
     * @param stem  The string to look up
     * @return      Shared pointer to Lexeme if found, nullptr otherwise
     */
    /**
     * lookup_all: Return all lexemes at the terminal state for a stem
     */
    std::vector<std::shared_ptr<Lexeme>> lookup_all(const std::string& stem) const {
        if (!has_start_state()) return {};

        State* current = start_state();

        for (char c : stem) {
            const Symbol* symbol = cached_symbol(static_cast<unsigned char>(c));
            if (!symbol) return {};

            State* next = child(current, symbol->id());
            if (next == nullptr) return {};

            current = next;
        }

        if (!current->is_final()) return {};

        auto it = state_annotations_.find(current);
        if (it == state_annotations_.end()) return {};

        return it->second->lexemes;
    }

    /**
     * lookup: Find first lexeme for a given stem string (backward compat)
     */
    std::shared_ptr<Lexeme> lookup(const std::string& stem) const {
        auto all = lookup_all(stem);
        return all.empty() ? nullptr : all[0];
    }

    /**
     * all_lexemes: Return all lexemes in the lexicon (for iteration)
     */
    const std::vector<std::shared_ptr<Lexeme>>& all_lexemes() const {
        return lexemes_;
    }

    /**
     * num_states / num_transitions: Statistics accessors
     */
    size_t num_states() const { return states_.size(); }
    size_t num_transitions() const { return transitions_.size(); }
    
    /**
     * get_annotation: Retrieve metadata for a state
     * 
     * Used during morphological analysis when traversing the lexicon FSA.
     * Allows checking properties (POS, stem class) of intermediate states.
     * 
     * @param state  The state to query
     * @return       Shared pointer to LexiconState metadata, or nullptr if none
     */
    std::shared_ptr<LexiconState> get_annotation(State* state) const {
        auto it = state_annotations_.find(state);
        return (it != state_annotations_.end()) ? it->second : nullptr;
    }
    
    // ========== Statistics ==========
    
    /**
     * num_lexemes: Get the number of lexemes in the lexicon
     * 
     * @return Count of lexemes (= count of terminal states)
     */
    size_t num_lexemes() const { return lexemes_.size(); }
    
    /**
     * print_lexemes: Display all lexemes (debugging utility)
     * 
     * Iterates through stored lexemes and prints each one.
     * Useful for verifying lexicon contents.
     */
    void print_lexemes() const {
        std::cout << "Lexicon contains " << lexemes_.size() << " lexemes:" << std::endl;
        for (const auto& lex : lexemes_) {
            std::cout << "  " << lex->to_string() << std::endl;
        }
    }
};

#endif // LEXICON_FSA_H