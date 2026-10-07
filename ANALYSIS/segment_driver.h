#ifndef SEGMENT_DRIVER_H
#define SEGMENT_DRIVER_H

#include "analyzer.h"
#include "segmentation.h"

#include <algorithm>
#include <string>
#include <vector>

/**
 * segment_driver.h: Word to ranked surface segmentations
 *
 * This is the single entry point shared by the CLI and the tests, so both see
 * exactly the same ranking, deduplication, and fallback behaviour.
 *
 * Returned vector:
 *   [0]   the primary record
 *   [1..] alternatives, ranked, with duplicate surface splits removed
 *
 * It is never empty: an input with no letters yields one passthrough record,
 * and a word with no analyses yields one fallback record.
 */
class WordSegmenter {
private:
    Analyzer* analyzer_;
    Segmenter segmenter_;

public:
    WordSegmenter(Analyzer* analyzer, const LexiconFSA* lexicon)
        : analyzer_(analyzer), segmenter_(lexicon) {}

    std::vector<Segmentation> segment_word(const std::string& word) const {
        if (Segmenter::is_passthrough_input(word)) {
            return { Segmenter::passthrough(word) };
        }

        auto analyses = analyzer_->analyze(word);
        if (analyses.empty()) {
            return { Segmenter::fallback(word) };
        }

        const std::string lower = Segmenter::ascii_lower(word);

        std::vector<Segmentation> ranked;
        ranked.reserve(analyses.size());
        for (const auto& a : analyses) {
            const bool irregular =
                analyzer_->is_irregular_form(lower, a.stem(), a.features());
            Segmentation seg = segmenter_.from_analysis(word, a, irregular);
            // n_analyses counts what the analyzer returned for the word, not
            // the number of distinct surface splits, which can be smaller.
            seg.n_analyses = analyses.size();
            ranked.push_back(seg);
        }

        std::sort(ranked.begin(), ranked.end(), segmentation_less);

        // Two analyses can describe the same surface split: "unkind" is
        // un- + kind as both a noun and an adjective, and POS does not appear
        // in this output. Collapse those so alternatives carry real variation.
        std::vector<Segmentation> deduped;
        for (const auto& seg : ranked) {
            bool seen = false;
            for (const auto& kept : deduped) {
                if (same_segmentation(kept, seg)) { seen = true; break; }
            }
            if (!seen) deduped.push_back(seg);
        }

        return deduped;
    }
};

#endif // SEGMENT_DRIVER_H
