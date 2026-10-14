#ifndef SEGMENTATION_H
#define SEGMENTATION_H

#include "analysis.h"
#include "../LEXICON/closed_class.h"
#include "../LEXICON/lexicon_fsa.h"
#include "../RULES/prefix_rule.h"

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
     * promotable: May this reading outrank a stored whole word?
     *
     * Read only by segmentation_less_derived_first, which is the one place a
     * decomposition is allowed to beat a higher-weighted whole word. Nothing
     * prints it and the default ranking never looks at it, so it changes
     * neither the output format nor the default output. Segmenter::from_analysis
     * documents the conditions; `demoted` below is the other verdict, and that
     * one both rankings read.
     */
    bool promotable = false;

    /**
     * demoted: Is this a reading the layer declines to report first?
     *
     * Read by both rankings, which is what separates it from `promotable`. A
     * decomposition the promotion guard blocks still competes on weight,
     * because its weight is a weight for the word: `numb|er` scores what the
     * adjective `numb` scores and loses to the stored noun `number` on the
     * default ranking without any help. Two kinds of reading carry no such
     * weight of their own:
     *
     *   - A prefix reading. Its weight is the weight of the base, copied
     *     unchanged, so `co|me` is reported at the weight of the pronoun `me`
     *     and outranks the stored verb `come` on either ranking. Weight cannot
     *     settle that pair, so the prefix guard does, and a reading it rejects
     *     sorts below every other reading of the word.
     *
     *   - A plural on a one or two letter stem. `SI|S` is the noun `si` plus a
     *     plural at the weight of `si`, and `sis` is not a lexeme, so there is
     *     nothing for it to lose to.
     *
     * Nothing prints the field, and a demoted reading is not withdrawn: it is
     * still reported under --nbest 2, exactly as a blocked promotion is.
     * Segmenter::prefix_is_licensed and Segmenter::plural_stem_is_long_enough
     * are the two places the value is decided.
     */
    bool demoted = false;

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
    const ClosedClassSet* closed_class_;

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

    /** suffix_label: Map an analysis to the label for its inflectional piece */
    static std::string suffix_label(const Analysis& a) {
        if (has_feature(a, "+PL"))          return piece_label::SUFFIX_PL;
        if (has_feature(a, "+PAST") ||
            has_feature(a, "+PAST_PART"))   return piece_label::SUFFIX_PAST;
        if (has_feature(a, "+PRES_PART"))   return piece_label::SUFFIX_ING;
        if (has_feature(a, "+3SG"))         return piece_label::SUFFIX_3SG;
        if (has_feature(a, "+COMP"))        return piece_label::SUFFIX_COMP;
        if (has_feature(a, "+SUP"))         return piece_label::SUFFIX_SUP;
        if (a.features().empty())           return piece_label::SUFFIX_DERIV;
        return piece_label::SUFFIX_INFL;
    }

    /**
     * split_suffixes: Cut the material after the stem into one piece per morph
     *
     * `tail` is everything after the stem piece, and the analysis says which
     * derivational suffixes are stacked in it, innermost first: "alities" for
     * "nationalities", carrying -al and -ity, with the plural still to come.
     *
     * Each piece is the longest prefix of what is left that the next suffix
     * accounts for, which is the same rule the stem piece follows, so the
     * altered material at a seam goes to the piece after it: "nationality"
     * before a plural gives "it" and then "ies", exactly as "city" gives "cit"
     * and then "ies". The one exception is the first piece, which also carries
     * whatever the stem's own alternation displaced: "happiness" is "happ" and
     * then "iness", not "happ", "i", "ness".
     *
     * A suffix that cannot be found is not forced. The rest of the tail
     * becomes a single piece, which is what the output has always done, so a
     * suffix whose surface form this does not predict costs a boundary rather
     * than inventing one in the wrong place.
     */
    static std::vector<SurfacePiece> split_suffixes(const std::string& tail,
                                                    const Analysis& a) {
        std::vector<SurfacePiece> out;
        size_t at = 0;

        const auto& suffixes = a.deriv_suffixes();
        for (size_t i = 0; i < suffixes.size() && at < tail.size(); i++) {
            // The stored form carries a leading hyphen: "-ness"
            const std::string suffix = suffixes[i].substr(1);
            const std::string rest = tail.substr(at);

            // Only the first piece may absorb a displaced character
            size_t skip = 0;
            if (!suffix.empty() && rest[0] != suffix[0] && i == 0 &&
                rest.size() > 1 && rest[1] == suffix[0]) {
                skip = 1;
            }
            const size_t matched =
                shared_prefix_length(rest.substr(skip), suffix);
            if (matched == 0) break;

            out.emplace_back(tail.substr(at, skip + matched),
                             piece_label::SUFFIX_DERIV);
            at += skip + matched;
        }

        if (at < tail.size()) {
            out.emplace_back(tail.substr(at), suffix_label(a));
        }
        return out;
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

    /**
     * shortest_affix_bytes: Length of the smallest affix in the reading
     *
     * The affix, not the piece. Three of the alternations move a character out
     * of the stem and into the piece after it, so the piece overstates the
     * affix by one: `bet|ter` is the two-letter `-er` with a doubled
     * consonant in front of it, `cit|ies` is the one-letter `-s`, and
     * `happ|iness` is `-ness`. Measuring the piece instead let `better`,
     * `matter` and `dinner` past the fourth condition of the guard below,
     * which is the one that was supposed to catch them.
     *
     * e_deletion is not one of the three: it takes a character out of the stem
     * without adding one to the suffix.
     *
     * Returns 0 when the reading has no affix piece at all.
     */
    static size_t shortest_affix_bytes(const std::vector<SurfacePiece>& pieces,
                                       const std::string& alt) {
        const bool displaced = alt == seg_alt::Y_TO_I ||
                               alt == seg_alt::CONSONANT_DOUBLING ||
                               alt == seg_alt::EPENTHESIS;
        size_t shortest = 0;
        bool after_stem = false;
        for (const auto& p : pieces) {
            if (p.label == piece_label::STEM) {
                after_stem = true;
                continue;
            }
            size_t length = p.text.size();
            // Only the piece right after the stem carries the displaced
            // character, and only when the stem is where it came from.
            if (displaced && after_stem && length > 1) {
                length--;
                after_stem = false;
            }
            if (shortest == 0 || length < shortest) shortest = length;
        }
        return shortest;
    }

    /**
     * best_lexeme: The highest-weighted lexeme for a stem, optionally by POS
     *
     * @param stem  Lowercased stem to look up
     * @param pos   Part of speech to require, or nullptr for any
     * @return      The entry with the highest weight, or null if there is none
     */
    std::shared_ptr<Lexeme> best_lexeme(const std::string& stem,
                                        const PartOfSpeech* pos) const {
        std::shared_ptr<Lexeme> best;
        if (!lexicon_) return best;
        for (const auto& lex : lexicon_->lookup_all(stem)) {
            if (pos && lex->pos() != *pos) continue;
            if (!best || lex->weight() > best->weight()) best = lex;
        }
        return best;
    }

    /**
     * PREFIX_FREQUENCY_MARGIN: How much commoner the base has to be
     *
     * Half a Zipf point on the scale in docs/LEXICON.md, where a weight is
     * 0.70 + 0.075 * zipf, so half a point is 0.0375 of weight and the base
     * has to be about three times as frequent as the whole word.
     *
     * A live prefix costs frequency: `write` is Zipf 5.1 and `rewrite` 3.5,
     * `kind` is 5.5 and `unkind` 2.9, `sense` is 5.2 and `nonsense` 4.3. A
     * lexicalized Latinate form sits level with the word it looks like it was
     * built from: `turn` is 5.3 and `return` 5.2, `sent` and `present` are
     * both 5.2, `stand` is 5.2 and `understand` 5.3. The margin is what
     * separates the two groups, and on the eleven words docs/LEXICON.md
     * tabulates it has a factor of six of headroom: every reading that should
     * stand clears 0.8 Zipf and every reading that should not stays inside
     * 0.15.
     */
    static constexpr float PREFIX_FREQUENCY_MARGIN = 0.0375f;

    /**
     * best_attaching_lexeme: The commonest base the prefix could attach to
     *
     * @param base    Lowercased material after the prefix
     * @param prefix  The prefix, which decides the parts of speech allowed
     */
    std::shared_ptr<Lexeme> best_attaching_lexeme(
            const std::string& base, const std::string& prefix) const {
        std::shared_ptr<Lexeme> best;
        if (!lexicon_) return best;
        for (const auto& lex : lexicon_->lookup_all(base)) {
            if (!prefix_attaches(prefix, lex->pos())) continue;
            if (!best || lex->weight() > best->weight()) best = lex;
        }
        return best;
    }

    /**
     * prefix_is_licensed: Is the prefix piece in this reading a real prefix?
     *
     * Prefix stripping had no guard at all, which is why `come` came out as
     * `co|me`, `understand` as `under|stand`, `present` as `pre|sent` and
     * `department` as `de|part|ment`: the analyzer tries each of fifteen
     * prefixes against every analysis of the remainder, and nothing asked
     * whether what followed was a word the prefix attaches to. Four conditions
     * now do, and they are the prefix side of the six in is_promotable below.
     *
     *   1. The base is at least three characters. `co|me` goes here, and with
     *      it 17,000 test tokens: the base is the two-letter pronoun, and a
     *      prefix on a stem that short is an accident of spelling. Three
     *      characters is the figure the promotion guard and the gold hygiene
     *      filter both already use for a stem.
     *
     *   2. The base is a lexeme as it is spelled, under a part of speech the
     *      prefix attaches to. RULES/prefix_rule.h records what each prefix
     *      takes: `re-` makes verbs out of verbs, `non-` takes a noun or an
     *      adjective, and so on.
     *
     *      Where the reading stacks a suffix outside the prefix the base is
     *      not a lexeme as spelled, because the suffix is still on it:
     *      `unhappiness` leaves `happiness`, which the rules build rather than
     *      store, and `department` leaves `partment`, which is not a word at
     *      all. There the condition is asked of the two forms the reading does
     *      claim: the lemma has to be a lexeme the prefix attaches to, and the
     *      prefixed lemma, which is what the suffix attaches to, has to be a
     *      lexeme as well. `-ness` attaching to `unhappy` is a claim about the
     *      word `unhappy`, and `-ment` attaching to `depart` is a claim about
     *      the word `depart`; both hold, and condition 4 is then what
     *      separates them.
     *
     *   3. The base is not closed class. A prefix does not attach to a
     *      function word, and Analyzer::analyze already declines to strip one
     *      off a FUNCTION lexeme so that `unto` is not `un|to`. The test is
     *      made again here against data/closed_class.tsv, which is read as a
     *      word list whether or not it was loaded as a lexicon.
     *
     *   4. The form the word is derived from is more frequent than the word,
     *      by the margin above. That form is the base for a prefix on its own
     *      and the prefixed lemma for a prefix with a suffix outside it, so
     *      `unhappy` is measured against `happy` and `unhappiness` against
     *      `unhappy`. This is the condition that separates `re|write` from
     *      `re|turn`, `pre|war` from `pre|sent` and `un|happ|iness` from
     *      `de|part|ment`, and it is the only one that can: both members of
     *      each pair are a listed prefix on a listed base of the right
     *      category. `department` is commoner than `depart` and
     *      `unhappiness` is rarer than `unhappy`, which is a word commoner
     *      than the thing it is supposedly built from against one that is
     *      not. A weight that was never stated counts as unknown and passes,
     *      and a whole word the lexicon does not hold has no frequency to
     *      beat, which is how `prewar` keeps its split.
     *
     * A reading this rejects is not withdrawn. It is demoted, which is
     * Segmentation::demoted and sorts it below every other reading of the
     * word on both rankings, and it is still reported under --nbest 2.
     *
     * @param lower  The lowercased input word
     * @param a      The analysis the record was built from
     */
    bool prefix_is_licensed(const std::string& lower,
                            const Analysis& a) const {
        const std::string& prefix = a.prefix();
        if (prefix.empty()) return true;
        if (lower.size() <= prefix.size()) return true;
        const std::string base = lower.substr(prefix.size());

        // 1. A base of at least three characters
        if (utf8::count(base) < 3) return false;

        // 3. The base is not a function word. Asked before the lexeme lookup
        //    because the closed-class list is read independently of --lexicon.
        if (closed_class_ && closed_class_->contains(base)) return false;
        {
            const PartOfSpeech func = PartOfSpeech::FUNCTION;
            if (best_lexeme(base, &func)) return false;
        }

        // 2. The base is a lexeme under a part of speech the prefix takes,
        //    or, where a suffix is stacked outside the prefix, the lemma is
        //    and the prefixed lemma the suffix attaches to is a lexeme too.
        //    `source` is the form the word is derived from, which condition 4
        //    measures: the base in the first case, the prefixed lemma in the
        //    second.
        auto source = best_attaching_lexeme(base, prefix);
        if (!source) {
            const std::string& lemma = a.stem();
            if (lemma == base) return false;
            if (!best_attaching_lexeme(lemma, prefix)) return false;
            source = best_lexeme(prefix + lemma, nullptr);
            if (!source) return false;
        }

        // 4. The form the word is derived from is commoner than the word, by
        //    the margin
        if (source->weight_known()) {
            const auto whole_lex = best_lexeme(lower, nullptr);
            if (whole_lex && whole_lex->weight() >
                    source->weight() - PREFIX_FREQUENCY_MARGIN) {
                return false;
            }
        }

        return true;
    }

    /**
     * plural_stem_is_long_enough: Is there a noun under this plural?
     *
     * A plural reading of a one or two letter noun is a mining artifact, not
     * morphology: `SIS` came out as `SI|S` off the two-letter noun `si`, and
     * `sis` is not a lexeme, so nothing competed with it. Three characters is
     * the same figure condition 2 of the promotion guard uses, and the same
     * one tools/eval_lexicon.py drops a gold row for.
     *
     * The lemma is measured rather than the stem piece, since an alternation
     * can leave the piece shorter: `flies` is `fl|ies` from `fly`.
     */
    static bool plural_stem_is_long_enough(const Analysis& a) {
        if (!has_feature(a, "+PL")) return true;
        return utf8::count(a.stem()) >= 3;
    }

    /**
     * is_promotable: May this decomposition outrank a stored whole word?
     *
     * The question only arises under --derived-first, which is the one ranking
     * where a decomposition beats a higher-weighted whole word. Ranking purely
     * on "decomposes at all" split the most frequent words in English into a
     * letter and a suffix: "is" became "i|s", "his" became "hi|s" and "number"
     * became "numb|er", and those three alone are 1.3% of the test tokens in
     * docs/BASELINES.md. Six conditions have to hold.
     *
     * A demoted reading never gets here: prefix_is_licensed and
     * plural_stem_is_long_enough are asked first, and a reading either of them
     * rejects is ranked below the whole words rather than level with them.
     *
     *   1. The word is not closed class. A function word has no morphology, so
     *      a decomposition of one is wrong whatever the lexicon says about its
     *      substrings. See LEXICON/closed_class.h.
     *
     *   2. The lemma is at least three characters. One and two letter lemmas
     *      are where the spurious readings come from: the noun "i" with a
     *      plural, the noun "hi", the noun "wa", the noun "u". A real stem that
     *      short carries no derivation worth a boundary. The lemma is measured,
     *      not the stem piece, because an alternation can leave the piece
     *      shorter than the lemma: "flies" is "fl|ies" from the three-letter
     *      lemma "fly".
     *
     *   3. The lemma is a lexeme in the loaded lexicon under the part of
     *      speech the affix attaches to. Every rule-derived analysis already
     *      satisfies this by construction, since that is how it was found; the
     *      test is made again here so the guard states it rather than assuming
     *      it, and so a reading that ever arrives another way is held to it.
     *
     *   4. Where the affix is one or two characters, the whole word is not
     *      more frequent than the stem. This is the frequency test, and it is the
     *      only thing that separates "number" from "higher": both are stored
     *      nouns whose only decomposition is an adjective plus comparative
     *      "-er", and the difference is that "number" is far more frequent than
     *      "numb" while "higher" is less frequent than "high". Restricting it
     *      to short affixes is what keeps "govern|ment" and "quick|ly"; see the
     *      decision in docs/LEXICON.md for the measurement behind that.
     *
     *   5. The word is not a stored irregular form. Where the lexicon lists
     *      the spelling outright, that listing is what the word is, and a
     *      reading the rules assembled is not promoted over it: "broken" is
     *      the participle of "break", not the adjective "broke" plus -en, and
     *      the only reason the rules reach the second one is that "broke" is a
     *      lexeme too. The irregular reading is itself a single piece, so this
     *      condition always ends in the word coming out whole.
     *
     *   6. Where a doubled consonant reaches a suffix that does not make
     *      verbs, the whole word is not a verb in its own right. Doubling is
     *      the one alternation the spelling cannot predict, so a doubled
     *      reading rests entirely on the stem class the lexicon states
     *      (decision 1 in docs/LEXICON.md), and it is where a spurious
     *      boundary is cheapest to spell: "manner" is "man" with a doubled n
     *      and -er, "hammer" is "ham", "copper" is "cop", "letter" is "let".
     *      What separates those from "runner" and "winner" is that the
     *      lexicon lists each of them as a verb as well as a noun, and an
     *      agentive -er noun does not become a verb: you run and you are a
     *      runner, but you letter a sign and hammer a nail, and neither verb
     *      is anything -er produces. A suffix that does make verbs is exempt,
     *      because there a verb is the expected reading: "flat|ten" and
     *      "mad|den" stand.
     *
     *      The brief for this work put the fix for "letter" in the rule, as a
     *      requirement that a doubling stem for the agentive -er be a verb
     *      rather than any lexeme. The rule already requires that: -er takes
     *      a verb and nothing else, and "let" is a DOUBLE_CONS verb in every
     *      generated lexicon, so "let|ter" satisfies it. The guard is where
     *      the distinction can actually be drawn, and the measurement is in
     *      docs/LEXICON.md.
     *
     * A reading the guard blocks is not withdrawn. It is ranked with the whole
     * words, where weight decides as it does by default, and it is still
     * reported under --nbest 2.
     *
     * @param lower    The lowercased input word
     * @param a        The analysis the record was built from
     * @param pieces   The pieces of the record
     * @param alt      The alternation at the stem seam, which decides whether
     *                 the piece after the stem overstates the affix by one
     * @param stored   Whether the lexicon holds this spelling as an irregular
     *                 form of some lexeme
     */
    bool is_promotable(const std::string& lower, const Analysis& a,
                       const std::vector<SurfacePiece>& pieces,
                       const std::string& alt, bool stored) const {
        if (pieces.size() < 2) return false;

        // 1. Closed class, either by the guard's own list or by a FUNCTION
        //    lexeme from a closed-class file that was loaded as a lexicon.
        if (closed_class_ && closed_class_->contains(lower)) return false;
        if (a.pos() == PartOfSpeech::FUNCTION) return false;
        {
            const PartOfSpeech func = PartOfSpeech::FUNCTION;
            if (best_lexeme(lower, &func)) return false;
        }

        // 2. A lemma of at least three characters
        const std::string& lemma = a.stem();
        if (utf8::count(lemma) < 3) return false;

        // 3. The lemma is a lexeme under the part of speech the affix takes
        const PartOfSpeech source_pos = a.pos();
        const auto stem_lex = best_lexeme(lemma, &source_pos);
        if (!stem_lex) return false;

        // 4. The frequency test, for one and two character affixes only
        const size_t affix = shortest_affix_bytes(pieces, alt);
        if (affix > 0 && affix <= 2 && stem_lex->weight_known()) {
            const auto whole_lex = best_lexeme(lower, nullptr);
            if (whole_lex && whole_lex->weight() > stem_lex->weight()) {
                return false;
            }
        }

        // 5. A stored irregular form is what the word is
        if (stored) return false;

        // 6. A doubled consonant reaching a suffix that does not make verbs,
        //    where the whole word is a verb of its own
        if (alt == seg_alt::CONSONANT_DOUBLING && a.is_derivational() &&
            a.derived_pos() != PartOfSpeech::VERB) {
            const PartOfSpeech verb = PartOfSpeech::VERB;
            if (best_lexeme(lower, &verb)) return false;
        }

        return true;
    }

public:
    /**
     * @param lexicon       Consulted for stem classes and for the guard
     * @param closed_class  Words --derived-first never splits, or nullptr
     */
    explicit Segmenter(const LexiconFSA* lexicon,
                       const ClosedClassSet* closed_class = nullptr)
        : lexicon_(lexicon), closed_class_(closed_class) {}

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
     * is_short_all_caps: Is this word written in capitals and short?
     *
     * Half the test for an acronym; WordSegmenter has the other half, which is
     * that every reading of the word is one the guards demoted. A short word
     * in capitals with nothing but a demoted reading behind it is far more
     * likely to be a name or an initialism than a morphologically complex
     * form, and reading it as one produced `SI|S` for the clipping of
     * `sister`.
     *
     * Six characters is the bound, and it is deliberately short: a long word
     * in capitals is a word someone shouted, not an initialism. A lowercase
     * form is never reached, so `sis` is unaffected and so is every word with
     * a reading the guards left alone, `CATS` included.
     */
    static bool is_short_all_caps(const std::string& word) {
        if (utf8::count(word) > 6) return false;
        bool any_upper = false;
        for (unsigned char c : word) {
            if (std::islower(c)) return false;
            if (std::isupper(c)) any_upper = true;
        }
        return any_upper;
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
     * @param stored       Whether the lexicon holds this spelling as an
     *                     irregular form of any lexeme, which is condition 5
     *                     of the guard on --derived-first. Defaults to false,
     *                     so a caller that does not know leaves the other four
     *                     conditions to decide.
     */
    Segmentation from_analysis(const std::string& word, const Analysis& a,
                               bool is_irregular, bool stored = false) const {
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
            for (auto& piece : split_suffixes(word.substr(stem_end), a)) {
                seg.pieces.push_back(piece);
            }
        }

        seg.alt  = detect_alt(region, lemma, lcp);
        seg.gate = seg.pieces.size() >= 2 ? seg_gate::DECOMPOSED
                                          : seg_gate::LEXICON_STEM;

        // A reading that keeps the word whole is never demoted: there would
        // be nothing to prefer it to but a split, which is the opposite of
        // what either guard is for.
        seg.demoted = seg.pieces.size() >= 2 &&
                      (!prefix_is_licensed(lower, a) ||
                       !plural_stem_is_long_enough(a));

        // A prefix reading is never promoted, only demoted. Promotion exists
        // because a derivational reading is multiplied by 0.9 and so loses to
        // any stored whole word of ordinary frequency; a prefix reading is
        // multiplied by nothing, since it carries the base's weight as it
        // stands, so it already beats a stored whole word wherever the base is
        // the commoner of the two. It needs a check, not a boost. Both
        // policies were measured, and docs/LEXICON.md has the numbers.
        seg.promotable = !seg.demoted && !a.has_prefix() &&
                         is_promotable(lower, a, seg.pieces, seg.alt, stored);
        fill_offsets(seg);
        return seg;
    }
};

// ── Ranking ────────────────────────────────────────────────────────────────

/**
 * segmentation_less: Total order over candidate segmentations
 *
 * First key is Segmentation::demoted, which holds the two kinds of reading
 * whose weight is not a weight for the word at all: a prefix reading carries
 * the base's weight, and a plural of a two-letter noun carries that noun's.
 * Both of them could otherwise outrank a stored whole word on either ranking
 * without anything having decided they should, which is how `co|me` beat
 * `come`. The field is documented on Segmentation.
 *
 * Then the analyzer's own weight, descending, which is the ranking the lexical
 * output already uses. The remaining keys exist because weights tie often (two
 * readings of "unkind", two of "sheep") and std::sort is not stable: without a
 * total order the chosen record would depend on the standard library
 * implementation. Preferring more pieces on a tie also means that when a
 * decomposed and an undecomposed reading are equally weighted, the decomposed
 * one is reported.
 */
inline bool segmentation_less(const Segmentation& a, const Segmentation& b) {
    if (a.demoted != b.demoted) return b.demoted;
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

/**
 * segmentation_less_derived_first: The ranking --derived-first selects
 *
 * One change from segmentation_less: a reading that decomposes, and that the
 * guard allows to be promoted, outranks any reading that keeps the word whole.
 * So `quickly` becomes `quick` plus `-ly` even though `quickly` is an adverb
 * lexeme in its own right, and `accused` becomes `accuse` plus `-d` even
 * though `accused` is a noun.
 *
 * The guard is `Segmentation::promotable`, and Segmenter::is_promotable states
 * its four conditions. A reading it blocks is ranked with the whole words
 * rather than withdrawn, so weight settles those pairs exactly as it does by
 * default: `number` stays whole because the stored noun at 1.12 outweighs
 * `numb` at 0.96, not because the `numb|er` reading went away. Making the
 * guard part of the first key rather than a test on the pair keeps this a
 * strict weak ordering: a blocked decomposition and a whole word are simply
 * two records with the same first key.
 *
 * The test is "decomposes at all", not "has more pieces". Ranking purely by
 * piece count also promotes a three-piece reading over a correct two-piece
 * one, which turned `recording` into `re|cord|ing` and `underground` into
 * `under|gr|ound`. Those readings are not competing with a stored whole word,
 * which is the only competition this flag is meant to settle, so once both
 * candidates decompose the ordering falls through to segmentation_less and
 * weight decides: `record` is far more frequent than `cord`.
 *
 * The default ranking is untouched, and so is the lexical analysis output,
 * which never consults either of these. This is a reporting order over
 * analyses the analyzer already produced, not a change to what it produces,
 * and both orders rank the same set.
 *
 * The cost is wrong splits on lexicalized words, where the decomposition
 * exists but is not what the word means any more, and on short function words
 * that happen to end in a suffix. docs/LEXICON.md measures both directions.
 */
inline bool segmentation_less_derived_first(const Segmentation& a,
                                            const Segmentation& b) {
    const bool a_promoted = a.pieces.size() > 1 && a.promotable;
    const bool b_promoted = b.pieces.size() > 1 && b.promotable;
    if (a_promoted != b_promoted) return a_promoted;
    return segmentation_less(a, b);
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
