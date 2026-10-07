#!/usr/bin/env python3
"""Write a word frequency table for the lexicon builder.

Output is two tab-separated columns, word and Zipf frequency, sorted by
frequency descending:

    the     7.73
    of      7.29
    dictionary      4.02

The Zipf scale is log10 of occurrences per billion words, so 7 is one word in
ten thousand and 1 is one in a billion. It compresses a very long tail into a
small range, which is what the weight scale in docs/LEXICON.md needs.

Frequencies come from the `wordfreq` package, which has to be installed
somewhere outside this repository:

    python3 -m venv ~/data/morph/.venv
    ~/data/morph/.venv/bin/pip install wordfreq
    ~/data/morph/.venv/bin/python tools/make_freq.py \\
        --wiktionary ~/data/morph/wiktionary/wikt_en.jsonl \\
        --out ~/data/morph/freq_en.tsv

If wordfreq is not importable, pass --corpus and frequencies are counted from
that file instead. The fallback is noted in the output header and on stderr,
because counts from one corpus are not interchangeable with wordfreq's blend
of several.

The vocabulary is the union of wordfreq's own top-N list and every word in the
files given with --wiktionary and --words, so the table covers exactly the
words the lexicon builder will ask about. A word wordfreq has never seen gets
no row, and the builder treats a missing row as the floor of the scale.
"""

import argparse
import json
import math
import os
import re
import sys
from collections import Counter

WORD_RE = re.compile(r"^[a-z]+$")
TOKEN_RE = re.compile(r"[a-z]+")


def words_from_wiktionary(path):
    """Yield the headword of every line of a wikt_en.jsonl dump."""
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            try:
                word = json.loads(line).get("word", "")
            except json.JSONDecodeError:
                continue
            if WORD_RE.match(word):
                yield word


def words_from_list(path):
    """Yield each non-blank line of a plain word list."""
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            word = line.strip()
            if WORD_RE.match(word):
                yield word


def zipf_from_wordfreq(vocabulary, top_n):
    """Look up the Zipf frequency of every word, via the wordfreq package.

    Returns (rows, source) where rows is a dict of word to Zipf, or raises
    ImportError if the package is not available.
    """
    from wordfreq import top_n_list, zipf_frequency

    vocabulary = set(vocabulary)
    if top_n:
        vocabulary.update(w for w in top_n_list("en", top_n) if WORD_RE.match(w))

    rows = {}
    for word in vocabulary:
        zipf = zipf_frequency(word, "en")
        if zipf > 0:
            rows[word] = zipf
    return rows, "wordfreq"


def zipf_from_corpus(vocabulary, corpus_path):
    """Count words in a corpus file and convert the counts to the Zipf scale.

    Zipf is log10(occurrences per billion), so a word seen `n` times in a
    corpus of `total` tokens scores log10(n / total * 1e9).
    """
    counts = Counter()
    total = 0
    with open(corpus_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            for token in TOKEN_RE.findall(line.lower()):
                counts[token] += 1
                total += 1
    if total == 0:
        sys.exit(f"error: {corpus_path} contains no words")

    keep = set(vocabulary) if vocabulary else set(counts)
    rows = {}
    for word, count in counts.items():
        if word not in keep:
            continue
        rows[word] = round(math.log10(count / total * 1e9), 2)
    return rows, f"corpus counts from {os.path.basename(corpus_path)}"


def main():
    parser = argparse.ArgumentParser(
        description="Write a word frequency table as word<TAB>zipf")
    parser.add_argument("--out", required=True,
                        help="output path, outside this repository")
    parser.add_argument("--wiktionary", action="append", default=[],
                        metavar="FILE",
                        help="wikt_en.jsonl dump to take headwords from; "
                             "repeatable")
    parser.add_argument("--words", action="append", default=[], metavar="FILE",
                        help="plain word list, one per line; repeatable")
    parser.add_argument("--top", type=int, default=500000,
                        help="also include wordfreq's top N English words "
                             "(default 500000, 0 to skip)")
    parser.add_argument("--corpus", metavar="FILE",
                        help="count frequencies from this file instead, used "
                             "automatically if wordfreq cannot be imported")
    args = parser.parse_args()

    vocabulary = set()
    for path in args.wiktionary:
        vocabulary.update(words_from_wiktionary(path))
    for path in args.words:
        vocabulary.update(words_from_list(path))
    print(f"vocabulary from input files: {len(vocabulary)}", file=sys.stderr)

    rows, source = None, None
    try:
        rows, source = zipf_from_wordfreq(vocabulary, args.top)
    except ImportError:
        if not args.corpus:
            sys.exit("error: wordfreq is not importable and no --corpus was "
                     "given.\n"
                     "  Install it outside the repository:\n"
                     "    python3 -m venv ~/data/morph/.venv\n"
                     "    ~/data/morph/.venv/bin/pip install wordfreq\n"
                     "  or pass --corpus FILE to count from a corpus instead.")
        print("note: wordfreq is not importable, counting from the corpus "
              "instead. These numbers are not interchangeable with "
              "wordfreq's.", file=sys.stderr)
        rows, source = zipf_from_corpus(vocabulary, args.corpus)

    ordered = sorted(rows.items(), key=lambda kv: (-kv[1], kv[0]))

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as out:
        out.write("# word<TAB>zipf, sorted by frequency descending\n")
        out.write(f"# source: {source}\n")
        out.write(f"# rows: {len(ordered)}\n")
        for word, zipf in ordered:
            out.write(f"{word}\t{zipf}\n")

    print(f"wrote {len(ordered)} rows to {args.out} ({source})",
          file=sys.stderr)
    if ordered:
        top = ", ".join(f"{w} {z}" for w, z in ordered[:5])
        print(f"most frequent: {top}", file=sys.stderr)


if __name__ == "__main__":
    main()
