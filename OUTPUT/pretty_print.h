#ifndef PRETTY_PRINT_H
#define PRETTY_PRINT_H

#include "../ANALYSIS/analysis.h"
#include <iostream>
#include <vector>

/**
 * PrettyPrinter: Human-readable output formatting for morphological analyses
 * 
 * Purpose:
 *   Provides clean, linguistically-appropriate formatting of analysis results.
 *   Separates presentation logic from analysis logic (single responsibility).
 * 
 * Design Principles:
 *   1. NO analysis logic here - purely formatting/display
 *   2. Multiple output formats for different use cases
 *   3. Handles ambiguity gracefully (multiple analyses)
 *   4. Uses standard linguistic notation
 * 
 * Separation of Concerns:
 *   - Analyzer: Computes morphological analyses
 *   - Analysis: Stores analysis data structures
 *   - PrettyPrinter: Presents analyses to humans
 * 
 * Output Formats:
 *   This class provides three output styles:
 *     1. Standard: Compact one-line format for single analyses
 *     2. Detailed: Numbered alternatives with metadata
 *     3. Compact: Tab-separated for batch processing / piping
 */
class PrettyPrinter {
public:
    /**
     * print_analyses: Standard output format
     * 
     * Format Rules:
     *   - No analyses: "word -> (no analysis)"
     *   - Single analysis: "word -> stem +POS +FEATURE..."
     *   - Multiple analyses: "word ->" followed by indented alternatives
     * 
     * Linguistic Notation:
     *   Uses the Leipzig Glossing Rules convention:
     *     - Stem in lowercase
     *     - POS and features prefixed with '+'
     *     - Space-separated components
     * 
     * Examples:
     *   Single:
     *     cats -> cat +N +PL
     *   
     *   Multiple (ambiguous):
     *     flies ->
     *       fly +N +PL
     *       fly +V +3SG
     *   
     *   No analysis:
     *     xyzzy -> (no analysis)
     * 
     * Use Case:
     *   Interactive mode where users type words and see immediate analyses
     * 
     * @param word       The original input word
     * @param analyses   Vector of Analysis objects (may be empty)
     */
    static void print_analyses(const std::string& word, 
                              const std::vector<Analysis>& analyses) {
        if (analyses.empty()) {
            // No valid morphological analysis found
            std::cout << word << " → (no analysis)" << std::endl;
            return;
        }
        
        if (analyses.size() == 1) {
            // Unambiguous: single analysis
            std::cout << word << " → " << analyses[0].to_string() << std::endl;
        } else {
            // Ambiguous: multiple possible analyses
            std::cout << word << " →" << std::endl;
            for (const auto& analysis : analyses) {
                std::cout << "  " << analysis.to_string() << std::endl;
            }
        }
    }
    
    /**
     * print_detailed: Verbose output with numbered alternatives
     * 
     * Format:
     *   Analysis of: word
     *   Found N reading(s):
     *     [1] stem +POS +FEATURE...
     *     [2] stem +POS +FEATURE...
     * 
     * Terminology:
     *   "Reading" is linguistic jargon for "interpretation" or "analysis"
     *   Borrowed from semantics where words have multiple readings
     * 
     * Examples:
     *   Analysis of: flies
     *   Found 2 reading(s):
     *     [1] fly +N +PL
     *     [2] fly +V +3SG
     * 
     *   Analysis of: elephant
     *   Found 0 reading(s):
     *     (no analysis found)
     * 
     * Use Case:
     *   - Debugging: See all alternatives clearly numbered
     *   - Linguistics research: Document ambiguity patterns
     *   - Teaching: Explain multiple interpretations
     * 
     * @param word       The input word being analyzed
     * @param analyses   Vector of Analysis objects
     */
    static void print_detailed(const std::string& word,
                              const std::vector<Analysis>& analyses) {
        std::cout << "Analysis of: " << word << std::endl;
        std::cout << "Found " << analyses.size() << " reading(s):" << std::endl;
        
        if (analyses.empty()) {
            std::cout << "  (no analysis found)" << std::endl;
            return;
        }
        
        // Print numbered alternatives
        for (size_t i = 0; i < analyses.size(); i++) {
            std::cout << "  [" << (i + 1) << "] " 
                     << analyses[i].to_string() << std::endl;
        }
    }
    
    /**
     * print_compact: Tab-separated format for batch processing
     * 
     * Format:
     *   word\tanalysis1\tanalysis2\t...
     *   
     *   Or if no analysis:
     *   word\tNO_ANALYSIS
     * 
     * Rationale:
     *   - Machine-readable format
     *   - Easy to parse with scripts (awk, cut, etc.)
     *   - One line per word (facilitates grep, sort, etc.)
     *   - Tab-separated values (TSV) standard
     * 
     * Examples:
     *   cats	cat +N +PL
     *   flies	fly +N +PL	fly +V +3SG
     *   elephant	NO_ANALYSIS
     * 
     * Use Cases:
     *   - Batch processing: Analyze entire corpora
     *   - Piping: ./analyzer < wordlist.txt | process.py
     *   - Importing: Load into spreadsheets, databases
     *   - Testing: Compare expected vs actual outputs
     * 
     * Shell Example:
     *   echo -e "cats\ndogs\nflies" | ./analyzer --compact | \
     *     awk -F'\t' '{print $1, "has", NF-1, "analyses"}'
     * 
     *   Output:
     *     cats has 1 analyses
     *     dogs has 1 analyses
     *     flies has 2 analyses
     * 
     * @param word       The input word
     * @param analyses   Vector of Analysis objects
     */
    static void print_compact(const std::string& word,
                            const std::vector<Analysis>& analyses) {
        std::cout << word;
        
        if (analyses.empty()) {
            std::cout << "\tNO_ANALYSIS";
        } else {
            // Output each analysis as a separate tab-delimited field
            for (const auto& analysis : analyses) {
                std::cout << "\t" << analysis.to_string();
            }
        }
        
        std::cout << std::endl;
    }
};

#endif // PRETTY_PRINT_H