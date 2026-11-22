"""Side-by-side evidence for the capitalisation question, for Jane.

MoirfEolas fills the urú and prefix columns for 0.4% of capitalised forms and
for 18.7% and 34.7% of lowercase ones, while the suffix column is filled for
practically all of both. That is an observation about the annotation, not a
verdict on it: it may be a deliberate scoping decision, and only Jane knows.

The clearest way to ask is a list of pairs. The first attempt was to pair a
capitalised word with the identical lowercase form, and that found nothing:
MoirfEolas contains no case variants at all, every one of its 35,517 forms
being distinct even case-insensitively. So the counterpart used here is a
lowercase word carrying the *same prefix*, annotated, beside a capitalised
word that begins with that prefix and has nothing recorded. 'Príomhfhostaithe'
with an empty prefix column beside 'príomhchigire' with 'príomh' in it is the
comparison that makes the point.

Writes results/irish/capitalised_annotation_examples.csv and the fill-rate
table the docs quote.
"""
import argparse
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT / "external" / "tknzn4Irish"
sys.path.insert(0, str(ROOT / "scripts"))

MOIRFEOLAS = REPO / "data" / "MoirfEolas" / "MoirfEolas.csv"
GA = REPO / "data" / "GA" / "unique_ga_tokens.txt"
AFFIXES = REPO / "data" / "MoirfEolas" / "Affixes" / "final_prefixes.txt"
OUT_CSV = ROOT / "results" / "irish" / "capitalised_annotation_examples.csv"
OUT_JSON = ROOT / "results" / "irish" / "capitalisation_fill_rates.json"

N_EXAMPLES = 100
FIELDS = ["capitalised", "cap_uru", "cap_prefix", "cap_suffix",
          "shared_prefix", "lowercase_counterpart", "low_uru", "low_prefix",
          "low_suffix", "annotated_on_lowercase"]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--moirfeolas", default=str(MOIRFEOLAS))
    ap.add_argument("--out-csv", default=str(OUT_CSV))
    ap.add_argument("--out-json", default=str(OUT_JSON))
    args = ap.parse_args()

    with open(args.moirfeolas, encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    cap = [r for r in rows if r["word"][:1].isupper()]
    low = [r for r in rows if not r["word"][:1].isupper()]
    by_lower = {r["word"].lower(): r for r in low}

    rates = {"n_capitalised": len(cap), "n_lowercase": len(low)}
    print(f"{'column':<8}{'capitalised':>24}{'lowercase':>24}")
    for col, key in (("urú", "uru"), ("prefix", "prefix"),
                     ("suffix", "suffix")):
        c = sum(1 for r in cap if r[col].strip())
        l = sum(1 for r in low if r[col].strip())
        rates[key] = {
            "capitalised_filled": c,
            "capitalised_pct": round(c / len(cap) * 100, 1),
            "lowercase_filled": l,
            "lowercase_pct": round(l / len(low) * 100, 1),
        }
        print(f"{col:<8}{c:>8} / {len(cap)} ({c / len(cap) * 100:>5.1f}%)"
              f"{l:>8} / {len(low)} ({l / len(low) * 100:>5.1f}%)")

    exact = sum(1 for r in cap if r["word"].lower() in by_lower)
    print(f"\ncapitalised words whose exact lowercase form is also a row: "
          f"{exact} (MoirfEolas holds no case variants)")
    rates["exact_case_variants"] = exact

    # One annotated lowercase exemplar per prefix, longest prefix first so
    # 'príomh' is preferred over any shorter string that also matches.
    jane = set()
    if AFFIXES.exists():
        with open(AFFIXES, encoding="utf-8") as f:
            jane = {line.strip().lower() for line in f if line.strip()}
    attested = {r["word"].lower() for r in rows}
    if GA.exists():
        with open(GA, encoding="utf-8") as f:
            attested |= {line.strip().lower() for line in f if line.strip()}

    exemplar = {}
    for r in low:
        pre = r["prefix"].strip()
        if pre and pre.lower() not in exemplar:
            exemplar[pre.lower()] = r
    by_len = sorted(exemplar, key=len, reverse=True)

    # Pairs: a capitalised word with nothing recorded, beside an annotated
    # lowercase word carrying the prefix the capitalised word begins with.
    pairs = []
    for r in cap:
        if r["urú"].strip() or r["prefix"].strip():
            continue
        w = r["word"].lower()
        # A bare string match is not enough: 'Aindrias' starts with 'ain'
        # and 'Antalya' with 'anta' without either being prefixed. Require
        # the prefix to be one of Jane's, and the remainder after it to be an
        # attested form in its own right (allowing for a hyphen and for a
        # lenited initial), so the pairs are morphology and not coincidence.
        def plausible(p):
            if len(p) < 3 or p not in jane or not w.startswith(p):
                return False
            rest = w[len(p):].lstrip("-")
            if len(rest) < 3:
                return False
            delen = rest[0] + rest[2:] if rest[1:2] == "h" else rest
            return rest in attested or delen in attested

        pre = next((p for p in by_len if plausible(p)), None)
        if pre is None:
            continue
        other = exemplar[pre]
        pairs.append({
            "capitalised": r["word"], "cap_uru": r["urú"],
            "cap_prefix": r["prefix"], "cap_suffix": r["suffix"],
            "lowercase_counterpart": other["word"], "low_uru": other["urú"],
            "low_prefix": other["prefix"], "low_suffix": other["suffix"],
            "annotated_on_lowercase": ("urú" if other["urú"].strip() else "")
            + ("+" if other["urú"].strip() and other["prefix"].strip() else "")
            + ("prefix" if other["prefix"].strip() else ""),
            "shared_prefix": pre,
        })
    # Spread the written sample across distinct prefixes rather than taking
    # the alphabetical first hundred, which would all have been 'aer'.
    pairs.sort(key=lambda r: r["capitalised"])
    grouped = {}
    for pr in pairs:
        grouped.setdefault(pr["shared_prefix"], []).append(pr)
    spread, i = [], 0
    while len(spread) < len(pairs):
        added = False
        for pre in sorted(grouped):
            if i < len(grouped[pre]):
                spread.append(grouped[pre][i])
                added = True
        if not added:
            break
        i += 1
    print(f"  pairs span {len(grouped)} distinct prefixes")
    print(f"\n{len(pairs)} capitalised words begin with a prefix that is "
          f"annotated on a lowercase row but left empty on theirs")
    rates["n_pairs"] = len(pairs)

    dest = Path(args.out_csv)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with open(dest, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(spread[:N_EXAMPLES])
    print(f"wrote the first {min(N_EXAMPLES, len(pairs))} to {dest}")

    Path(args.out_json).write_text(
        json.dumps(rates, indent=2, ensure_ascii=False))
    print(f"wrote {args.out_json}")

    for p in spread[:12]:
        print(f"  {p['capitalised']:<24} prefix='' vs "
              f"{p['lowercase_counterpart']:<22} "
              f"prefix={p['low_prefix']!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
