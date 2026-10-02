#ifndef TEXT_SEGMENTER_H
#define TEXT_SEGMENTER_H

#include "segment_driver.h"
#include "segmentation.h"
#include "../PIPELINE/text_pipeline.h"

#include <cctype>
#include <string>
#include <vector>

/**
 * text_segmenter.h: Surface segmentation for lines of running text
 *
 * Word mode takes one word per line. Text mode takes raw text, hands it to
 * Scalpel for tokenization, and emits one record per token with two extra
 * fields: `kind`, the Scalpel token class, and `ws_before`, the exact
 * whitespace that preceded the token.
 *
 * Reconstruction guarantee:
 *   Concatenating ws_before + the pieces over a line's records reproduces the
 *   line exactly. The records tile the input: every byte is in exactly one
 *   record, either in a piece or in a ws_before.
 *
 * Two things make that harder than it sounds:
 *
 *   Scalpel does not always emit a token for every character. A stray
 *   apostrophe in "'single'", for instance, is dropped. Any uncovered span
 *   holding non-whitespace is recovered here as its own PUNCT record, so
 *   nothing is silently lost.
 *
 *   Whitespace at the end of a line has no following token to attach to. It is
 *   carried forward, along with the line terminator, into the ws_before of the
 *   next token, which may be on the next line. Whatever is still pending at
 *   end of input becomes one final record with an empty word.
 */

/** TokenRecord: one output record in text mode */
struct TokenRecord {
    Segmentation seg;
    std::vector<Segmentation> alternatives;
    std::string kind;
    std::string ws_before;
};

class LineSegmenter {
private:
    const WordSegmenter* words_;
    int nbest_;

    static bool is_space(char c) {
        return std::isspace(static_cast<unsigned char>(c)) != 0;
    }

    /** make_record: Build a record for a token, analyzing it if it is a word */
    TokenRecord make_record(const std::string& text, const std::string& kind,
                            const std::string& ws_before) const {
        TokenRecord record;
        record.kind = kind;
        record.ws_before = ws_before;

        if (kind == "WORD") {
            auto ranked = words_->segment_word(text);
            record.seg = ranked[0];
            record.alternatives.assign(ranked.begin() + 1, ranked.end());
            if (static_cast<int>(record.alternatives.size()) > nbest_ - 1) {
                record.alternatives.resize(nbest_ - 1);
            }
        } else {
            // Punctuation, numbers and the rest are reported as the raw
            // surface string, not as a wrapped tag.
            record.seg = Segmenter::passthrough(text);
        }
        return record;
    }

    /**
     * flush_gap: Account for a span of the line no token covered
     *
     * The span is split into leading whitespace, a non-whitespace core, and
     * trailing whitespace. The core, when there is one, becomes a PUNCT
     * record of its own. Whatever whitespace is left over is returned so it
     * can attach to the next token.
     *
     * @param gap      The uncovered span
     * @param pending  Whitespace already waiting for a token, consumed here
     * @param out      Records are appended here
     * @return         Whitespace to carry forward to the next token
     */
    std::string flush_gap(const std::string& gap, const std::string& pending,
                          std::vector<TokenRecord>& out) const {
        size_t first = 0;
        while (first < gap.size() && is_space(gap[first])) first++;

        if (first == gap.size()) {
            return pending + gap;  // all whitespace, nothing to recover
        }

        size_t last = gap.size();
        while (last > first && is_space(gap[last - 1])) last--;

        out.push_back(make_record(gap.substr(first, last - first), "PUNCT",
                                  pending + gap.substr(0, first)));
        return gap.substr(last);
    }

public:
    LineSegmenter(const WordSegmenter* words, int nbest)
        : words_(words), nbest_(nbest) {}

    /**
     * segment_line: Records for one line of text
     *
     * @param line     One line, with no line terminator
     * @param pending  Whitespace carried in from earlier input. On return it
     *                 holds whatever this line left unattached, for the next
     *                 line's first token.
     */
    std::vector<TokenRecord> segment_line(const std::string& line,
                                          std::string& pending) const {
        std::vector<TokenRecord> out;
        const auto tokens = tokenize_line(line);

        size_t cursor = 0;
        for (const auto& token : tokens) {
            if (token.start < cursor) continue;  // never happens, guard anyway

            pending = flush_gap(line.substr(cursor, token.start - cursor),
                                pending, out);
            out.push_back(make_record(token.text, token.kind, pending));
            pending.clear();
            cursor = token.start + token.text.size();
        }

        // Whatever follows the last token: whitespace is carried forward, and
        // anything else becomes a record so the line stays reproducible.
        pending = flush_gap(line.substr(cursor), pending, out);
        return out;
    }

    /** trailing_record: The final record holding input-ending whitespace */
    TokenRecord trailing_record(const std::string& pending) const {
        return make_record("", "PUNCT", pending);
    }
};

#endif // TEXT_SEGMENTER_H
