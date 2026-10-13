#!/usr/bin/env python3
"""Per-suffix agreement with MorphoLex, and the recall gap broken down.

    python3 tools/suffix_report.py \\
        --lexicon data/closed_class.tsv \\
        --lexicon ~/data/morph/lexicons/en_100k.tsv \\
        --derived-first

Two questions, both about the derivational rule set in
RULES/derivation_rule.h and both asked against MorphoLex, which is the
independent gold.

**Which suffixes earn their place.** A suffix is credited with a MorphoLex
form when the analyzer's primary reading of that form ends in it, and scored
on the boundaries that reading predicts. A suffix whose own splits are wrong
more often than one in five is not worth having: the inventory is there to
raise recall, and a rule that lowers precision while doing it can be dropped
or restricted instead. Three were dropped on this measurement and two were
restricted; docs/LEXICON.md says which and why.

**Where the rest of the recall gap is.** Every gold boundary the analyzer
misses is put in one of five buckets: a compound, a word it cannot analyze at
all, an affix the rule set does not have, the inner layer of a multi-layer
form, or an affix it does have and did not strip. Compounds are tested first,
since `play|house` is a compound whether or not the analyzer reached the word.

With `--before PATH`, a second analyzer binary is run over the same words and
each suffix also reports the most frequent test words it newly splits. That is
how the "newly split" lists in docs/BASELINES.md were taken, with PATH built
from the commit before the suffixes were added.

Standard library, and the gold readers come from tools/eval_lexicon.py.
"""

import argparse
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from eval_lexicon import read_morpholex  # noqa: E402
from fst_tokenizer import FstTokenizer  # noqa: E402
from score_segmentation import (internal_boundaries, match_boundaries,  # noqa: E402
                                prf)

DEFAULT_MORPHOLEX = "~/data/morph/morpholex/MorphoLex-en/MorphoLEX_en.xlsx"
DEFAULT_TYPES = "~/data/tok/splits/test_types.tsv"
DEFAULT_JSON = "~/data/tok/results/suffix_report.json"

# The suffix inventory, as RULES/derivation_rule.h lists it. Kept here rather
# than parsed out of the header: a report that silently stops covering a
# suffix because a regex missed it would be worse than one that has to be
# edited alongside the rules.
SUFFIXES = ["ness", "ly", "er", "tion", "ment",
            "able", "al", "ful", "less", "ous", "ic", "ive", "ish", "y",
            "ity", "ism", "ist", "ion", "ation", "ance", "ence",
            "hood", "ship", "dom", "ize", "ise", "ify", "en"]

# Longest first, so that "ation" is tried before "ion"
BY_LENGTH = sorted(SUFFIXES, key=len, reverse=True)

# The inflectional suffixes, for the gap breakdown only
INFLECTIONAL = ["s", "es", "ed", "d", "ing", "est", "ies", "ied"]

PREFIXES = ["un", "re", "pre", "dis", "mis", "over", "under", "non", "anti",
            "de", "out", "up", "sub", "inter", "co"]


def expand(path):
    return os.path.expanduser(path) if path else path


def read_counts(path):
    """Read a `item<TAB>count` table, for ordering examples by frequency."""
    counts = {}
    if not path or not os.path.exists(path):
        return counts
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            item, _, count = line.rstrip("\n").partition("\t")
            counts[item] = int(count)
    return counts


def attributed_suffix(record):
    """Which suffix the last piece of this record is, or None.

    The segmentation record carries pieces rather than morphs, so the last
    piece is matched against the inventory: exactly, or allowing one leading
    character that an alternation displaced out of the stem, which is what
    makes `iness` the suffix -ness and `ner` the suffix -er.
    """
    if len(record["pieces"]) < 2:
        return None
    if record["labels"][-1] != "SUFFIX_DERIV":
        return None
    piece = record["pieces"][-1].lower()
    for suffix in BY_LENGTH:
        if piece == suffix:
            return suffix
    for suffix in BY_LENGTH:
        if len(piece) == len(suffix) + 1 and piece[1:] == suffix:
            return suffix
    return None


def best_gold(gold_pieces, predicted):
    """The gold reading that agrees best with the prediction, and its score."""
    best = None
    chosen = None
    for pieces in gold_pieces:
        scored = match_boundaries(predicted, internal_boundaries(pieces), 0)
        if best is None or scored[0] > best[0]:
            best = scored
            chosen = pieces
    return chosen, best


def segment(analyzer, words, extra):
    """One record per word, from one analyzer process."""
    with FstTokenizer(analyzer, extra_args=extra) as tok:
        return tok.segment_many(words)


def per_suffix(gold, records, counts, before):
    """Boundary scores for each suffix, over the forms credited to it."""
    stats = collections.defaultdict(collections.Counter)
    disagree = collections.defaultdict(list)
    newly = collections.defaultdict(list)

    for word, record in records.items():
        suffix = attributed_suffix(record)
        if suffix is None:
            continue
        if before is not None and before[word]["pieces"] != record["pieces"]:
            newly[suffix].append((counts.get(word, 0), word,
                                  "|".join(before[word]["pieces"]),
                                  "|".join(record["pieces"])))
        if word not in gold:
            continue
        predicted = internal_boundaries(record["pieces"])
        pieces, scored = best_gold(gold[word], predicted)
        counter = stats[suffix]
        counter["forms"] += 1
        counter["tp"] += scored[0]
        counter["fp"] += scored[1]
        counter["fn"] += scored[2]
        if tuple(record["pieces"]) in gold[word]:
            counter["exact"] += 1
        else:
            disagree[suffix].append((counts.get(word, 0), word,
                                     "|".join(record["pieces"]),
                                     "|".join(pieces)))
    return stats, disagree, newly


def recall_gap(gold, records, is_word):
    """Every missed gold boundary, in one of five buckets."""
    buckets = collections.Counter()
    examples = collections.defaultdict(list)
    layers = collections.Counter()

    for word, record in records.items():
        if word not in gold:
            continue
        predicted = internal_boundaries(record["pieces"])
        pieces, scored = best_gold(gold[word], predicted)

        if len(pieces) >= 3:
            layers["gold_three_or_more_morphemes"] += 1
            if len(record["pieces"]) >= 3:
                layers["analyzer_three_or_more_pieces"] += 1
            if tuple(record["pieces"]) in gold[word]:
                layers["analyzer_exact"] += 1

        if scored[2] == 0:
            continue

        offsets = []
        at = 0
        for piece in pieces[:-1]:
            at += len(piece)
            offsets.append(at)

        for i, offset in enumerate(offsets):
            if offset in predicted:
                continue
            before, after = pieces[i], pieces[i + 1]
            if (is_word(before) and is_word(after) and
                    len(before) >= 3 and len(after) >= 3 and
                    after not in SUFFIXES + INFLECTIONAL and
                    before not in PREFIXES):
                key = "compound"
            elif record["source"] != "fst":
                key = "no_analysis_of_the_word"
            elif (after not in SUFFIXES + INFLECTIONAL and
                  before not in PREFIXES):
                key = "affix_the_rules_do_not_have"
            elif len(pieces) >= 3 and len(record["pieces"]) < len(pieces):
                key = "inner_layer_of_a_multi_layer_form"
            else:
                key = "known_affix_not_stripped"
            buckets[key] += 1
            if len(examples[key]) < 20:
                examples[key].append("%s %s vs gold %s"
                                     % (word, "|".join(record["pieces"]),
                                        "|".join(pieces)))
    return buckets, examples, layers


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--analyzer", default="./analyzer")
    parser.add_argument("--before", default=None,
                        help="a second analyzer binary, to report what each "
                             "suffix newly splits")
    parser.add_argument("--lexicon", action="append", default=[],
                        metavar="FILE", help="repeatable, as the analyzer "
                                             "takes it")
    parser.add_argument("--derived-first", action="store_true")
    parser.add_argument("--morpholex", default=DEFAULT_MORPHOLEX)
    parser.add_argument("--types", default=DEFAULT_TYPES,
                        help="test type counts, for ordering examples")
    parser.add_argument("--json", default=DEFAULT_JSON,
                        help="write every number here; '' to write nothing")
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args(argv)

    extra = []
    for lexicon in args.lexicon:
        extra += ["--lexicon", expand(lexicon)]
    if args.derived_first:
        extra.append("--derived-first")

    gold, gold_stats = read_morpholex(expand(args.morpholex))
    counts = read_counts(expand(args.types))
    print("morpholex: %d scorable forms, %d test types"
          % (len(gold), len(counts)), file=sys.stderr)

    # Every test type as well as every gold form, so that the newly-split
    # lists can be ordered by how often the word actually occurs.
    words = sorted(set(gold) | set(counts))
    records = dict(zip(words, segment(args.analyzer, words, extra)))
    before = None
    if args.before:
        before = dict(zip(words, segment(args.before, words, extra)))

    # Is a gold morph a free-standing word? Asked of the analyzer rather than
    # guessed, over the distinct morphs only.
    morphs = sorted({m for pieces in gold.values() for p in pieces for m in p})
    morph_records = dict(zip(morphs, segment(args.analyzer, morphs, extra)))

    def is_word(text):
        record = morph_records.get(text)
        return (record is not None and record["source"] == "fst" and
                len(record["pieces"]) == 1)

    stats, disagree, newly = per_suffix(gold, records, counts, before)
    buckets, examples, layers = recall_gap(gold, records, is_word)

    print("\n== MorphoLex agreement per suffix ==\n")
    print("%-8s %10s %7s %7s %7s %7s %6s" %
          ("suffix", "gold forms", "P", "R", "F1", "exact", "newly"))
    rows = {}
    for suffix in SUFFIXES:
        counter = stats[suffix]
        precision, recall, f1 = prf(counter["tp"], counter["fp"],
                                    counter["fn"])
        rows[suffix] = {
            "forms": counter["forms"], "precision": precision,
            "recall": recall, "f1": f1, "exact": counter["exact"],
            "tp": counter["tp"], "fp": counter["fp"], "fn": counter["fn"],
            "newly_split": len(newly[suffix]),
        }
        print("%-8s %10d %7.3f %7.3f %7.3f %7d %6d"
              % (suffix, counter["forms"], precision, recall, f1,
                 counter["exact"], len(newly[suffix])))

    total = sum(buckets.values())
    print("\n== The recall gap: %d missed gold boundaries ==\n" % total)
    for key, count in buckets.most_common():
        print("  %-36s %6d  %5.1f%%"
              % (key, count, 100.0 * count / total if total else 0.0))
    print()
    for key, count in sorted(layers.items()):
        print("  %-36s %6d" % (key, count))

    if before is not None:
        print("\n== Newly split, by test frequency ==")
        for suffix in SUFFIXES:
            rows_for = sorted(newly[suffix], reverse=True)[:args.top]
            print("\n-%s: %d words" % (suffix, len(newly[suffix])))
            for count, word, was, now in rows_for:
                print("   %8d  %-22s %-26s %s" % (count, word, was, now))

    print("\n== Where MorphoLex disagrees, by test frequency ==")
    for suffix in SUFFIXES:
        rows_for = sorted(disagree[suffix], reverse=True)[:10]
        print("\n-%s: %d of %d gold forms"
              % (suffix, len(disagree[suffix]), stats[suffix]["forms"]))
        for count, word, predicted, gold_pieces in rows_for:
            print("   %8d  %-22s %-26s gold %s"
                  % (count, word, predicted, gold_pieces))

    if args.json:
        path = expand(args.json)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        payload = {
            "lexicons": args.lexicon,
            "derived_first": args.derived_first,
            "morpholex_stats": dict(gold_stats),
            "per_suffix": rows,
            "recall_gap": dict(buckets),
            "multi_layer": dict(layers),
            "disagreements": {s: [list(r) for r in sorted(disagree[s],
                                                          reverse=True)[:10]]
                              for s in SUFFIXES},
            "newly_split": {s: [list(r) for r in sorted(newly[s],
                                                        reverse=True)[:args.top]]
                            for s in SUFFIXES},
        }
        with open(path, "w", encoding="utf-8") as out:
            json.dump(payload, out, indent=2, ensure_ascii=False)
            out.write("\n")
        print("\nwrote %s" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
