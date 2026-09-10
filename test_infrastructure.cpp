/**
 * test_infrastructure.cpp: Unit tests for core FSA/FST infrastructure
 * 
 * Purpose:
 *   Tests the fundamental building blocks of the morphological analyzer:
 *     - Symbol system (SymbolTable, Symbol)
 *     - Finite-state acceptors (FSA)
 *     - Finite-state transducers (FST)
 *     - Epsilon transitions
 * 
 * These tests verify that the automata machinery works correctly
 * before we build higher-level morphological components on top.
 * 
 * Compilation:
 *   g++ -std=c++17 -I. test_infrastructure.cpp SYMBOLS/symbol.cpp -o test_infra
 * 
 * Usage:
 *   ./test_infra
 * 
 * Expected Output:
 *   All tests should pass with  marks
 */

#include "SYMBOLS/symbol.h"
#include "AUTOMATA/automaton.h"
#include "AUTOMATA/fsa.h"
#include "AUTOMATA/fst.h"
#include <iostream>
#include <cassert>

/**
 * test_symbol_table: Verify SymbolTable functionality
 * 
 * Tests:
 *   1. Epsilon symbol is created automatically
 *   2. Surface symbols can be created and compared
 *   3. Lexical symbols can be created
 *   4. Symbol uniqueness is enforced (same repr -> same Symbol*)
 * 
 * Coverage:
 *   - SymbolTable constructor
 *   - epsilon() accessor
 *   - get_or_create() factory method
 *   - Symbol type queries (is_epsilon, is_surface, is_lexical)
 *   - Symbol equality operators
 */
void test_symbol_table() {
    std::cout << "Testing SymbolTable..." << std::endl;
    
    SymbolTable symbols;
    
    // Test 1: Epsilon symbol
    auto eps = symbols.epsilon();
    assert(eps->is_epsilon());
    std::cout << "  ✓ Epsilon symbol created" << std::endl;
    
    // Test 2: Surface symbols
    auto sym_a = symbols.get_or_create("a", SymbolType::SURFACE);
    auto sym_b = symbols.get_or_create("b", SymbolType::SURFACE);
    assert(sym_a->is_surface());
    assert(*sym_a != *sym_b);  // Different symbols
    std::cout << "  ✓ Surface symbols created and comparable" << std::endl;
    
    // Test 3: Lexical symbols
    auto pl = symbols.get_or_create("+PL", SymbolType::LEXICAL);
    auto n = symbols.get_or_create("+N", SymbolType::LEXICAL);
    assert(pl->is_lexical());
    assert(*pl != *n);  // Different symbols
    std::cout << "  ✓ Lexical symbols created" << std::endl;
    
    // Test 4: Uniqueness (idempotent creation)
    auto sym_a2 = symbols.get_or_create("a", SymbolType::SURFACE);
    assert(sym_a == sym_a2);  // Same pointer (not just equal, but identical)
    std::cout << "  ✓ Symbol uniqueness enforced" << std::endl;
    
    std::cout << "SymbolTable tests passed!\n" << std::endl;
}

/**
 * test_fsa: Verify FSA (Finite-State Acceptor) functionality
 * 
 * Builds a simple FSA and tests acceptance:
 * 
 * FSA Structure:
 *   State 0 (start) --a--> State 1 --b--> State 2 (final)
 * 
 * Language:
 *   L(FSA) = {"ab"}  (exactly one string)
 * 
 * Tests:
 *   - FSA accepts "ab" (valid string)
 *   - FSA rejects "ac" (wrong character)
 *   - FSA rejects "a" (incomplete string)
 * 
 * Coverage:
 *   - FSA construction (create_state, set_start_state, create_transition)
 *   - FSA.accepts() method
 *   - Deterministic traversal
 */
void test_fsa() {
    std::cout << "Testing FSA..." << std::endl;
    
    SymbolTable symbols;
    FSA fsa(&symbols);
    
    // Build simple FSA: accepts "ab"
    auto s0 = fsa.create_state(false);  // Start, non-final
    auto s1 = fsa.create_state(false);  // Intermediate, non-final
    auto s2 = fsa.create_state(true);   // End, final/accepting
    
    fsa.set_start_state(s0);
    
    auto sym_a = symbols.get_or_create("a", SymbolType::SURFACE);
    auto sym_b = symbols.get_or_create("b", SymbolType::SURFACE);
    auto sym_c = symbols.get_or_create("c", SymbolType::SURFACE);
    
    fsa.create_transition(s0, s1, sym_a);  // 0 --a--> 1
    fsa.create_transition(s1, s2, sym_b);  // 1 --b--> 2
    
    std::cout << "  Created FSA with " << fsa.num_states() << " states, "
              << fsa.num_transitions() << " transitions" << std::endl;
    
    // Test acceptance
    std::vector<std::shared_ptr<Symbol>> input_ab = {sym_a, sym_b};
    std::vector<std::shared_ptr<Symbol>> input_ac = {sym_a, sym_c};
    std::vector<std::shared_ptr<Symbol>> input_a = {sym_a};
    
    assert(fsa.accepts(input_ab));   // Should accept "ab"
    std::cout << "  ✓ Accepts 'ab'" << std::endl;
    
    assert(!fsa.accepts(input_ac));  // Should reject "ac" (wrong char)
    std::cout << "  ✓ Rejects 'ac'" << std::endl;
    
    assert(!fsa.accepts(input_a));   // Should reject "a" (incomplete)
    std::cout << "  ✓ Rejects incomplete 'a'" << std::endl;
    
    std::cout << "FSA tests passed!\n" << std::endl;
}

/**
 * test_fst: Verify FST (Finite-State Transducer) basic functionality
 * 
 * Builds a simple transducer and tests transduction:
 * 
 * FST Structure:
 *   State 0 (start) --a:b--> State 1 (final)
 * 
 * Relation:
 *   R(FST) = {("a", "b")}  (maps "a" to "b")
 * 
 * Tests:
 *   - FST transduces "a" -> "b"
 *   - Result has correct structure
 *   - Output matches expected symbol
 * 
 * Coverage:
 *   - FST construction (create_fst_state, create_fst_transition)
 *   - FST.transduce() method
 *   - FSTTransition with input:output pairs
 */
void test_fst() {
    std::cout << "Testing FST..." << std::endl;
    
    SymbolTable symbols;
    FST fst(&symbols);
    
    // Build simple FST: maps "a" -> "b"
    auto s0 = fst.create_fst_state(false);  // Start
    auto s1 = fst.create_fst_state(true);   // Final
    
    fst.set_start_state(s0);
    
    auto sym_a = symbols.get_or_create("a", SymbolType::SURFACE);
    auto sym_b = symbols.get_or_create("b", SymbolType::SURFACE);
    
    fst.create_fst_transition(s0, s1, sym_a, sym_b);  // a:b
    
    std::cout << "  Created FST with " << fst.num_states() << " states" << std::endl;
    
    // Test transduction
    std::vector<std::shared_ptr<Symbol>> input = {sym_a};
    auto results = fst.transduce(input);
    
    assert(results.size() == 1);           // One transduction result
    assert(results[0].success);             // Transduction succeeded
    assert(results[0].output.size() == 1);  // Output has one symbol
    assert(*results[0].output[0] == *sym_b); // Output is 'b'
    
    std::cout << "  ✓ Transduces 'a' -> 'b'" << std::endl;
    
    std::cout << "FST tests passed!\n" << std::endl;
}

/**
 * test_fst_with_epsilon: Verify epsilon transitions in FSTs
 * 
 * Builds an FST with epsilon transition (insertion):
 * 
 * FST Structure:
 *   State 0 (start) --a:a--> State 1 --ε:b--> State 2 (final)
 * 
 * Relation:
 *   R(FST) = {("a", "ab")}  (inserts 'b' after 'a')
 * 
 * Key Concept:
 *   The ε:b transition writes 'b' WITHOUT consuming input.
 *   This demonstrates insertion in finite-state morphology.
 * 
 * Tests:
 *   - FST transduces "a" -> "ab"
 *   - Output contains both 'a' and 'b' in correct order
 *   - Epsilon transition doesn't consume input
 * 
 * Coverage:
 *   - Epsilon symbols in FSTs
 *   - Multi-symbol output
 *   - Insertion operations (ε:x)
 */
void test_fst_with_epsilon() {
    std::cout << "Testing FST with epsilon transitions..." << std::endl;
    
    SymbolTable symbols;
    FST fst(&symbols);
    
    // Build FST: maps "a" -> "ab" (insertion)
    auto s0 = fst.create_fst_state(false);  // Start
    auto s1 = fst.create_fst_state(false);  // Intermediate
    auto s2 = fst.create_fst_state(true);   // Final
    
    fst.set_start_state(s0);
    
    auto sym_a = symbols.get_or_create("a", SymbolType::SURFACE);
    auto sym_b = symbols.get_or_create("b", SymbolType::SURFACE);
    auto eps = symbols.epsilon();
    
    fst.create_fst_transition(s0, s1, sym_a, sym_a);  // a:a (identity)
    fst.create_fst_transition(s1, s2, eps, sym_b);    // ε:b (insertion)
    
    // Test transduction
    std::vector<std::shared_ptr<Symbol>> input = {sym_a};
    auto results = fst.transduce(input);
    
    assert(results.size() == 1);              // One result
    assert(results[0].output.size() == 2);    // Output: "ab" (2 symbols)
    assert(*results[0].output[0] == *sym_a);  // First symbol is 'a'
    assert(*results[0].output[1] == *sym_b);  // Second symbol is 'b'
    
    std::cout << "  ✓ Transduces 'a' -> 'ab' with epsilon insertion" << std::endl;
    
    std::cout << "FST epsilon tests passed!\n" << std::endl;
}

/**
 * main: Test runner
 * 
 * Runs all infrastructure tests in sequence.
 * If any test fails (assertion), program terminates with error.
 * 
 * @return 0 if all tests pass, 1 if any test fails
 */
int main() {
    std::cout << "\n=== Testing Morphological Analyzer Infrastructure ===\n" << std::endl;
    
    try {
        test_symbol_table();
        test_fsa();
        test_fst();
        test_fst_with_epsilon();
        
        std::cout << "=== All tests passed! ===\n" << std::endl;
        return 0;
    } 
    catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}