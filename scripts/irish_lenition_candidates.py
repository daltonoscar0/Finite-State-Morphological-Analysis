"""Export Scalpel's lenition readings, for extending MoirfEolas to mark them.

MoirfEolas records urú, prefix and suffix but not lenition, because lenition
changes the morpheme internally rather than prefixing material to it. Scalpel
analyses lenition anyway and the hybrid currently discards that analysis, at a
measured cost of zero against the present gold.

This writes every lenition reading out as a CSV so the readings can be checked
by hand and used to seed a lenition column.

Columns:

``word``              the surface form, as it appears in the source list.
``lenited``           the radical consonant that carries the lenition.
``marker``            the material lenition adds, always 'h'.
``base``              the demutated stem, with the h removed.
``base_in_corpus``    the stem occurs as its own token in data/GA. The
                      strongest automatic signal that the reading is real.
``base_in_moirfeolas`` the stem is itself a MoirfEolas row.
``also_reads_as``     other readings the analyser offered, so ambiguous
                      forms can be found quickly. 'bhfear' reads as both
                      Len:bfear and Ecl:fear and only the second is real.
``gold_uru`` / ``gold_prefix`` / ``gold_suffix``
                      what MoirfEolas already says about the word, so a
                      lenition column can be checked against the other three.

Nothing here is scored. Scoring lenition needs a gold that marks it, which is
the point of the export.
"""
import argparse
import csv
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT / "external" / "tknzn4Irish"
sys.path.insert(0, str(REPO / "src"))
sys.path.insert(0, str(ROOT / "scripts"))

from criochscore.moirfeolas import load_moirfeolas          # noqa: E402
from moirfeolas_score import (                              # noqa: E402
    MOIRFEOLAS, SPLIT_TAGS, demutate_all, marker_and_base, pick,
)

CORPUS = REPO / "data" / "GA" / "unique_ga_tokens.txt"
DEFAULT_OUT = Path.home() / "data" / "tok" / "results" / "irish" / "corrected" \
    / "lenition_candidates.csv"

FIELDS = ["word", "lenited", "marker", "base", "base_in_corpus",
          "base_in_moirfeolas", "also_reads_as",
          "gold_uru", "gold_prefix", "gold_suffix"]


def load_corpus(path):
    if not Path(path).exists():
        return set()
    with open(path, encoding="utf-8") as f:
        return {line.strip() for line in f if line.strip()}


def lenition_rows(words, affix, corpus):
    """One row per word whose chosen reading is lenition.

    A word that also reads as eclipsis is not a lenition candidate: eclipsis
    outranks lenition and the eclipsis reading is the real one. Running the
    ordinary ranking first and keeping only what falls through to Len is the
    same filter the hybrid applies, so this export and the scored system
    always agree about which words are lenited.
    """
    demut = demutate_all(words)
    rows = []
    for w in words:
        # Whatever the ordinary (non-lenition) ranking would have taken wins.
        if pick(w, demut.get(w, []), SPLIT_TAGS):
            continue
        chosen = pick(w, demut.get(w, []), ("Len",), concatenative_only=False)
        if not chosen:
            continue
        _, base = chosen
        marker, base, _ = marker_and_base(w, base)
        others = [f"{t}:{b}" for t, b in demut.get(w, []) if t != "Len"]
        gold = affix.get(w, ("", "", ""))
        rows.append({
            "word": w,
            "lenited": base[0] if base else "",
            "marker": marker,
            "base": base,
            "base_in_corpus": int(base in corpus),
            "base_in_moirfeolas": int(base in affix),
            "also_reads_as": " ".join(others),
            "gold_uru": gold[0],
            "gold_prefix": gold[1],
            "gold_suffix": gold[2],
        })
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--moirfeolas", default=str(MOIRFEOLAS))
    ap.add_argument("--corpus", default=str(CORPUS),
                    help="One-word-per-line list used only to check whether a "
                         "demutated stem occurs on its own.")
    ap.add_argument("--source", choices=["moirfeolas", "corpus"],
                    default="moirfeolas",
                    help="Which word list to analyse. 'moirfeolas' gives "
                         "candidates for the rows she already has.")
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    args = ap.parse_args()

    affix = load_moirfeolas(args.moirfeolas)
    corpus = load_corpus(args.corpus)
    words = sorted(affix) if args.source == "moirfeolas" else sorted(corpus)
    print(f"Analysing {len(words)} words from {args.source}.")

    rows = lenition_rows(words, affix, corpus)
    dest = Path(args.out)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with open(dest, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    n = len(rows)
    in_corpus = sum(r["base_in_corpus"] for r in rows)
    in_mf = sum(r["base_in_moirfeolas"] for r in rows)
    ambiguous = sum(1 for r in rows if r["also_reads_as"])
    has_gold_uru = sum(1 for r in rows if r["gold_uru"])

    print(f"\n{n} lenition candidates ({n / len(words) * 100:.1f}% of the list)")
    print(f"  stem occurs in the GA corpus       {in_corpus:>6}  "
          f"{in_corpus / n * 100:.1f}%")
    print(f"  stem is itself a MoirfEolas row    {in_mf:>6}  {in_mf / n * 100:.1f}%")
    print(f"  word has another reading too       {ambiguous:>6}  "
          f"{ambiguous / n * 100:.1f}%")
    print(f"  word already has a gold urú        {has_gold_uru:>6}")
    print(f"\nby lenited consonant:")
    counts = {}
    for r in rows:
        counts[r["lenited"].lower()] = counts.get(r["lenited"].lower(), 0) + 1
    for c, k in sorted(counts.items(), key=lambda x: -x[1]):
        print(f"  {c or '?':<3} {k:>6}")
    print(f"\nwrote {dest}")


if __name__ == "__main__":
    main()
