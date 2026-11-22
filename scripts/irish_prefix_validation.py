"""Validate the prefix-lenition extension without scoring it on its own data.

The problem this answers. The prefix list, the decision to drop `ain`, and the
`SHORT_PREFIX` guard were all chosen by looking at MoirfEolas, and the 99.83%
figure was then measured on MoirfEolas. That is an in-sample number and has to
be labelled as one. Three things here:

1. **In-sample baseline.** The current list scored on all of MoirfEolas, which
   is the 99.83% as previously reported, now explicitly labelled.
2. **Five-fold cross-validation.** On each training fold the data-driven
   choices are *remade* from that fold alone: the guard threshold is swept and
   picked, and any prefix whose firings disagree with the layout more often
   than they agree is dropped. The result is then scored on the held-out fold,
   which it never saw. Mean and range are reported for both agreement and
   off-layout firings.
3. **Out-of-benchmark sample.** The extension is run over GA corpus forms that
   are *not* in MoirfEolas, so there is no layout to agree with at all, and
   300 firings are written out for hand-checking. No precision is computed
   from them: an unchecked sample is not evidence.

"Agreement" rather than "recall" throughout, for the reason in
lenition_depth.md: the sites are derived by this project's own script from
MoirfEolas's prefix and urú columns, using the same lenition validity rule as
the FST, so they are candidate sites rather than gold. The extension stays off
by default.
"""
import argparse
import csv
import json
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT / "external" / "tknzn4Irish"
sys.path.insert(0, str(ROOT / "scripts"))

from ga_lenition_prefix import (                               # noqa: E402
    LENITING_PREFIXES, prefix_lenition, prefix_lenition_composed,
)
from ga_mutation_split import demutate_all                     # noqa: E402
from irish_lenition_depth import layout, sites                 # noqa: E402

MOIRFEOLAS = REPO / "data" / "MoirfEolas" / "MoirfEolas.csv"
# Two different GA files, for two different jobs. The attestation set that
# vets short prefixes must be the one the extension actually uses in
# production (irish_lenition_depth.py's CORPUS), or the in-sample figure here
# will not be the figure reported elsewhere. The wider form list is only used
# to find material outside the benchmark.
GA_ATTEST = REPO / "data" / "GA" / "unique_ga_tokens.txt"
GA = REPO / "data" / "GA" / "all_ga_input.txt"
AFFIXES = REPO / "data" / "MoirfEolas" / "Affixes" / "final_prefixes.txt"

OUT_JSON = ROOT / "results" / "irish" / "prefix_validation.json"
OUT_HANDCHECK = ROOT / "results" / "irish" / "prefix_extension_handcheck.csv"
OUT_REVIEW = ROOT / "results" / "irish" / "prefix_list_review.csv"

SEED = 20261122
FOLDS = 5
GUARDS = (0, 2, 3, 4)
HANDCHECK_N = 300


def all_lower(w):
    return not any(c.isupper() for c in w)


def gold_sites(rows):
    """{(word, index)} of layout-derived candidate sites, and the deep subset.

    Restricted to fully-lowercase words: MoirfEolas fills prefix and urú for
    0.4% of capitalised forms against 34.7% of lowercase ones, so a firing on
    a capitalised word has nothing to be judged against.
    """
    every, deep = set(), set()
    for r in rows:
        w = r["word"]
        if not all_lower(w):
            continue
        lay = layout(w, r["urú"].strip(), r["prefix"].strip())
        if lay is None:
            continue
        start, n_before, _ = lay
        for idx in sites(w, start):
            every.add((w, idx))
            if n_before >= 1:
                deep.add((w, idx))
    return every, deep


def fire(word, demut, attested, prefixes, guard):
    """Where the extension fires on a word, or None."""
    hit = prefix_lenition(word, attested, prefixes=prefixes,
                          short_prefix=guard)
    if hit:
        return len(hit[0]), hit
    comp = prefix_lenition_composed(word, demut.get(word, []), attested,
                                    prefixes=prefixes, short_prefix=guard)
    if comp:
        return len(comp[0]) + len(comp[1]), (comp[0] + comp[1], comp[2])
    return None


def evaluate(words, demut, attested, prefixes, guard, every, deep):
    """(n_fire, n_hit, n_offlayout) over ``words``."""
    n_fire = n_hit = n_off = 0
    for w in words:
        f = fire(w, demut, attested, prefixes, guard)
        if not f:
            continue
        idx, _ = f
        n_fire += 1
        if (w, idx) in deep:
            n_hit += 1
        elif (w, idx) not in every:
            n_off += 1
    return n_fire, n_hit, n_off


def per_prefix(words, demut, attested, prefixes, guard, every):
    """{prefix: (agree, disagree)} over ``words``, for the drop rule."""
    stat = {}
    for w in words:
        f = fire(w, demut, attested, prefixes, guard)
        if not f:
            continue
        idx, (pre, _) = f
        key = pre.rstrip("-").lower()
        # Attribute to the listed prefix the match actually used.
        match = max((p for p in prefixes if key.endswith(p)),
                    key=len, default=key)
        a, d = stat.get(match, (0, 0))
        if (w, idx) in every:
            stat[match] = (a + 1, d)
        else:
            stat[match] = (a, d + 1)
    return stat


def choose_on_fold(train, demut, attested, every, deep):
    """Remake the data-driven choices from a training fold alone.

    Two choices were originally made by looking at the whole benchmark: the
    guard threshold, and dropping a prefix that disagrees more than it agrees.
    Both are remade here from ``train`` only, so the held-out fold is genuinely
    unseen.
    """
    best, best_guard = None, GUARDS[0]
    for g in GUARDS:
        n_fire, n_hit, n_off = evaluate(train, demut, attested,
                                        LENITING_PREFIXES, g, every, deep)
        if not n_fire:
            continue
        prec = (n_fire - n_off) / n_fire
        # The original rule: take the highest agreement among the settings
        # that hold precision, which in the sweep meant preferring the looser
        # guard whenever precision did not fall.
        score = (round(prec, 4), n_hit)
        if best is None or score > best:
            best, best_guard = score, g

    stat = per_prefix(train, demut, attested, LENITING_PREFIXES, best_guard,
                      every)
    dropped = sorted(p for p, (a, d) in stat.items() if d > a)
    kept = frozenset(LENITING_PREFIXES) - set(dropped)
    return best_guard, kept, dropped


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--moirfeolas", default=str(MOIRFEOLAS))
    ap.add_argument("--ga", default=str(GA))
    ap.add_argument("--ga-attest", default=str(GA_ATTEST))
    ap.add_argument("--out-json", default=str(OUT_JSON))
    ap.add_argument("--out-handcheck", default=str(OUT_HANDCHECK))
    ap.add_argument("--out-review", default=str(OUT_REVIEW))
    ap.add_argument("--allow-skips", action="store_true")
    args = ap.parse_args()

    with open(args.moirfeolas, encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    mf_words = [r["word"] for r in rows]

    ga_forms = []
    if Path(args.ga).exists():
        with open(args.ga, encoding="utf-8") as f:
            ga_forms = sorted({line.strip() for line in f if line.strip()})

    universe = sorted(set(mf_words) | set(ga_forms))
    demut = demutate_all(universe, allow_skips=args.allow_skips)

    attest_src = set()
    if Path(args.ga_attest).exists():
        with open(args.ga_attest, encoding="utf-8") as f:
            attest_src = {line.strip() for line in f if line.strip()}
    attested = {w.lower() for w in mf_words} | {w.lower() for w in attest_src}
    print(f"{len(mf_words)} MoirfEolas words, {len(ga_forms)} GA forms, "
          f"{len(universe)} distinct in total")
    print(f"attestation set for the short-prefix guard: {len(attested)} "
          f"({Path(args.ga_attest).name} + MoirfEolas), matching production")

    every, deep = gold_sites(rows)
    print(f"layout-derived candidate sites on fully-lowercase words: "
          f"{len(every)}, of which depth >= 1: {len(deep)}")

    low = [w for w in mf_words if all_lower(w)]
    report = {}

    # ---------------------------------------------------------- in-sample
    n_fire, n_hit, n_off = evaluate(low, demut, attested, LENITING_PREFIXES,
                                    2, every, deep)
    report["in_sample"] = {
        "words": len(low), "firings": n_fire, "on_deep_site": n_hit,
        "off_layout": n_off,
        "agreement_pct": round(n_hit / len(deep) * 100, 2),
        "on_layout_pct": round((n_fire - n_off) / n_fire * 100, 2),
        "guard": 2, "n_prefixes": len(LENITING_PREFIXES),
    }
    print(f"\nIN-SAMPLE (list v1, the previously reported figure):")
    print(f"  firings {n_fire}, on a depth>=1 site {n_hit} "
          f"({n_hit / len(deep) * 100:.2f}% agreement), "
          f"off-layout {n_off} ({(n_fire - n_off) / n_fire * 100:.2f}% "
          f"on-layout)")

    # --------------------------------------------------- cross-validation
    rng = random.Random(SEED)
    shuffled = list(low)
    rng.shuffle(shuffled)
    folds = [shuffled[i::FOLDS] for i in range(FOLDS)]

    cv = []
    print(f"\n{FOLDS}-FOLD CROSS-VALIDATION (choices remade per training fold)")
    print(f"  {'fold':<6}{'guard':>6}{'dropped':>9}{'held words':>12}"
          f"{'fire':>7}{'hit':>6}{'agree%':>9}{'off':>6}{'onlayout%':>11}")
    for i in range(FOLDS):
        held = folds[i]
        train = [w for j, f in enumerate(folds) if j != i for w in f]
        guard, kept, dropped = choose_on_fold(train, demut, attested,
                                              every, deep)
        held_deep = {(w, idx) for (w, idx) in deep if w in set(held)}
        n_f, n_h, n_o = evaluate(held, demut, attested, kept, guard,
                                 every, deep)
        agree = n_h / len(held_deep) * 100 if held_deep else None
        onlay = (n_f - n_o) / n_f * 100 if n_f else None
        cv.append({"fold": i, "guard": guard, "dropped": dropped,
                   "held_words": len(held), "held_deep_sites": len(held_deep),
                   "firings": n_f, "on_deep_site": n_h, "off_layout": n_o,
                   "agreement_pct": round(agree, 2) if agree else None,
                   "on_layout_pct": round(onlay, 2) if onlay else None})
        print(f"  {i:<6}{guard:>6}{len(dropped):>9}{len(held):>12}"
              f"{n_f:>7}{n_h:>6}{agree:>8.2f}%{n_o:>6}{onlay:>10.2f}%")

    ag = [c["agreement_pct"] for c in cv if c["agreement_pct"] is not None]
    ol = [c["on_layout_pct"] for c in cv if c["on_layout_pct"] is not None]
    report["cross_validation"] = {
        "folds": cv, "seed": SEED,
        "agreement_mean": round(sum(ag) / len(ag), 2),
        "agreement_min": min(ag), "agreement_max": max(ag),
        "on_layout_mean": round(sum(ol) / len(ol), 2),
        "on_layout_min": min(ol), "on_layout_max": max(ol),
        "off_layout_total": sum(c["off_layout"] for c in cv),
    }
    print(f"\n  agreement  mean {sum(ag)/len(ag):.2f}%  "
          f"range {min(ag):.2f} to {max(ag):.2f}")
    print(f"  on-layout  mean {sum(ol)/len(ol):.2f}%  "
          f"range {min(ol):.2f} to {max(ol):.2f}")
    print(f"  off-layout firings across all folds: "
          f"{sum(c['off_layout'] for c in cv)}")

    # ------------------------------------------------ out of benchmark
    mf_set = set(mf_words)
    # all_ga_input.txt is raw corpus tokens with punctuation still attached
    # ('Ard-Chúirt.".', 'An-bhlasta,'). Hand-checking those would mean judging
    # tokenization artifacts rather than morphology, so the sheet is limited
    # to forms made only of letters, hyphens and apostrophes.
    def clean(w):
        return bool(w) and all(c.isalpha() or c in "-'’" for c in w)

    outside = [w for w in ga_forms if w not in mf_set and clean(w)]
    fires = []
    for w in outside:
        f = fire(w, demut, attested, LENITING_PREFIXES, 2)
        if f:
            _, (pre, stem) = f
            fires.append({"word": w, "prefix": pre, "stem": stem,
                          "stem_attested": int(stem.lower() in attested),
                          "correct": ""})
    print(f"\nOUT OF BENCHMARK: {len(outside)} clean GA forms absent from "
          f"MoirfEolas, extension fires on {len(fires)}")
    rng2 = random.Random(SEED)
    sample = rng2.sample(fires, min(HANDCHECK_N, len(fires)))
    sample.sort(key=lambda r: r["word"])
    dest = Path(args.out_handcheck)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with open(dest, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["word", "prefix", "stem",
                                          "stem_attested", "correct"])
        w.writeheader()
        w.writerows(sample)
    print(f"  wrote {len(sample)} rows to {dest} with an empty 'correct' "
          f"column; no precision is computed from these")
    report["out_of_benchmark"] = {
        "ga_forms_outside_moirfeolas": len(outside),
        "firings": len(fires), "sampled": len(sample),
        "handcheck_csv": str(dest), "seed": SEED,
    }

    # ------------------------------------------------- 4d: review sheet
    jane = set()
    if AFFIXES.exists():
        with open(AFFIXES, encoding="utf-8") as f:
            jane = {line.strip().lower() for line in f if line.strip()}

    stat = {}
    examples = {}
    for w in low:
        f = fire(w, demut, attested, LENITING_PREFIXES, 2)
        if not f:
            continue
        idx, (pre, stem) = f
        key = pre.rstrip("-").lower()
        match = max((p for p in LENITING_PREFIXES if key.endswith(p)),
                    key=len, default=key)
        a, d = stat.get(match, (0, 0))
        hits, miss = examples.setdefault(match, ([], []))
        if (w, idx) in every:
            stat[match] = (a + 1, d)
            if len(hits) < 5:
                hits.append(f"{w} = {pre}+{stem}")
        else:
            stat[match] = (a, d + 1)
            if len(miss) < 5:
                miss.append(f"{w} = {pre}+{stem}")

    dest = Path(args.out_review)
    with open(dest, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["prefix", "firings", "agree_with_layout",
                    "disagree_with_layout", "in_jane_inventory",
                    "example_hits", "example_disagreements"])
        for p in sorted(LENITING_PREFIXES):
            a, d = stat.get(p, (0, 0))
            hits, miss = examples.get(p, ([], []))
            w.writerow([p, a + d, a, d, int(p in jane),
                        "; ".join(hits), "; ".join(miss)])
    print(f"\nwrote the per-prefix review sheet to {dest} "
          f"({len(LENITING_PREFIXES)} prefixes)")
    report["review_csv"] = str(dest)
    never = [p for p in sorted(LENITING_PREFIXES) if p not in stat]
    print(f"  prefixes that never fire on a lowercase MoirfEolas word: "
          f"{len(never)} {never}")
    report["prefixes_never_firing"] = never

    Path(args.out_json).write_text(
        json.dumps(report, indent=2, ensure_ascii=False))
    print(f"wrote {args.out_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
