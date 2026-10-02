#ifndef JSONL_PRINT_H
#define JSONL_PRINT_H

#include "../ANALYSIS/segmentation.h"

#include <cmath>
#include <cstdio>
#include <ostream>
#include <string>
#include <vector>

/**
 * jsonl_print.h: JSON Lines writer for surface segmentations
 *
 * One JSON object per input line, with a fixed field order so the output
 * diffs cleanly:
 *
 *   {"word": "cities", "pieces": ["cit","ies"], "offsets": [0,3,6],
 *    "labels": ["STEM","SUFFIX_PL"], "lemma": "city", "alt": "y_to_i",
 *    "source": "fst", "gate": "decomposed", "n_analyses": 1, "weight": 0.92}
 *
 * The output is always valid JSON, for any byte sequence on input. Quotes,
 * backslashes, and control characters are escaped; well-formed UTF-8 passes
 * through as-is; bytes that are not well-formed UTF-8 are escaped one by one
 * as \u00XX so the result stays valid UTF-8. Numbers are always finite.
 */
class JsonlPrinter {
public:
    /**
     * escape: JSON-escape a string, including non-UTF-8 input
     *
     * Valid multi-byte sequences are emitted verbatim, which keeps readable
     * words such as "naive" with an accent readable in the output. Any byte
     * that cannot be part of a well-formed sequence is emitted as \u00XX,
     * treating it as a Latin-1 code point. Round-tripping still holds for
     * such input because "word" and the single fallback piece are escaped
     * identically, so a JSON reader sees the same string for both.
     */
    static std::string escape(const std::string& s) {
        std::string out;
        out.reserve(s.size() + 8);

        for (size_t i = 0; i < s.size();) {
            const unsigned char c = static_cast<unsigned char>(s[i]);

            if (c < 0x80) {
                switch (c) {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b";  break;
                    case '\f': out += "\\f";  break;
                    case '\n': out += "\\n";  break;
                    case '\r': out += "\\r";  break;
                    case '\t': out += "\\t";  break;
                    default:
                        if (c < 0x20 || c == 0x7F) {
                            out += unicode_escape(c);
                        } else {
                            out += static_cast<char>(c);
                        }
                }
                i++;
                continue;
            }

            bool valid = false;
            const size_t len = utf8::sequence_length(s, i, valid);
            if (valid) {
                out.append(s, i, len);
            } else {
                out += unicode_escape(c);
            }
            i += len;
        }

        return out;
    }

    /**
     * write: Emit one record, without a trailing newline
     *
     * @param os            Destination stream
     * @param seg           The primary segmentation
     * @param alternatives  Other segmentations, already ranked; omitted from
     *                      the output entirely when empty and `nbest` is 1
     * @param nbest         Value of --nbest; an "alternatives" array is
     *                      written whenever this is greater than 1
     */
    static void write(std::ostream& os, const Segmentation& seg,
                      const std::vector<Segmentation>& alternatives,
                      int nbest) {
        os << '{';
        write_body(os, seg);

        if (nbest > 1) {
            os << ", \"alternatives\": [";
            for (size_t i = 0; i < alternatives.size(); i++) {
                if (i) os << ", ";
                os << '{';
                write_body(os, alternatives[i]);
                os << '}';
            }
            os << ']';
        }

        os << '}';
    }

    /**
     * write_token: Emit one record for text mode
     *
     * Same fields as write(), plus the two token-level fields that only make
     * sense when the input was a line of running text rather than a word:
     *   kind       Scalpel's token class (WORD, PUNCT, NUMBER, ...)
     *   ws_before  the exact whitespace that preceded the token
     */
    static void write_token(std::ostream& os, const Segmentation& seg,
                            const std::vector<Segmentation>& alternatives,
                            int nbest, const std::string& kind,
                            const std::string& ws_before) {
        os << '{';
        write_body(os, seg);
        os << ", \"kind\": \"" << escape(kind) << '"'
           << ", \"ws_before\": \"" << escape(ws_before) << '"';

        if (nbest > 1) {
            os << ", \"alternatives\": [";
            for (size_t i = 0; i < alternatives.size(); i++) {
                if (i) os << ", ";
                os << '{';
                write_body(os, alternatives[i]);
                os << '}';
            }
            os << ']';
        }

        os << '}';
    }

private:
    static std::string unicode_escape(unsigned char c) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
        return std::string(buf);
    }

    /**
     * number: Format a weight as a finite JSON number
     *
     * %.6g keeps the common values short (1, 0.9, 0.81) and never produces
     * the JSON-invalid tokens nan or inf.
     */
    static std::string number(float value) {
        if (!std::isfinite(value)) return "0";
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6g", static_cast<double>(value));
        return std::string(buf);
    }

    /** write_body: The shared field list, without the enclosing braces */
    static void write_body(std::ostream& os, const Segmentation& seg) {
        os << "\"word\": \"" << escape(seg.word) << '"';

        os << ", \"pieces\": [";
        for (size_t i = 0; i < seg.pieces.size(); i++) {
            if (i) os << ", ";
            os << '"' << escape(seg.pieces[i].text) << '"';
        }
        os << ']';

        os << ", \"offsets\": [";
        for (size_t i = 0; i < seg.offsets.size(); i++) {
            if (i) os << ", ";
            os << seg.offsets[i];
        }
        os << ']';

        os << ", \"labels\": [";
        for (size_t i = 0; i < seg.pieces.size(); i++) {
            if (i) os << ", ";
            os << '"' << escape(seg.pieces[i].label) << '"';
        }
        os << ']';

        os << ", \"lemma\": \""  << escape(seg.lemma)  << '"';
        os << ", \"alt\": \""    << escape(seg.alt)    << '"';
        os << ", \"source\": \"" << escape(seg.source) << '"';
        os << ", \"gate\": \""   << escape(seg.gate)   << '"';
        os << ", \"n_analyses\": " << seg.n_analyses;
        os << ", \"weight\": "     << number(seg.weight);
    }
};

#endif // JSONL_PRINT_H
