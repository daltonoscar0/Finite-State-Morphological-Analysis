#ifndef SYMBOL_H
#define SYMBOL_H

#include <string>
#include <unordered_map>
#include <memory>

/**
 * Symbol: Abstract token in finite-state computation
 * 
 * Fundamental Concept:
 *   In finite-state morphology, symbols are NOT raw characters.
 *   They are abstract tokens with unique identities and semantic types.
 * 
 * Why Abstract Symbols?
 *   1. Type Safety: Surface 'a' ≠ Lexical 'a' (different semantic roles)
 *   2. Unique Identity: Symbol equality based on ID, not string
 *   3. Epsilon Handling: Special empty string symbol (cannot be a char)
 *   4. Extensibility: Can represent multi-character units (digraphs, features)
 * 
 * Three Symbol Types:
 *   
 *   1. SURFACE - Orthographic symbols (concrete):
 *      - Individual letters: 'a', 'b', 'c', ...
 *      - Visible in actual written words
 *      - Example: "cats" = [c, a, t, s]
 *   
 *   2. LEXICAL - Abstract grammatical symbols:
 *      - Features: +PL, +PAST, +3SG, +DEF, ...
 *      - Part-of-speech: +N, +V, +ADJ, ...
 *      - Boundaries: #, ^, $, ...
 *      - Never visible in surface forms
 *      - Example: cat +N +PL
 *   
 *   3. EPSILON - Empty string (ε):
 *      - Special symbol representing nothing
 *      - Used for: deletions, insertions, spontaneous transitions
 *      - Has unique ID (typically 0)
 *      - Not the same as empty string ""
 * 
 * Symbol Identity:
 *   Symbols are compared by ID, not by string representation.
 *   
 *   Example:
 *     Symbol s1(5, SURFACE, "a")
 *     Symbol s2(5, SURFACE, "a")  // Same symbol (ID 5)
 *     Symbol s3(7, SURFACE, "a")  // Different symbol (ID 7)
 *     
 *     s1 == s2  -> true  (same ID)
 *     s1 == s3  -> false (different ID)
 * 
 * Design Pattern:
 *   This follows the "flyweight pattern" - symbols are created once
 *   and shared throughout the system via shared_ptr.
 */
enum class SymbolType {
    EPSILON,    // ε - empty string (no orthographic realization)
    SURFACE,    // Orthographic characters appearing in written text
    LEXICAL     // Abstract features, POS tags, boundaries
};

/**
 * Symbol: Individual symbol object
 * 
 * Immutable value object representing a single symbol in the system.
 */
class Symbol {
private:
    int id_;                // Unique identifier (assigned by SymbolTable)
    SymbolType type_;       // Semantic category of this symbol
    std::string repr_;      // Human-readable representation (for display/debug)
    
public:
    /**
     * Constructor
     * 
     * Symbols should only be created via SymbolTable to ensure uniqueness.
     * 
     * @param id    Unique integer ID
     * @param type  Symbol type (EPSILON, SURFACE, or LEXICAL)
     * @param repr  String representation (e.g., "a", "+PL", "ε")
     */
    Symbol(int id, SymbolType type, const std::string& repr)
        : id_(id), type_(type), repr_(repr) {}
    
    // ========== Accessors ==========
    
    /**
     * Get the unique ID of this symbol
     * 
     * ID is used for:
     *   - Equality comparison (two symbols equal iff same ID)
     *   - Hash keys in maps/sets
     *   - Compact representation in FSA transition tables
     */
    int id() const { return id_; }
    
    /**
     * Get the semantic type of this symbol
     */
    SymbolType type() const { return type_; }
    
    /**
     * Get the human-readable representation
     * 
     * Used for:
     *   - Debugging output
     *   - Logging
     *   - User-facing display
     * 
     * Note: Two symbols can have same repr but different IDs/types
     */
    const std::string& repr() const { return repr_; }
    
    // ========== Type Queries ==========
    
    /**
     * Check if this is the epsilon (empty string) symbol
     * 
     * Epsilon transitions in FSAs/FSTs can be taken without consuming input.
     * 
     * @return true if type is EPSILON
     */
    bool is_epsilon() const { return type_ == SymbolType::EPSILON; }
    
    /**
     * Check if this is a surface (orthographic) symbol
     * 
     * BUG FIX: Original had colon `:` instead of semicolon `;`
     * 
     * @return true if type is SURFACE
     */
    bool is_surface() const { return type_ == SymbolType::SURFACE; }
    
    /**
     * Check if this is a lexical (abstract) symbol
     * 
     * @return true if type is LEXICAL
     */
    bool is_lexical() const { return type_ == SymbolType::LEXICAL; }
    
    // ========== Equality Operators ==========
    
    /**
     * Equality: Symbols are equal if they have the same ID
     * 
     * This is ID-based equality, not value-based.
     * Two symbols with same repr but different IDs are NOT equal.
     * 
     * Rationale:
     *   - Surface "a" and Lexical "a" should be different symbols
     *   - Enables type-safe symbol comparisons
     * 
     * @param other  Symbol to compare against
     * @return       true if IDs match
     */
    bool operator==(const Symbol& other) const {
        return id_ == other.id_;
    }
    
    /**
     * Inequality operator (inverse of ==)
     */
    bool operator!=(const Symbol& other) const {
        return !(*this == other);
    }
};

/**
 * SymbolTable: Central registry for all symbols in the system
 * 
 * Purpose:
 *   Manages the global symbol inventory, ensuring:
 *     1. Uniqueness: Each (repr, type) pair gets exactly one Symbol
 *     2. Bidirectional lookup: By representation OR by ID
 *     3. ID allocation: Monotonically increasing IDs
 *     4. Epsilon management: Special epsilon symbol always available
 * 
 * Design Pattern:
 *   This is a "registry" or "symbol table" pattern (like in compilers).
 *   All parts of the system share a single SymbolTable instance.
 * 
 * Guarantees:
 *   - get_or_create("a", SURFACE) always returns the same Symbol
 *   - Symbol IDs are unique across all types
 *   - Epsilon symbol always has ID 0
 * 
 * Thread Safety:
 *   Not thread-safe. Assume single-threaded access or external synchronization.
 * 
 * Lifetime:
 *   - SymbolTable should outlive all FSAs/FSTs that reference its symbols
 *   - Typically created at program start, destroyed at program end
 *   - Symbols are owned by SymbolTable (via maps)
 */
class SymbolTable {
private:
    // Bidirectional maps for symbol lookup
    
    // Map: string representation -> Symbol
    // Key: "a", "+PL", "ε", etc.
    // Enables lookup by name: lookup("a") -> Symbol
    std::unordered_map<std::string, std::shared_ptr<Symbol>> repr_to_symbol_;
    
    // Map: unique ID -> Symbol
    // Key: 0, 1, 2, 3, ...
    // Enables lookup by ID: lookup(5) -> Symbol
    std::unordered_map<int, std::shared_ptr<Symbol>> id_to_symbol_;
    
    // Next available symbol ID (monotonically increasing)
    int next_id_;
    
    // Special epsilon symbol (always ID 0)
    // Kept separate for fast access without lookup
    std::shared_ptr<Symbol> epsilon_;
    
public:
    /**
     * Constructor
     * 
     * Initializes symbol table with epsilon as symbol 0.
     * Epsilon is created automatically and always available.
     */
    SymbolTable();
    
    // ========== Symbol Creation ==========
    
    /**
     * get_or_create: Get existing symbol or create new one
     * 
     * This is the primary method for obtaining symbols.
     * 
     * Idempotent:
     *   Multiple calls with same (repr, type) return the same Symbol.
     * 
     * Process:
     *   1. Check if (repr, type) already exists in repr_to_symbol_
     *   2. If yes: return existing Symbol
     *   3. If no: create new Symbol with unique ID, store, return
     * 
     * Note on Types:
     *   ("a", SURFACE) and ("a", LEXICAL) are DIFFERENT symbols.
     *   They have different IDs and are distinct entities.
     * 
     * @param repr  String representation (e.g., "a", "+PL")
     * @param type  Symbol type (SURFACE or LEXICAL, not EPSILON)
     * @return      Shared pointer to Symbol (new or existing)
     * 
     * Example:
     *   auto s1 = table.get_or_create("a", SURFACE);  // Creates, ID=1
     *   auto s2 = table.get_or_create("a", SURFACE);  // Returns existing, ID=1
     *   s1 == s2  -> true (same Symbol object)
     *   
     *   auto s3 = table.get_or_create("a", LEXICAL);  // Creates new, ID=2
     *   s1 == s3  -> false (different types, different symbols)
     */
    std::shared_ptr<Symbol> get_or_create(const std::string& repr, SymbolType type);
    
    // ========== Lookup ==========
    
    /**
     * lookup: Find symbol by string representation
     * 
     * @param repr  String to look up
     * @return      Pointer to Symbol if found, nullptr otherwise
     * 
     * Note: This doesn't distinguish types. If you need type-specific
     *       lookup, check the returned symbol's type.
     */
    std::shared_ptr<Symbol> lookup(const std::string& repr) const;
    
    /**
     * lookup: Find symbol by unique ID
     * 
     * @param id  Symbol ID to look up
     * @return    Pointer to Symbol if found, nullptr otherwise
     */
    std::shared_ptr<Symbol> lookup(int id) const;
    
    // ========== Special Symbols ==========
    
    /**
     * epsilon: Get the epsilon (empty string) symbol
     * 
     * Epsilon is always available and always has ID 0.
     * This is a fast accessor (no map lookup needed).
     * 
     * @return Shared pointer to the epsilon Symbol
     */
    std::shared_ptr<Symbol> epsilon() const { return epsilon_; }
    
    // ========== Existence Checks ==========
    
    /**
     * has_symbol: Check if a symbol with given representation exists
     * 
     * @param repr  String representation to check
     * @return      true if symbol exists, false otherwise
     */
    bool has_symbol(const std::string& repr) const;
    
    /**
     * has_symbol: Check if a symbol with given ID exists
     * 
     * @param id  Symbol ID to check
     * @return    true if symbol exists, false otherwise
     */
    bool has_symbol(int id) const;
};

#endif // SYMBOL_H