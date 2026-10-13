#ifndef CLOSED_CLASS_H
#define CLOSED_CLASS_H

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

/**
 * closed_class.h: The word list --derived-first never splits
 *
 * A closed-class word has no morphology by definition, so a decomposition of
 * one is always wrong whatever the lexicon says a substring of it is. "is" is
 * not "i" plus a plural and "his" is not "hi" plus a plural, but "i" and "ha"
 * really are noun lexemes in Wiktionary, so a decomposition exists, and
 * --derived-first used to promote it: the two most frequent words in the
 * corpus came apart into a letter and a suffix.
 *
 * `data/closed_class.tsv` already lists the 324 words, and loading it as a
 * lexicon gives each of them a FUNCTION lexeme that no rule matches. That is
 * not enough on its own for two reasons. It is opt-in, so an evaluation that
 * does not pass --lexicon (the four settings in docs/BASELINES.md do not) gets
 * no protection at all. And even when it is loaded, its weight of 1.3 only
 * wins on the default ranking; --derived-first puts the decomposition first
 * before any weight is consulted.
 *
 * So the same file is also read here, independently of --lexicon, purely as a
 * list of surface strings the flag's re-ranking declines to promote. Only the
 * first column is read, and the rest of the row is ignored: this is a set of
 * words, not a lexicon, and nothing in it reaches the analyzer. Loading it
 * cannot withdraw an analysis or change the default ranking, both of which the
 * output contract fixes in place.
 */
class ClosedClassSet {
private:
    std::unordered_set<std::string> words_;

    static std::string lower(const std::string& s) {
        std::string out = s;
        for (char& c : out) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return out;
    }

public:
    /** DEFAULT_PATH: read relative to the working directory, as the lexicon is */
    static const char* default_path() { return "data/closed_class.tsv"; }

    /**
     * load: Read the first column of a TSV into the set
     *
     * Comments and blank lines are skipped, as in LexiconLoader. A file that
     * cannot be opened leaves the set as it was and returns false; the caller
     * decides whether that is worth a notice, since the guard degrades to its
     * other three conditions rather than failing.
     *
     * @param path  A TSV whose first whitespace-separated field is the word
     * @return      true when the file was opened
     */
    bool load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            std::string word;
            if (!(ss >> word)) continue;
            words_.insert(lower(word));
        }
        return true;
    }

    /** add: Put one word in the set, for tests and for callers with a list */
    void add(const std::string& word) { words_.insert(lower(word)); }

    /** contains: Is this word closed class? The argument is lowercased here. */
    bool contains(const std::string& word) const {
        return words_.find(lower(word)) != words_.end();
    }

    size_t size() const { return words_.size(); }
    bool empty() const { return words_.empty(); }
};

#endif // CLOSED_CLASS_H
