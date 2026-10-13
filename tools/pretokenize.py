#!/usr/bin/env python3
"""The one pretokenizer every system in the baseline comparison sees.

Comparing a morphology-aware tokenizer against BPE, WordPiece and Unigram is
only meaningful if all four are handed the same word units. Each library has
its own default splitting rule, and those rules disagree about apostrophes,
hyphens and digits, so a difference in pieces per word could just as easily be
a difference in what counted as a word. This module fixes one rule and every
baseline is trained and scored through it.

The rule
--------
Whitespace separates units and is not itself a unit. Within the text between
two runs of whitespace, a unit is a maximal run of characters from one of
three classes:

    letters   [^\\W\\d_]+        the regex word characters that are neither a
                                digit nor an underscore
    digits    \\d+
    other     (?:[^\\s\\w]|_)+   everything else that is not whitespace,
                                underscore included

Nothing is lowercased, nothing is stripped, and no character other than the
separating whitespace is dropped. Concatenating the units of a line, with the
whitespace put back, reproduces the line.

So `don't` is three units, `don`, `'`, `t`; `U.S.A.` is six; `3.14` is three;
and `cats,` is two. An *alphabetic word token* is a unit the letters branch
matched, which is what the comparison scores on.

Standard library only.

    python3 tools/pretokenize.py < text.txt            # one unit per line
    python3 tools/pretokenize.py --joined < text.txt   # units space joined
    python3 tools/pretokenize.py --words < text.txt    # alphabetic units only
"""

import argparse
import gzip
import re
import sys

# The three classes, in the order the scanner tries them. Written as one
# alternation so the whole line is scanned by the regex engine rather than
# character by character in Python, which matters on a 538 MB training file.
UNIT_RE = re.compile(r"[^\W\d_]+|\d+|(?:[^\s\w]|_)+")

# The letters branch on its own, for asking whether a unit is a word.
WORD_RE = re.compile(r"[^\W\d_]+\Z")


def open_text(path, mode="r"):
    """Open a UTF-8 text file, compressed if the name ends in `.gz`.

    The pretokenized training text is 577 MB and the test word list is 48 MB.
    Both are written compressed, both because housekeeping on this machine
    compresses large plain files under the home directory out from under a
    running job, and because the whole of `~/data/tok` then fits in a few
    hundred megabytes. Every tool here reads through this, so the choice is
    one line in one place.
    """
    if path.endswith(".gz"):
        return gzip.open(path, mode + "t", encoding="utf-8", newline="")
    return open(path, mode, encoding="utf-8")


def pretokenize(text):
    """Return the units of a string, in order."""
    return UNIT_RE.findall(text)


def is_word(unit):
    """True if the unit is an alphabetic word token."""
    return WORD_RE.match(unit) is not None


def iter_units(lines):
    """Yield every unit of an iterable of lines, in order."""
    find = UNIT_RE.findall
    for line in lines:
        for unit in find(line):
            yield unit


def iter_words(lines):
    """Yield every alphabetic word token of an iterable of lines, in order."""
    match = WORD_RE.match
    find = UNIT_RE.findall
    for line in lines:
        for unit in find(line):
            if match(unit):
                yield unit


def joined_lines(lines):
    """Yield each line's units joined by single spaces.

    This is the form the tokenizer libraries are trained on: their own
    pre-tokenizers are then set to plain whitespace splitting, so the units
    they see are exactly the ones defined here. A line with no units yields an
    empty string, so the line count is preserved.
    """
    find = UNIT_RE.findall
    for line in lines:
        yield " ".join(find(line))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("files", nargs="*",
                        help="input files, or stdin if none")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--joined", action="store_true",
                      help="one input line per output line, units space joined")
    mode.add_argument("--words", action="store_true",
                      help="alphabetic word tokens only, one per line")
    args = parser.parse_args(argv)

    if args.files:
        handles = [open(path, encoding="utf-8", errors="replace")
                   for path in args.files]
    else:
        handles = [sys.stdin]

    out = sys.stdout
    try:
        for handle in handles:
            if args.joined:
                for line in joined_lines(handle):
                    out.write(line + "\n")
            elif args.words:
                for word in iter_words(handle):
                    out.write(word + "\n")
            else:
                for unit in iter_units(handle):
                    out.write(unit + "\n")
    finally:
        for handle in handles:
            if handle is not sys.stdin:
                handle.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
