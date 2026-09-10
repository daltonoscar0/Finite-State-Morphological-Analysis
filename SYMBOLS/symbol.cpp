/**
 * symbol.cpp: Implementation of SymbolTable
 * 
 * This file implements the symbol registry for the morphological analyzer.
 * See symbol.h for interface documentation and design rationale.
 */

#include "symbol.h"

/**
 * SymbolTable Constructor
 * 
 * Initialization:
 *   1. Set next_id_ to 0
 *   2. Create epsilon symbol with ID 0
 *   3. Register epsilon in both lookup maps
 *   4. Increment next_id_ to 1 (epsilon consumed ID 0)
 * 
 * Post-condition:
 *   - epsilon_ points to Symbol(0, EPSILON, "ε")
 *   - repr_to_symbol_["ε"] -> epsilon symbol
 *   - id_to_symbol_[0] -> epsilon symbol
 *   - next_id_ == 1
 *   - System ready to create additional symbols
 * 
 * Invariant:
 *   Epsilon is ALWAYS symbol 0 throughout the system's lifetime.
 */
SymbolTable::SymbolTable() : next_id_(0) {
    // Create epsilon as symbol 0
    // Epsilon is special: it represents the empty string (no character)
    epsilon_ = std::make_shared<Symbol>(next_id_++, SymbolType::EPSILON, "ε");
    
    // Register epsilon in both lookup maps
    repr_to_symbol_["ε"] = epsilon_;
    id_to_symbol_[epsilon_->id()] = epsilon_;
    
    // next_id_ is now 1, ready for first real symbol
}

/**
 * get_or_create: Get existing symbol or create new one
 * 
 * This is the factory method for symbols. It ensures symbol uniqueness
 * by checking the registry before creating.
 * 
 * Algorithm:
 *   1. Check if repr already exists in repr_to_symbol_
 *   2. If found: return existing Symbol (idempotent)
 *   3. If not found:
 *      a. Create new Symbol with unique ID (next_id_)
 *      b. Increment next_id_ for next symbol
 *      c. Register in both maps (repr->symbol, id->symbol)
 *      d. Return new Symbol
 * 
 * Complexity:
 *   O(1) expected (hash map lookup/insert)
 * 
 * Example Trace:
 *   First call: get_or_create("a", SURFACE)
 *     - repr_to_symbol_.find("a") -> not found
 *     - Create Symbol(1, SURFACE, "a")
 *     - repr_to_symbol_["a"] = Symbol(1, ...)
 *     - id_to_symbol_[1] = Symbol(1, ...)
 *     - next_id_ becomes 2
 *     - Return Symbol(1, ...)
 *   
 *   Second call: get_or_create("a", SURFACE)
 *     - repr_to_symbol_.find("a") -> found!
 *     - Return existing Symbol(1, ...)  (same object)
 * 
 * @param repr  String representation (e.g., "a", "+PL", "ε")
 * @param type  Symbol type (SURFACE or LEXICAL)
 * @return      Shared pointer to Symbol (either existing or newly created)
 */
std::shared_ptr<Symbol> SymbolTable::get_or_create(const std::string& repr, SymbolType type) {
    // Check if symbol already exists
    auto it = repr_to_symbol_.find(repr);
    if (it != repr_to_symbol_.end()) {
        // Found existing symbol - return it (idempotent)
        return it->second;
    }
    
    // Not found - create new symbol with unique ID
    auto symbol = std::make_shared<Symbol>(next_id_++, type, repr);
    
    // Register in both maps for bidirectional lookup
    repr_to_symbol_[repr] = symbol;
    id_to_symbol_[symbol->id()] = symbol;
    
    return symbol;
}

/**
 * lookup (by representation): Find symbol by string
 * 
 * Looks up a symbol in the repr_to_symbol_ map.
 * 
 * Algorithm:
 *   1. Search repr_to_symbol_ for key == repr
 *   2. If found: return Symbol pointer
 *   3. If not found: return nullptr
 * 
 * Complexity:
 *   O(1) expected (hash map lookup)
 * 
 * Use Cases:
 *   - Checking if a symbol exists before creating
 *   - Converting user input (string) to Symbol
 *   - Validation during FST construction
 * 
 * @param repr  String representation to search for
 * @return      Shared pointer to Symbol if found, nullptr otherwise
 * 
 * Example:
 *   auto sym = table.lookup("a");
 *   if (sym) {
 *     // Symbol exists, use it
 *   } else {
 *     // Symbol doesn't exist
 *   }
 */
std::shared_ptr<Symbol> SymbolTable::lookup(const std::string& repr) const {
    auto it = repr_to_symbol_.find(repr);
    return (it != repr_to_symbol_.end()) ? it->second : nullptr;
}

/**
 * lookup (by ID): Find symbol by unique identifier
 * 
 * Looks up a symbol in the id_to_symbol_ map.
 * 
 * Algorithm:
 *   1. Search id_to_symbol_ for key == id
 *   2. If found: return Symbol pointer
 *   3. If not found: return nullptr
 * 
 * Complexity:
 *   O(1) expected (hash map lookup)
 * 
 * Use Cases:
 *   - Reverse lookup during FSA traversal
 *   - Debugging (print symbol for given ID)
 *   - Deserialization (reconstruct symbols from IDs)
 * 
 * @param id  Symbol ID to search for
 * @return    Shared pointer to Symbol if found, nullptr otherwise
 * 
 * Example:
 *   auto sym = table.lookup(5);  // Get symbol with ID 5
 *   if (sym) {
 *     std::cout << "Symbol 5 is: " << sym->repr() << std::endl;
 *   }
 */
std::shared_ptr<Symbol> SymbolTable::lookup(int id) const {
    auto it = id_to_symbol_.find(id);
    return (it != id_to_symbol_.end()) ? it->second : nullptr;
}

/**
 * has_symbol (by representation): Check existence by string
 * 
 * Convenience method that checks if a symbol with given representation exists.
 * Equivalent to: lookup(repr) != nullptr
 * 
 * Complexity:
 *   O(1) expected (hash map lookup)
 * 
 * @param repr  String representation to check
 * @return      true if symbol exists, false otherwise
 * 
 * Example:
 *   if (table.has_symbol("a")) {
 *     // Symbol "a" is in the table
 *   }
 */
bool SymbolTable::has_symbol(const std::string& repr) const {
    return repr_to_symbol_.find(repr) != repr_to_symbol_.end();
}

/**
 * has_symbol (by ID): Check existence by ID
 * 
 * Convenience method that checks if a symbol with given ID exists.
 * Equivalent to: lookup(id) != nullptr
 * 
 * Complexity:
 *   O(1) expected (hash map lookup)
 * 
 * @param id  Symbol ID to check
 * @return    true if symbol exists, false otherwise
 * 
 * Example:
 *   if (table.has_symbol(0)) {
 *     // Symbol 0 exists (always true - it's epsilon)
 *   }
 */
bool SymbolTable::has_symbol(int id) const {
    return id_to_symbol_.find(id) != id_to_symbol_.end();
}