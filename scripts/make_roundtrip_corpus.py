"""Build the round-trip test corpus for the MT pipeline.

Stage 4 wants at least 10,000 real Irish lines, covering d' forms, hyphenated
prefixes, capitalised mutated forms, curly apostrophes and punctuation stuck
to words.

No Irish *sentence* corpus is in this checkout. What there is, is
``data/GA/all_ga_input.txt``: 4,682,103 lines, one word per line, and it is a
dump of real running text in order with punctuation still attached
('áirithe,', 'nGaeilge.', 'd’imeacht.'). So consecutive lines are consecutive
words of real Irish, and joining a run of them reconstructs a real line
rather than inventing one. That is what this does, and the limitation is
recorded in MT_PIPELINE.md: when Jane's parallel data arrives the round trip
should be rerun on it, because her line breaks and her whitespace are the
real test.

Lines are stratified so that every required category is present in quantity
rather than left to chance, and a tail of unfiltered lines is added so the
set is not all special cases. A handful of adversarial lines are appended by
hand for whitespace cases the corpus cannot supply (tabs, double spaces,
leading and trailing space).
"""
import argparse
import random
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORPUS = ROOT / "external" / "tknzn4Irish" / "data" / "GA" / "all_ga_input.txt"
OUT = ROOT / "results" / "irish" / "roundtrip_corpus.txt"

SEED = 20261118
WORDS_PER_LINE = (8, 22)

CATEGORIES = {
    "clitic_straight": re.compile(r"^[dmbDMB]'"),
    "clitic_curly": re.compile(r"^[dmbDMB]’"),
    "hyphen": re.compile(r"\w-\w"),
    "cap_mutation": re.compile(
        r"^(?:[nhtd]-?[A-ZÁÉÍÓÚ]|(?:bh|mb|gc|nd|ng|bp|dt)[A-ZÁÉÍÓÚ])"),
    "punct_attached": re.compile(r"\w[.,;:!?)\"']|[(\"']\w"),
}

# Whitespace shapes the word-per-line corpus cannot produce.
ADVERSARIAL = [
    "Tab\there agus d'fhág sé.",
    "Double  space  agus  an-mhaith.",
    " Leading space, nGaeilge.",
    "Trailing space, hÉireann. ",
    "",
    "   ",
    "d’fhág",
    "nGaeilge",
    "Ard-Chúirt.",
    "m'athair agus b'fhearr liom é.",
    "D'FHÁG SÉ AN TEACH.",
    "Focal le nbsp agus tSeáin.",
]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--corpus", default=str(CORPUS))
    ap.add_argument("--out", default=str(OUT))
    ap.add_argument("--lines", type=int, default=12000)
    args = ap.parse_args()

    with open(args.corpus, encoding="utf-8") as f:
        words = [w for w in (line.strip() for line in f) if w]
    print(f"{len(words)} words from {args.corpus}")

    rng = random.Random(SEED)

    # Index the start of every run that contains each category, so the
    # stratified draw is over real positions rather than filtered words.
    per_cat = max(1, (args.lines - len(ADVERSARIAL)) // (len(CATEGORIES) + 1))
    lines, seen = [], set()

    def take(start):
        n = rng.randint(*WORDS_PER_LINE)
        line = " ".join(words[start:start + n])
        if line and line not in seen:
            seen.add(line)
            lines.append(line)
            return True
        return False

    for name, pat in CATEGORIES.items():
        hits = [i for i, w in enumerate(words) if pat.search(w)]
        rng.shuffle(hits)
        got = 0
        for i in hits:
            if got >= per_cat:
                break
            # Put the matching word somewhere inside the line, not always
            # first, so leading-position bugs are not the only ones caught.
            start = max(0, i - rng.randint(0, 6))
            if take(start):
                got += 1
        print(f"  {name:<18}{len(hits):>9} words in corpus, "
              f"{got:>5} lines drawn")

    # Unfiltered tail.
    tail = 0
    while len(lines) < args.lines - len(ADVERSARIAL):
        if take(rng.randrange(0, max(1, len(words) - 24))):
            tail += 1
    print(f"  {'unfiltered':<18}{'':>9}      {tail:>5} lines drawn")

    lines.extend(ADVERSARIAL)
    rng.shuffle(lines)

    dest = Path(args.out)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with open(dest, "w", encoding="utf-8") as f:
        for line in lines:
            f.write(line + "\n")
    print(f"\nwrote {dest} with {len(lines)} lines")

    # Report the coverage actually achieved, over the written lines.
    print("\ncoverage of the written set:")
    for name, pat in CATEGORIES.items():
        n = sum(1 for line in lines if any(pat.search(w)
                                           for w in line.split()))
        print(f"  {name:<18}{n:>7} lines")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
