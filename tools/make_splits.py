#!/usr/bin/env python3
"""Build the shared evaluation splits and the pretokenized training file.

Everything downstream reads what this writes, so the corpus is read once and
the units every system sees are fixed here.

Test side, from `test.txt`:

    test_words.txt.gz the alphabetic word tokens, in order, one per line
    test_types.tsv    word, count: the same tokens as a type list
    test_units.tsv    unit, count: every unit including punctuation and
                      digits, which is what the pieces per 1000 bytes
                      figure is computed over
    test_stats.json   token and type counts, raw byte size, alphabet

Train side, from `train_100M.txt`:

    train_pretok.txt.gz  one line per input line, units joined by single
                      spaces
    train_stats.json  line and unit counts, alphabet

The training file exists because the tokenizer libraries each bring their own
splitting rule. Feeding them text that is already split, with their own
pre-tokenizer set to plain whitespace, is the only way to be sure all three
trained on the same units.

The alphabet in each stats file is there to answer one question before any
training runs: whether a character can appear in the test text that the
training text never showed, which is what decides whether a tokenizer without
byte fallback can round-trip.

The two large outputs are gzip compressed, and the corpora are read through
`existing`, which finds them whether or not they have been compressed since.
See `open_text` in tools/pretokenize.py for why. `raw_bytes` is the size of
the uncompressed text either way, since it is counted while reading rather
than taken from the directory entry.

Standard library only.
"""

import argparse
import collections
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pretokenize import (UNIT_RE, WORD_RE, existing,  # noqa: E402
                         open_text)

REPORT_EVERY = 1000000


def expand(path):
    return os.path.expanduser(path)


def build_test(path, out_dir):
    """Write the three test split files and return their stats."""
    find = UNIT_RE.findall
    is_word = WORD_RE.match

    word_counts = collections.Counter()
    unit_counts = collections.Counter()
    alphabet = set()
    lines = 0
    word_tokens = 0
    unit_tokens = 0

    raw_bytes = 0
    words_path = os.path.join(out_dir, "test_words.txt.gz")
    with open_text(existing(path)) as source, \
            open_text(words_path, "w") as words_out:
        for line in source:
            lines += 1
            raw_bytes += len(line.encode("utf-8"))
            alphabet.update(line)
            for unit in find(line):
                unit_counts[unit] += 1
                unit_tokens += 1
                if is_word(unit):
                    words_out.write(unit + "\n")
                    word_counts[unit] += 1
                    word_tokens += 1
            if lines % REPORT_EVERY == 0:
                print("  %s: %d lines" % (os.path.basename(path), lines),
                      file=sys.stderr)

    write_counts(os.path.join(out_dir, "test_types.tsv"), word_counts)
    write_counts(os.path.join(out_dir, "test_units.tsv"), unit_counts)

    alphabet.discard("\n")
    return {
        "source": path,
        "raw_bytes": raw_bytes,
        "lines": lines,
        "word_tokens": word_tokens,
        "word_types": len(word_counts),
        "unit_tokens": unit_tokens,
        "unit_types": len(unit_counts),
        "alphabet_size": len(alphabet),
        "alphabet": "".join(sorted(alphabet)),
    }


def build_train(path, out_dir):
    """Write the pretokenized training file and return its stats."""
    find = UNIT_RE.findall
    alphabet = set()
    lines = 0
    unit_tokens = 0
    types = set()

    raw_bytes = 0
    out_path = os.path.join(out_dir, "train_pretok.txt.gz")
    with open_text(existing(path)) as source, open_text(out_path, "w") as out:
        for line in source:
            lines += 1
            raw_bytes += len(line.encode("utf-8"))
            alphabet.update(line)
            units = find(line)
            unit_tokens += len(units)
            types.update(units)
            out.write(" ".join(units))
            out.write("\n")
            if lines % REPORT_EVERY == 0:
                print("  %s: %d lines" % (os.path.basename(path), lines),
                      file=sys.stderr)

    alphabet.discard("\n")
    return {
        "source": path,
        "raw_bytes": raw_bytes,
        "pretokenized": out_path,
        "pretokenized_bytes": os.path.getsize(out_path),
        "lines": lines,
        "unit_tokens": unit_tokens,
        "unit_types": len(types),
        "alphabet_size": len(alphabet),
        "alphabet": "".join(sorted(alphabet)),
    }


def write_counts(path, counts):
    """Write a count table, most frequent first, ties broken alphabetically."""
    with open(path, "w", encoding="utf-8") as out:
        for item, count in sorted(counts.items(), key=lambda kv: (-kv[1], kv[0])):
            out.write("%s\t%d\n" % (item, count))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--test", default="~/data/corpora/babylm/test.txt")
    parser.add_argument("--train", default="~/data/corpora/babylm/train_100M.txt")
    parser.add_argument("--out", default="~/data/tok/splits")
    parser.add_argument("--skip-test", action="store_true")
    parser.add_argument("--skip-train", action="store_true")
    args = parser.parse_args(argv)

    out_dir = expand(args.out)
    os.makedirs(out_dir, exist_ok=True)

    if not args.skip_test:
        print("reading test split", file=sys.stderr)
        stats = build_test(expand(args.test), out_dir)
        save(os.path.join(out_dir, "test_stats.json"), stats)
        print("  %d word tokens, %d types, %d unit tokens, alphabet %d"
              % (stats["word_tokens"], stats["word_types"],
                 stats["unit_tokens"], stats["alphabet_size"]), file=sys.stderr)

    if not args.skip_train:
        print("reading train split", file=sys.stderr)
        stats = build_train(expand(args.train), out_dir)
        save(os.path.join(out_dir, "train_stats.json"), stats)
        print("  %d lines, %d unit tokens, %d unit types, alphabet %d"
              % (stats["lines"], stats["unit_tokens"], stats["unit_types"],
                 stats["alphabet_size"]), file=sys.stderr)

    return 0


def save(path, stats):
    with open(path, "w", encoding="utf-8") as out:
        json.dump(stats, out, indent=2, ensure_ascii=False)
        out.write("\n")


if __name__ == "__main__":
    sys.exit(main())
