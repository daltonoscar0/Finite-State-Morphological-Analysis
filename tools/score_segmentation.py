#!/usr/bin/env python3
"""Score the analyzer's surface segmentation.

Two kinds of number are reported:

  Intrinsic, needing no gold standard: round-trip exactness, coverage (the
  share of words the analyzer actually analyzed), the share of words landing
  in each gate, fertility, and piece size in characters and bytes.

  Against a gold standard: exact match rate, and precision, recall and F1 over
  internal boundaries, micro-averaged and broken down by gate.

Usage:
    python3 tools/score_segmentation.py --gold data/gold_sample.tsv
    python3 tools/score_segmentation.py --gold data/gold_sample.tsv \\
        --words corpus_words.txt --json scores.json --alt-tolerant

The gold file is TSV, one entry per line:

    cities<TAB>cit|ies

Lines that are blank or start with # are ignored. A gold entry whose pieces do
not concatenate to its word is reported as malformed and excluded.

Exit status is 1 if any record fails to round-trip, since every downstream
metric assumes the pieces rebuild the input.

Standard library only.
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from fst_tokenizer import AnalyzerError, FstTokenizer  # noqa: E402


GATES = ["decomposed", "lexicon_stem", "irregular", "no_analysis", "passthrough"]


# ── Inputs ────────────────────────────────────────────────────────────────

def read_gold(path):
    """Read a gold TSV.

    Returns:
        (entries, malformed) where entries maps word -> list of pieces, and
        malformed is a list of (line number, line, reason).
    """
    entries, malformed = {}, []
    with open(path, "r", encoding="utf-8") as handle:
        for lineno, raw in enumerate(handle, 1):
            line = raw.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != 2:
                malformed.append((lineno, line, "expected exactly one tab"))
                continue
            word, pieces_field = parts[0], parts[1]
            pieces = pieces_field.split("|")
            if any(p == "" for p in pieces):
                malformed.append((lineno, line, "empty piece"))
                continue
            if "".join(pieces) != word:
                malformed.append(
                    (lineno, line, "pieces rebuild %r, not %r"
                     % ("".join(pieces), word)))
                continue
            entries[word] = pieces
    return entries, malformed


def read_words(path):
    """Read a plain word list, one per line, keeping order and duplicates."""
    words = []
    with open(path, "r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            if line.strip() or line == "":
                words.append(line)
    return words


# ── Boundaries ────────────────────────────────────────────────────────────

def internal_boundaries(pieces):
    """Character offsets strictly inside a word, one per piece seam.

    A word split into N pieces has N-1 internal boundaries. The outer edges,
    0 and the word length, are excluded: every segmentation agrees on them, so
    counting them would inflate precision and recall.
    """
    bounds, running = [], 0
    for piece in pieces[:-1]:
        running += len(piece)
        bounds.append(running)
    return bounds


def match_boundaries(predicted, gold, tolerance):
    """Match predicted boundaries to gold boundaries one to one.

    With a tolerance of 0 this is set intersection. With a tolerance of 1 a
    predicted boundary may claim a gold boundary one character away, which is
    what --alt-tolerant allows for records involving an orthographic
    alternation. Matching is one to one, so two predicted boundaries can never
    both score against the same gold boundary.

    Returns:
        (true_positives, false_positives, false_negatives)
    """
    unclaimed = sorted(gold)
    used = [False] * len(unclaimed)
    true_positives = 0

    for p in sorted(predicted):
        best, best_distance = None, None
        for i, g in enumerate(unclaimed):
            if used[i]:
                continue
            distance = abs(g - p)
            if distance <= tolerance and (best_distance is None or
                                          distance < best_distance):
                best, best_distance = i, distance
        if best is None:
            continue
        used[best] = True
        true_positives += 1

    return (true_positives,
            len(predicted) - true_positives,
            len(gold) - true_positives)


def prf(true_positives, false_positives, false_negatives):
    """Precision, recall, F1. Each is 0.0 when undefined."""
    p_den = true_positives + false_positives
    r_den = true_positives + false_negatives
    precision = true_positives / p_den if p_den else 0.0
    recall = true_positives / r_den if r_den else 0.0
    f1 = (2 * precision * recall / (precision + recall)
          if precision + recall else 0.0)
    return precision, recall, f1


# ── Scoring ───────────────────────────────────────────────────────────────

def intrinsic_scores(records):
    """Metrics computable without a gold standard."""
    total = len(records)
    if total == 0:
        return {"words": 0}

    round_trip_ok, offenders = 0, []
    fst = 0
    gate_counts = {gate: 0 for gate in GATES}
    pieces_total = 0
    chars_total = 0
    bytes_total = 0

    for record in records:
        word = record["word"]
        pieces = record["pieces"]

        if "".join(pieces) == word:
            round_trip_ok += 1
        elif len(offenders) < 10:
            offenders.append((word, pieces))

        if record["source"] == "fst":
            fst += 1
        gate_counts[record["gate"]] = gate_counts.get(record["gate"], 0) + 1

        pieces_total += len(pieces)
        chars_total += sum(len(p) for p in pieces)
        bytes_total += sum(len(p.encode("utf-8", "surrogateescape"))
                           for p in pieces)

    return {
        "words": total,
        "round_trip_exact": round_trip_ok,
        "round_trip_rate": round_trip_ok / total,
        "round_trip_offenders": offenders,
        "coverage_fst": fst,
        "coverage_rate": fst / total,
        "gate_counts": gate_counts,
        "gate_share": {g: c / total for g, c in gate_counts.items()},
        "pieces": pieces_total,
        "fertility": pieces_total / total,
        "chars_per_piece": chars_total / pieces_total if pieces_total else 0.0,
        "bytes_per_piece": bytes_total / pieces_total if pieces_total else 0.0,
        "chars_per_word": chars_total / total,
        "bytes_per_word": bytes_total / total,
    }


def gold_scores(records_by_word, gold, alt_tolerant):
    """Metrics against a gold standard.

    Args:
        records_by_word: word -> analyzer record.
        gold: word -> gold pieces.
        alt_tolerant: Allow a one-character slip where the record reports an
            orthographic alternation.

    Returns:
        A dict with micro-averaged scores, a per-gate breakdown, and the
        disagreements, most useful first.
    """
    micro = {"tp": 0, "fp": 0, "fn": 0}
    by_gate = {}
    exact, scored, missing = 0, 0, []
    disagreements = []

    for word, gold_pieces in sorted(gold.items()):
        record = records_by_word.get(word)
        if record is None:
            missing.append(word)
            continue

        scored += 1
        predicted = record["pieces"]
        gate = record["gate"]
        tolerance = 1 if (alt_tolerant and record.get("alt")) else 0

        is_exact = predicted == gold_pieces
        if is_exact:
            exact += 1

        tp, fp, fn = match_boundaries(internal_boundaries(predicted),
                                      internal_boundaries(gold_pieces),
                                      tolerance)
        micro["tp"] += tp
        micro["fp"] += fp
        micro["fn"] += fn

        bucket = by_gate.setdefault(gate, {"tp": 0, "fp": 0, "fn": 0,
                                           "words": 0, "exact": 0})
        bucket["tp"] += tp
        bucket["fp"] += fp
        bucket["fn"] += fn
        bucket["words"] += 1
        bucket["exact"] += 1 if is_exact else 0

        if not is_exact:
            disagreements.append({
                "word": word,
                "predicted": "|".join(predicted),
                "gold": "|".join(gold_pieces),
                "gate": gate,
                "alt": record.get("alt", ""),
            })

    precision, recall, f1 = prf(micro["tp"], micro["fp"], micro["fn"])

    gate_table = {}
    for gate, bucket in by_gate.items():
        p, r, f = prf(bucket["tp"], bucket["fp"], bucket["fn"])
        gate_table[gate] = {
            "words": bucket["words"],
            "exact": bucket["exact"],
            "exact_rate": bucket["exact"] / bucket["words"],
            "tp": bucket["tp"], "fp": bucket["fp"], "fn": bucket["fn"],
            # Single-piece gates contribute no internal boundaries at all, so
            # precision, recall and F1 are undefined rather than zero. The
            # count lets a consumer tell the two cases apart.
            "boundaries": bucket["tp"] + bucket["fp"] + bucket["fn"],
            "precision": p, "recall": r, "f1": f,
        }

    return {
        "gold_entries": len(gold),
        "scored": scored,
        "missing_from_analyzer": missing,
        "exact_match": exact,
        "exact_match_rate": exact / scored if scored else 0.0,
        "alt_tolerant": bool(alt_tolerant),
        "boundary_tp": micro["tp"],
        "boundary_fp": micro["fp"],
        "boundary_fn": micro["fn"],
        "boundary_precision": precision,
        "boundary_recall": recall,
        "boundary_f1": f1,
        "by_gate": gate_table,
        "disagreements": disagreements,
    }


# ── Reporting ─────────────────────────────────────────────────────────────

def print_table(intrinsic, gold, malformed):
    """Print the text report."""
    width = 72
    print("=" * width)
    print("Surface segmentation scores")
    print("=" * width)

    print("\nIntrinsic")
    print("-" * width)
    print("  words scored                %d" % intrinsic["words"])
    print("  round-trip exact            %d / %d  (%.4f)" % (
        intrinsic["round_trip_exact"], intrinsic["words"],
        intrinsic["round_trip_rate"]))
    print("  coverage, source fst        %d / %d  (%.4f)" % (
        intrinsic["coverage_fst"], intrinsic["words"],
        intrinsic["coverage_rate"]))
    print("  fertility, pieces per word  %.4f" % intrinsic["fertility"])
    print("  characters per piece        %.4f" % intrinsic["chars_per_piece"])
    print("  bytes per piece             %.4f" % intrinsic["bytes_per_piece"])
    print("  characters per word         %.4f" % intrinsic["chars_per_word"])
    print("  bytes per word              %.4f" % intrinsic["bytes_per_word"])

    print("\nGate shares")
    print("-" * width)
    print("  %-16s %8s  %8s" % ("gate", "words", "share"))
    for gate in GATES:
        count = intrinsic["gate_counts"].get(gate, 0)
        print("  %-16s %8d  %8.4f" % (gate, count,
                                      intrinsic["gate_share"].get(gate, 0.0)))

    if malformed:
        print("\nMalformed gold lines")
        print("-" * width)
        for lineno, line, reason in malformed[:10]:
            print("  line %d: %s  (%s)" % (lineno, line, reason))
        if len(malformed) > 10:
            print("  ... and %d more" % (len(malformed) - 10))

    if gold is None:
        print()
        return

    print("\nAgainst gold%s" % (", alternation tolerant"
                                if gold["alt_tolerant"] else ""))
    print("-" * width)
    print("  gold entries                %d" % gold["gold_entries"])
    print("  scored                      %d" % gold["scored"])
    print("  exact match                 %d / %d  (%.4f)" % (
        gold["exact_match"], gold["scored"], gold["exact_match_rate"]))
    print("  boundary tp / fp / fn       %d / %d / %d" % (
        gold["boundary_tp"], gold["boundary_fp"], gold["boundary_fn"]))
    print("  boundary precision          %.4f" % gold["boundary_precision"])
    print("  boundary recall             %.4f" % gold["boundary_recall"])
    print("  boundary F1                 %.4f" % gold["boundary_f1"])

    print("\nBy gate")
    print("-" * width)
    print("  %-14s %6s %6s %6s %6s %6s %7s %7s %7s" % (
        "gate", "words", "exact", "tp", "fp", "fn", "P", "R", "F1"))
    for gate in GATES:
        row = gold["by_gate"].get(gate)
        if not row:
            continue
        if row["boundaries"]:
            scores = "%7.4f %7.4f %7.4f" % (row["precision"], row["recall"],
                                            row["f1"])
        else:
            # No internal boundaries on either side, so the three scores are
            # undefined here, not zero.
            scores = "%7s %7s %7s" % ("n/a", "n/a", "n/a")
        print("  %-14s %6d %6d %6d %6d %6d %s" % (
            gate, row["words"], row["exact"], row["tp"], row["fp"],
            row["fn"], scores))

    if gold["missing_from_analyzer"]:
        print("\n  gold words with no record: %s" %
              ", ".join(gold["missing_from_analyzer"][:10]))

    if gold["disagreements"]:
        print("\nDisagreements")
        print("-" * width)
        print("  %-16s %-22s %-22s %s" % ("word", "predicted", "gold", "alt"))
        for row in gold["disagreements"][:25]:
            print("  %-16s %-22s %-22s %s" % (
                row["word"], row["predicted"], row["gold"], row["alt"]))
        if len(gold["disagreements"]) > 25:
            print("  ... and %d more" % (len(gold["disagreements"]) - 25))
    print()


# ── Entry point ───────────────────────────────────────────────────────────

def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Score the analyzer's surface segmentation output.")
    parser.add_argument("--gold", required=True,
                        help="gold TSV: word<TAB>piece|piece|piece")
    parser.add_argument("--words",
                        help="optional unlabeled word list for the intrinsic "
                             "metrics; defaults to the gold words")
    parser.add_argument("--analyzer", default="./analyzer",
                        help="path to the analyzer binary (default ./analyzer)")
    parser.add_argument("--json", dest="json_path",
                        help="also write all scores to this path as JSON")
    parser.add_argument("--alt-tolerant", action="store_true",
                        help="accept a predicted boundary within one character "
                             "of a gold boundary when the record reports an "
                             "orthographic alternation")
    args = parser.parse_args(argv)

    gold_entries, malformed = read_gold(args.gold)
    if not gold_entries and not malformed:
        print("error: no usable gold entries in %s" % args.gold,
              file=sys.stderr)
        return 2

    if args.words:
        words = read_words(args.words)
    else:
        words = sorted(gold_entries)

    try:
        with FstTokenizer(args.analyzer) as tokenizer:
            records = tokenizer.segment_many(words)
            # When a separate word list is given, the gold words still need
            # records of their own for the boundary metrics.
            gold_only = [w for w in sorted(gold_entries) if w not in set(words)]
            gold_records = tokenizer.segment_many(gold_only) if gold_only else []
    except AnalyzerError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2

    by_word = {r["word"]: r for r in records}
    by_word.update({r["word"]: r for r in gold_records})

    intrinsic = intrinsic_scores(records)
    gold = gold_scores(by_word, gold_entries, args.alt_tolerant)

    print_table(intrinsic, gold, malformed)

    if args.json_path:
        payload = {
            "analyzer": os.path.abspath(args.analyzer),
            "gold_file": os.path.abspath(args.gold),
            "words_file": os.path.abspath(args.words) if args.words else None,
            "intrinsic": intrinsic,
            "gold": gold,
            "malformed_gold": [
                {"line": lineno, "text": text, "reason": reason}
                for lineno, text, reason in malformed
            ],
        }
        with open(args.json_path, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, indent=2, sort_keys=True,
                      ensure_ascii=False)
            handle.write("\n")
        print("Wrote JSON to %s" % args.json_path)

    failures = intrinsic["words"] - intrinsic["round_trip_exact"]
    if failures:
        print("FAIL: %d of %d records did not round-trip" % (
            failures, intrinsic["words"]), file=sys.stderr)
        for word, pieces in intrinsic["round_trip_offenders"]:
            print("  %r -> %r" % (word, pieces), file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
