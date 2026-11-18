"""Reshape the lenition export into the crosscheck file Jane asked for.

``irish_lenition_candidates.py`` writes the full export: ten columns, most of
them our own diagnostics (which ranking fired, what the present gold says).
Jane does not need any of that. She is adding a lenition column to MoirfEolas
and wants to line her annotation up against ours, so she needs the surface
form, the stem we demutated it to, the consonant carrying the séimhiú, and a
way back into her own file.

Doubtful rows stay in. A row she disagrees with is more useful to her than a
row we quietly dropped, so everything we are unsure about is flagged instead:

``fixed``             the th-/ch- is fossilised, not a séimhiú. 'chun' and
                      'thart' are not 'cun' and 'tart' lenited, they are
                      single morphemes that happen to start that way.
``english``           an English word, loan or Latin name sitting in the
                      Irish corpus that only looks lenited. 'Thunbergia'
                      is not 'Tunbergia' with a séimhiú.
``proper_noun``       capitalised and never attested lowercase, so the
                      demutation is probably of a name.
``stem_unattested``   the stem we propose occurs nowhere else in MoirfEolas,
                      which is the main way a wrong demutation shows up.

A row can carry more than one flag; they are joined with ';'. An empty flag
means nothing tripped, not that the row is certified.

Frequency is counted over MoirfEolas rather than assumed. MoirfEolas is a
unique word list, so in practice every count is 1 and the frequency sort
collapses to alphabetical; the column is still written so the file does not
change shape if she ever ships a version with repeats.
"""
import argparse
import csv
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT / "external" / "tknzn4Irish"
sys.path.insert(0, str(REPO / "src"))

RESULTS = Path.home() / "data" / "tok" / "results" / "irish" / "corrected"
SOURCE = RESULTS / "lenition_candidates.csv"
MOIRFEOLAS = REPO / "data" / "MoirfEolas" / "MoirfEolas.csv"
CORPUS = REPO / "data" / "GA" / "unique_ga_tokens.txt"
WORDLIST = Path("/usr/share/dict/words")
DEFAULT_OUT = RESULTS / "for_jane" / "lenition_candidates.csv"

FIELDS = ["word", "stem", "lenited_consonant", "moirfeolas_row",
          "frequency", "flag"]

# Fossilised th-/ch- forms. Prepositions and their pronominals, the
# directional adverbs, and the handful of particles that look lenited and
# never are. Matched on the whole surface form, case-insensitively.
FIXED = {
    "chun", "chuig", "chuige", "chuici", "chucu", "chugam", "chugat",
    "chugainn", "chugaibh", "cheana", "chomh", "cha", "char", "chan",
    "thar", "tharam", "tharat", "thairis", "thairste", "tharainn",
    "tharaibh", "thartu", "thart", "thall", "thíos", "thuas", "thoir",
    "thiar", "theas", "thimpeall", "thrí", "thríd", "chasla",
}

FADAS = set("áéíóúÁÉÍÓÚ")


def load_lines(path):
    if not Path(path).exists():
        return set()
    with open(path, encoding="utf-8") as f:
        return {line.strip() for line in f if line.strip()}


def relenite(stem):
    """Put the séimhiú back: 'Babhta' -> 'Bhabhta'.

    The inverse of what the export did, so it is worth checking rather than
    trusting. A stem that does not round-trip means the two files disagree
    about where the h came from.
    """
    return stem[:1] + "h" + stem[1:] if stem else ""


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--source", default=str(SOURCE))
    ap.add_argument("--moirfeolas", default=str(MOIRFEOLAS))
    ap.add_argument("--corpus", default=str(CORPUS))
    ap.add_argument("--wordlist", default=str(WORDLIST))
    ap.add_argument("--out", default=str(DEFAULT_OUT))
    args = ap.parse_args()

    with open(args.source, encoding="utf-8") as f:
        export = list(csv.DictReader(f))

    # Row numbers are 1-based over the data, so the header is not row 1 and
    # the number matches what a spreadsheet shows minus one.
    index, freq = {}, {}
    with open(args.moirfeolas, encoding="utf-8") as f:
        for i, row in enumerate(csv.DictReader(f), start=1):
            w = row["word"]
            index.setdefault(w, i)
            freq[w] = freq.get(w, 0) + 1

    mf_lower = {w.lower() for w in freq}
    corpus = load_lines(args.corpus)
    # Tokens the corpus actually shows in lowercase, not the case-folded set.
    # Case-folding would fold 'Bhealtaine' down to 'bhealtaine' and make
    # every capitalised word look attested lowercase, which flags nothing.
    corpus_lower = {w for w in corpus if w == w.lower()}
    english = {w.lower() for w in load_lines(args.wordlist)}
    # Attested in Irish at all, by either source. Used only to veto the
    # English screen below.
    attested = mf_lower | {w.lower() for w in corpus}

    rows, dropped = [], []
    for r in export:
        word, stem = r["word"], r["base"]
        if word not in freq:
            dropped.append(word)
            continue

        flags = []
        if word.lower() in FIXED:
            flags.append("fixed")
        # The English wordlist alone is far too loose: web2 carries genus
        # names and archaisms, so it matches 'chara', 'chait' and 'theine',
        # which are ordinary lenited Irish. Two vetoes tighten it. A fada
        # means the form is Irish, and a stem attested in Irish means the
        # demutation landed on a real word, so only forms that are English
        # on the surface and nonsense underneath survive.
        if (word.lower() in english and len(word) > 1
                and not (set(word) & FADAS)
                and stem.lower() not in attested):
            flags.append("english")
        if word[:1].isupper() and word.lower() not in corpus_lower:
            flags.append("proper_noun")
        if stem.lower() not in mf_lower:
            flags.append("stem_unattested")

        rows.append({
            "word": word,
            "stem": stem,
            "lenited_consonant": r["lenited"],
            "moirfeolas_row": index[word],
            "frequency": freq[word],
            "flag": ";".join(flags),
        })

    # Frequency descending, then alphabetical. Sorting on the decomposed form
    # keeps 'á' next to 'a' instead of after 'z', which is what she will
    # expect when she scrolls the file.
    def alpha(r):
        return unicodedata.normalize("NFKD", r["word"]).lower()

    rows.sort(key=lambda r: (-r["frequency"], alpha(r)))

    problems = []
    seen = set()
    for r in rows:
        if r["word"] in seen:
            problems.append(f"duplicate surface form: {r['word']}")
        seen.add(r["word"])
        if relenite(r["stem"]) != r["word"]:
            problems.append(
                f"stem does not re-lenite to the word: "
                f"{r['stem']} -> {relenite(r['stem'])} != {r['word']}")
        for key in FIELDS:
            if key != "flag" and str(r[key]).strip() == "":
                problems.append(f"empty {key} on row for {r['word']!r}")
    if dropped:
        problems.append(f"{len(dropped)} words absent from MoirfEolas: "
                        f"{dropped[:5]}")

    dest = Path(args.out)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with open(dest, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    print(f"read {len(export)} export rows, wrote {len(rows)} to {dest}")
    counts = {}
    for r in rows:
        for flag in (r["flag"].split(";") if r["flag"] else ["(none)"]):
            counts[flag] = counts.get(flag, 0) + 1
    print("\nflags:")
    for flag, n in sorted(counts.items(), key=lambda x: -x[1]):
        print(f"  {flag:<17} {n:>6}")
    print(f"  rows carrying any flag "
          f"{sum(1 for r in rows if r['flag']):>6}")

    print("\nvalidation:", "clean" if not problems
          else f"{len(problems)} problem(s)")
    for p in problems[:20]:
        print(f"  {p}")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
