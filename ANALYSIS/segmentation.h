#ifndef SEGMENTATION_H
#define SEGMENTATION_H

#include "analysis.h"
#include "../LEXICON/lexicon_fsa.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/**
 * segmentation.h: Surface-piece output for the morphological analyzer
 *
 * The analyzer's native output is a LEXICAL analysis: "cities" -> city +N +PL.
 * That string cannot be concatenated back into the input, so tokenizer metrics
 * (compression, fertility, round-trip exactness, boundary F1) cannot be
 * computed on it.
 *
 * This header adds a second view of the same analysis: a list of SURFACE
 * PIECES, substrings of the original input that concatenate back to it byte
 * for byte, with the linguistic analysis carried alongside as annotation.
 *
 * Segmentation convention:
 *   The stem piece is the longest prefix of the surface word (after any
 *   prefix piece) that is shared with the lemma. Everything after it belongs
 *   to the following suffix piece, allomorph included:
 *
 *     stopped -> stop | ped      (lemma "stop",  shared prefix "stop")
 *     hoping  -> hop  | ing      (lemma "hope",  shared prefix "hop")
 *     cities  -> cit  | ies      (lemma "city",  shared prefix "cit")
 *     kisses  -> kiss | es       (lemma "kiss",  shared prefix "kiss")
 *     walked  -> walk | ed       (lemma "walk",  shared prefix "walk")
 *     dark    -> dark | ness     (lemma "dark",  shared prefix "dark")
 *     unkind  -> un   | kind     (prefix "un", then the stem)
 *
 *   Irregular and suppletive forms are kept as a single piece: went, mice,
 *   sheep, children, oxen, better. The orthographic alternation involved, if
 *   any, is reported separately in the `alt` field so a scorer can tolerate
 *   gold standards that place the boundary one character away.
 *
 * Nothing here changes the lexical analysis path. A Segmentation is derived
 * from an Analysis that the analyzer already produced; the Analysis itself is
 * read only.
 */

// ── Piece labels ───────────────────────────────────────────────────────────

namespace piece_label {
inline const char* STEM        = "STEM";
inline const char* PREFIX      = "PREFIX";
inline const char* SUFFIX_INFL = "SUFFIX_INFL";
inline const char* SUFFIX_PL   = "SUFFIX_PL";
inline const char* SUFFIX_PAST = "SUFFIX_PAST";
inline const char* SUFFIX_ING  = "SUFFIX_ING";
inline const char* SUFFIX_3SG  = "SUFFIX_3SG";
inline const char* SUFFIX_COMP = "SUFFIX_COMP";
inline const char* SUFFIX_SUP  = "SUFFIX_SUP";
inline const char* SUFFIX_DERIV = "SUFFIX_DERIV";
inline const char* IRREGULAR   = "IRREGULAR";
inline const char* UNK         = "UNK";
}  // namespace piece_label

// ── Source and gate values ─────────────────────────────────────────────────

namespace seg_source {
inline const char* FST         = "fst";
inline const char* FALLBACK    = "fallback";
inline const char* PASSTHROUGH = "passthrough";
}  // namespace seg_source

namespace seg_gate {
inline const char* DECOMPOSED   = "decomposed";
inline const char* LEXICON_STEM = "lexicon_stem";
inline const char* IRREGULAR    = "irregular";
inline const char* NO_ANALYSIS  = "no_analysis";
inline const char* PASSTHROUGH  = "passthrough";
}  // namespace seg_gate

namespace seg_alt {
inline const char* NONE               = "";
inline const char* Y_TO_I             = "y_to_i";
inline const char* E_DELETION         = "e_deletion";
inline const char* CONSONANT_DOUBLING = "consonant_doubling";
inline const char* EPENTHESIS         = "epenthesis";
}  // namespace seg_alt

// ── UTF-8 helpers ──────────────────────────────────────────────────────────

/**
 * utf8: Minimal code-point scanning over std::string.
 *
 * Offsets in the output format are code-point boundaries, not byte offsets,
 * so a word like "cafe" with an accented e has character length 4 even though
 * it occupies 5 bytes.
 *
 * Invalid input is not rejected: a byte that cannot start or continue a valid
 * sequence counts as one code point of its own. That keeps offsets well
 * defined for arbitrary byte input, and matches how the JSON writer escapes
 * such bytes (one \u00XX escape each).
 */
namespace utf8 {

/**
 * sequence_length: Length in bytes of the code point starting at `i`
 *
 * @param s      Byte string
 * @param i      Start offset, must be < s.size()
 * @param valid  Set to true if a well-formed sequence was found
 * @return       Byte length of the code point (1 when invalid)
 */
inline size_t sequence_length(const std::string& s, size_t i, bool& valid) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    size_t need = 0;
    uint32_t cp = 0;

    if (c < 0x80)                   { valid = true; return 1; }
    else if ((c & 0xE0) == 0xC0)    { need = 1; cp = c & 0x1Fu; }
    else if ((c & 0xF0) == 0xE0)    { need = 2; cp = c & 0x0Fu; }
    else if ((c & 0xF8) == 0xF0)    { need = 3; cp = c & 0x07u; }
    else                            { valid = false; return 1; }

    if (i + need >= s.size()) {
        // Truncated sequence at end of input
        valid = false;
        return 1;
    }

    for (size_t k = 1; k <= need; k++) {
        const unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) { valid = false; return 1; }
        cp = (cp << 6) | (cc & 0x3Fu);
    }

    // Reject overlong encodings, surrogates, and out-of-range code points
    if ((need == 1 && cp < 0x80) ||
        (need == 2 && cp < 0x800) ||
        (need == 3 && cp < 0x10000) ||
        (cp >= 0xD800 && cp <= 0xDFFF) ||
        cp > 0x10FFFF) {
        valid = false;
        return 1;
    }

    valid = true;
    return need + 1;
}

/** count: Number of code points in `s`, counting each invalid byte as one */
inline size_t count(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        bool valid = false;
        i += sequence_length(s, i, valid);
        n++;
    }
    return n;
}

/** is_valid: true if `s` is entirely well-formed UTF-8 */
inline bool is_valid(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        bool valid = false;
        const size_t len = sequence_length(s, i, valid);
        if (!valid) return false;
        i += len;
    }
    return true;
}

/**
 * snap_to_boundary: Move `byte_off` left until it starts a code point
 *
 * Piece boundaries are computed by comparing a surface word against an ASCII
 * lemma, so in practice they already land on code-point boundaries. This is a
 * guard so that a boundary can never split a multi-byte character even if a
 * non-ASCII lemma ever enters the lexicon.
 */
inline size_t snap_to_boundary(const std::string& s, size_t byte_off) {
    if (byte_off >= s.size()) return s.size();
    while (byte_off > 0) {
        const unsigned char c = static_cast<unsigned char>(s[byte_off]);
        if ((c & 0xC0) != 0x80) break;  // not a continuation byte
        byte_off--;
    }
    return byte_off;
}

}  // namespace utf8

// ── Data model ─────────────────────────────────────────────────────────────

/** SurfacePiece: one substring of the input word, plus its label */
struct SurfacePiece {
    std::string text;
    std::string label;

    SurfacePiece(const std::string& t, const std::string& l)
        : text(t), label(l) {}
};

/**
 * Segmentation: the surface-piece view of one analysis of one word
 *
 * Invariants, enforced by Segmentation::check():
 *   1. The pieces concatenate to `word`, byte for byte, case preserved.
 *   2. offsets.size() == pieces.size() + 1, offsets.front() == 0,
 *      offsets.back() == code-point length of `word`, strictly increasing.
 *   3. labels and pieces have equal length, and no piece is empty.
 *   4. source == "fallback" implies a single UNK piece covering the word.
 *   5. gate "decomposed" implies two or more pieces.
 */
struct Segmentation {
    std::string word;
    std::vector<SurfacePiece> pieces;
    std::vector<size_t> offsets;
    std::string lemma;
    std::string alt    = seg_alt::NONE;
    std::string source = seg_source::FALLBACK;
    std::string gate   = seg_gate::NO_ANALYSIS;
    size_t n_analyses  = 0;
    float weight       = 0.0f;

    /**
     * check: Verify every invariant above
     *
     * @param why  Filled with a short description of the first failure
     * @return     true when the record is well formed
     */
    bool check(std::string& why) const {
        if (offsets.size() != pieces.size() + 1) {
            why = "offsets size != pieces size + 1";
            return false;
        }
        if (offsets.empty() || offsets.front() != 0) {
            why = "offsets must start at 0";
            return false;
        }
        if (offsets.back() != utf8::count(word)) {
            why = "last offset != character length of word";
            return false;
        }
        for (size_t i = 1; i < offsets.size(); i++) {
            if (offsets[i] <= offsets[i - 1]) {
                why = "offsets not strictly increasing";
                return false;
            }
        }

        std::string joined;
        for (const auto& p : pieces) {
            if (p.text.empty()) {
                why = "empty piece";
                return false;
            }
            if (p.label.empty()) {
                why = "empty label";
                return false;
            }
            joined += p.text;
        }
        if (joined != word) {
            why = "pieces do not concatenate to word";
            return false;
        }

        if (source != seg_source::FST &&
            source != seg_source::FALLBACK &&
            source != seg_source::PASSTHROUGH) {
            why = "unknown source";
            return false;
        }
        if (gate != seg_gate::DECOMPOSED &&
            gate != seg_gate::LEXICON_STEM &&
            gate != seg_gate::IRREGULAR &&
            gate != seg_gate::NO_ANALYSIS &&
            gate != seg_gate::PASSTHROUGH) {
            why = "unknown gate";
            return false;
        }
        if (alt != seg_alt::NONE &&
            alt != seg_alt::Y_TO_I &&
            alt != seg_alt::E_DELETION &&
            alt != seg_alt::CONSONANT_DOUBLING &&
            alt != seg_alt::EPENTHESIS) {
            why = "unknown alt";
            return false;
        }

        if (source == seg_source::FALLBACK) {
            if (pieces.size() != 1 || pieces[0].text != word ||
                pieces[0].label != piece_label::UNK ||
                gate != seg_gate::NO_ANALYSIS ||
                !lemma.empty() || weight != 0.0f) {
                why = "fallback record is not in canonical form";
                return false;
            }
        }
        if (gate == seg_gate::DECOMPOSED && pieces.size() < 2) {
            why = "gate decomposed with fewer than two pieces";
            return false;
        }
        return true;
    }
};

// ── Segmenter ──────────────────────────────────────────────────────────────

/**
 * Segmenter: turns an Analysis into a Segmentation
 *
 * The analyzer lowercases its input and reports lemmas in lowercase, so piece
 * boundaries are located on a lowercased copy of the word and then applied as
 * slices of the ORIGINAL input. Case is therefore preserved exactly:
 *   "Cats" -> ["Cat", "s"], "CATS" -> ["CAT", "S"].
 */
class Segmenter {
private:
    const LexiconFSA* lexicon_;

    static bool is_vowel(char c) {
        return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
    }

    static bool has_letter(const std::string& s) {
        for (unsigned char c : s) {
            if (std::isalpha(c)) return true;
        }
        return false;
    }

    static size_t shared_prefix_length(const std::string& a, const std::string& b) {
        const size_t n = std::min(a.size(), b.size());
        size_t i = 0;
        while (i < n && a[i] == b[i]) i++;
        return i;
    }

    static bool has_feature(const Analysis& a, const std::string& tag) {
        for (const auto& f : a.features()) {
            if (f == tag) return true;
        }
        return false;
    }

    /** suffix_label: Map an analysis to the label for its suffix piece */
    static std::string suffix_label(const Analysis& a) {
        if (a.is_derivational())            return piece_label::SUFFIX_DERIV;
        if (has_feature(a, "+PL"))          return piece_label::SUFFIX_PL;
        if (has_feature(a, "+PAST") ||
            has_feature(a, "+PAST_PART"))   return piece_label::SUFFIX_PAST;
        if (has_feature(a, "+PRES_PART"))   return piece_label::SUFFIX_ING;
        if (has_feature(a, "+3SG"))         return piece_label::SUFFIX_3SG;
        if (has_feature(a, "+COMP"))        return piece_label::SUFFIX_COMP;
        if (has_feature(a, "+SUP"))         return piece_label::SUFFIX_SUP;
        return piece_label::SUFFIX_INFL;
    }

    /**
     * has_stem_class: Does any lexeme for this lemma carry `sc`?
     *
     * A stem can appear in the lexicon more than once, including under
     * different stem classes: "box" is listed both as a regular noun and as a
     * sibilant one. Asking whether ANY entry carries the class, rather than
     * reading the class off the first entry found, keeps alternation detection
     * independent of insertion order.
     */
    bool has_stem_class(const std::string& lemma, StemClass sc) const {
        if (!lexicon_) return false;
        for (const auto& lex : lexicon_->lookup_all(lemma)) {
            if (lex->stem_class() == sc) return true;
        }
        return false;
    }

    /**
     * ends_in_sibilant: Does the lemma end in a sibilant grapheme?
     *
     * The orthographic trigger for the epenthetic e before -s: kiss, buzz,
     * box, church, dish. Used alongside the SIBILANT stem class so that a
     * lemma classified only as regular is still recognized.
     */
    static bool ends_in_sibilant(const std::string& lemma) {
        if (lemma.empty()) return false;
        const char last = lemma.back();
        if (last == 's' || last == 'z' || last == 'x' || last == 'j') return true;
        if (lemma.size() >= 2) {
            const std::string tail = lemma.substr(lemma.size() - 2);
            if (tail == "ch" || tail == "sh") return true;
        }
        return false;
    }

    /**
     * detect_alt: Name the orthographic alternation at the stem/suffix seam
     *
     * `region` is the lowercased word with any prefix removed, `lemma` the
     * lowercased lemma, and `lcp` the shared prefix length between them.
     */
    std::string detect_alt(const std::string& region, const std::string& lemma,
                           size_t lcp) const {
        if (lcp >= region.size()) return seg_alt::NONE;  // no suffix piece

        // city -> cities: the lemma's final y surfaces as i
        if (lcp < lemma.size() && lemma[lcp] == 'y' && region[lcp] == 'i') {
            return seg_alt::Y_TO_I;
        }

        // hope -> hoping: the lemma's final silent e is dropped
        if (lcp + 1 == lemma.size() && lemma[lcp] == 'e') {
            return seg_alt::E_DELETION;
        }

        if (lcp == lemma.size() && lcp > 0) {
            // stop -> stopped: the final consonant is doubled
            if (region[lcp] == lemma[lcp - 1] && !is_vowel(region[lcp])) {
                return seg_alt::CONSONANT_DOUBLING;
            }
            // kiss -> kisses: an epenthetic e appears before the -s
            const std::string rest = region.substr(lcp);
            if (rest.size() >= 2 && rest[0] == 'e' && rest[1] == 's' &&
                (ends_in_sibilant(lemma) ||
                 has_stem_class(lemma, StemClass::SIBILANT))) {
                return seg_alt::EPENTHESIS;
            }
        }

        return seg_alt::NONE;
    }

    /** fill_offsets: Derive code-point offsets from the piece texts */
    static void fill_offsets(Segmentation& seg) {
        seg.offsets.clear();
        seg.offsets.push_back(0);
        size_t running = 0;
        for (const auto& p : seg.pieces) {
            running += utf8::count(p.text);
            seg.offsets.push_back(running);
        }
    }

    /** single_piece: Build a one-piece record covering the whole word */
    static Segmentation single_piece(const std::string& word,
                                     const std::string& label,
                                     const std::string& source,
                                     const std::string& gate) {
        Segmentation seg;
        seg.word   = word;
        seg.source = source;
        seg.gate   = gate;
        if (!word.empty()) {
            seg.pieces.emplace_back(word, label);
        }
        fill_offsets(seg);
        return seg;
    }

public:
    explicit Segmenter(const LexiconFSA* lexicon) : lexicon_(lexicon) {}

    /**
     * ascii_lower: Lowercase ASCII letters, leaving every other byte alone
     *
     * This mirrors what Analyzer::analyze does to its input, so the lowercased
     * copy used for boundary finding lines up byte for byte with the original.
     */
    static std::string ascii_lower(const std::string& s) {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return out;
    }

    /**
     * passthrough: Record for input with no letters at all
     *
     * Punctuation, digit strings, and the empty line are reported as
     * passthrough: there is nothing for a morphological analyzer to say about
     * them. An empty word yields no pieces and the single offset [0].
     */
    static Segmentation passthrough(const std::string& word) {
        return single_piece(word, piece_label::UNK,
                            seg_source::PASSTHROUGH, seg_gate::PASSTHROUGH);
    }

    /** fallback: Record for a word the analyzer could not analyze */
    static Segmentation fallback(const std::string& word) {
        if (word.empty()) return passthrough(word);
        return single_piece(word, piece_label::UNK,
                            seg_source::FALLBACK, seg_gate::NO_ANALYSIS);
    }

    /**
     * is_passthrough_input: Should this input skip morphological analysis?
     *
     * The rule is deliberately simple: an input containing no ASCII letter
     * cannot carry English morphology, so it is passed through. An input with
     * at least one letter is analyzed, and reported as fallback if nothing is
     * found.
     *
     * Input that is not well-formed UTF-8 is never passed through, even when
     * it has no letters in it. It is not punctuation or a number, it is
     * damaged text, and the format reports it as a fallback record.
     */
    static bool is_passthrough_input(const std::string& word) {
        return !has_letter(word) && utf8::is_valid(word);
    }

    /**
     * from_analysis: Build the surface-piece view of one analysis
     *
     * @param word         The original input word, used for all piece slices
     * @param a            An analysis the analyzer produced for `word`
     * @param is_irregular Whether `a` came from the stored irregular forms
     */
    Segmentation from_analysis(const std::string& word, const Analysis& a,
                               bool is_irregular) const {
        const std::string lower = ascii_lower(word);
        const std::string lemma = a.stem();

        Segmentation seg;
        seg.word   = word;
        seg.lemma  = lemma;
        seg.source = seg_source::FST;
        seg.weight = a.weight();

        // Irregular and suppletive forms stay whole: went, mice, children.
        if (is_irregular) {
            seg.pieces.emplace_back(word, piece_label::IRREGULAR);
            seg.gate = seg_gate::IRREGULAR;
            fill_offsets(seg);
            return seg;
        }

        // A prefix piece, when the analysis stripped one
        size_t region_start = 0;
        const std::string& prefix = a.prefix();
        if (!prefix.empty() && lower.size() > prefix.size() &&
            lower.compare(0, prefix.size(), prefix) == 0) {
            region_start = utf8::snap_to_boundary(word, prefix.size());
        }

        const std::string region = lower.substr(region_start);
        const size_t lcp = shared_prefix_length(region, lemma);

        // No shared material with the lemma means there is no boundary to
        // report: keep the word whole rather than inventing a split.
        if (lcp == 0) {
            seg.pieces.emplace_back(word, piece_label::IRREGULAR);
            seg.gate = seg_gate::IRREGULAR;
            fill_offsets(seg);
            return seg;
        }

        if (region_start > 0) {
            seg.pieces.emplace_back(word.substr(0, region_start),
                                    piece_label::PREFIX);
        }

        const size_t stem_end = utf8::snap_to_boundary(word, region_start + lcp);
        seg.pieces.emplace_back(word.substr(region_start, stem_end - region_start),
                                piece_label::STEM);

        if (stem_end < word.size()) {
            seg.pieces.emplace_back(word.substr(stem_end), suffix_label(a));
        }

        seg.alt  = detect_alt(region, lemma, lcp);
        seg.gate = seg.pieces.size() >= 2 ? seg_gate::DECOMPOSED
                                          : seg_gate::LEXICON_STEM;
        fill_offsets(seg);
        return seg;
    }
};

// ── Ranking ────────────────────────────────────────────────────────────────

/**
 * segmentation_less: Total order over candidate segmentations
 *
 * Primary key is the analyzer's own weight, descending, which is the ranking
 * the lexical output already uses. The remaining keys exist because weights
 * tie often (two readings of "unkind", two of "sheep") and std::sort is not
 * stable: without a total order the chosen record would depend on the
 * standard library implementation. Preferring more pieces on a tie also means
 * that when a decomposed and an undecomposed reading are equally weighted,
 * the decomposed one is reported.
 */
inline bool segmentation_less(const Segmentation& a, const Segmentation& b) {
    if (a.weight != b.weight) return a.weight > b.weight;
    if (a.pieces.size() != b.pieces.size()) {
        return a.pieces.size() > b.pieces.size();
    }
    if (a.lemma != b.lemma) return a.lemma < b.lemma;
    for (size_t i = 0; i < a.pieces.size(); i++) {
        if (a.pieces[i].label != b.pieces[i].label) {
            return a.pieces[i].label < b.pieces[i].label;
        }
    }
    if (a.gate != b.gate) return a.gate < b.gate;
    return a.alt < b.alt;
}

/** same_segmentation: Do two records describe the same surface split? */
inline bool same_segmentation(const Segmentation& a, const Segmentation& b) {
    if (a.pieces.size() != b.pieces.size()) return false;
    if (a.lemma != b.lemma || a.gate != b.gate || a.alt != b.alt) return false;
    for (size_t i = 0; i < a.pieces.size(); i++) {
        if (a.pieces[i].text != b.pieces[i].text) return false;
        if (a.pieces[i].label != b.pieces[i].label) return false;
    }
    return true;
}

#endif // SEGMENTATION_H
