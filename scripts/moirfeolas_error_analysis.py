"""Bucket the words where the Scalpel hybrid and its back-off disagree.

Reads the per-word dump that ``moirfeolas_score.py --dump-per-word`` writes
(statuses, score and tokens per word, straight out of her
``calculate_morphological_score``) and attributes each difference to a
mechanism.

Two comparisons are run:

``within``  hybrid vs the SAME back-off. Isolates what layer 1 does, because
            the only difference between the two systems is layer 1.
``across``  a hybrid vs Unigram 8k, which is the reference row in Jane's
            table. Mixes layer 1's effect with the difference between two
            back-offs, so it is reported second.

Buckets, in the order they are tested:

``backoff-difference``     layer 1 did not fire, so the word is byte-identical
                           through both systems and the difference is entirely
                           the two back-offs. Empty by construction in a
                           ``within`` comparison.
``gold-missing-eclipsis``  layer 1 fires on real eclipsis that the gold does
                           not mark, so the split is linguistically right and
                           scored as wrong. A gold gap, not a system error.
``eclipsis-recovered``     gold marks an urú and the urú column improves.
``eclipsis-regressed``     gold marks an urú and the urú column gets worse.
``base-reencoding``        the urú column is unchanged but the prefix or
                           suffix column moved, because layer 2 encodes the
                           demutated base in isolation and segments it
                           differently from the surface form.
``over-segmentation``      same as above, but the hybrid also emits more
                           tokens than the back-off did.
``other``                  anything the tests above do not reach.

Lenition and the lexicon gap cannot appear as buckets: both are cases where
layer 1 declines to fire, which leaves the word untouched and so changes no
score at all. They are reported separately, as the two reasons layer 1
passes up a word it could have had an opinion about.
"""
import argparse
import json
import random
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "external" / "tknzn4Irish" / "src"))
sys.path.insert(0, str(ROOT / "scripts"))

from criochscore.moirfeolas import load_moirfeolas        # noqa: E402
from moirfeolas_score import (                            # noqa: E402
    MOIRFEOLAS, demutate_all, mutation_splits, pick,
)

SEED = 20261104

# Her SCORE_MAP, used only to say which way the urú column moved.
SCORE = {"tokenized exactly": 1.0, "tokenized in parts": 0.5,
         "tokenized incorrectly": 0.0, "no uru": 0.0,
         "no prefix": 0.0, "no suffix": 0.0}


def bucket(word, gold, base_row, hyb_row, split, cands):
    """Attribute one hybrid/back-off difference to a mechanism.

    Layer 1 leaves a word it does not fire on completely untouched, so within
    one back-off such a word cannot differ at all. If it does differ, the two
    systems have different back-offs and layer 1 is not the cause; that is
    ``backoff-difference`` and nothing finer can be attributed to it.
    """
    gold_uru = gold[0]
    b_status, _, b_tokens = base_row
    h_status, _, h_tokens = hyb_row
    uru_moved = b_status[0] != h_status[0]
    pre_moved = b_status[1] != h_status[1]
    suf_moved = b_status[2] != h_status[2]

    if word not in split:
        return "backoff-difference"

    if not gold_uru:
        return "gold-missing-eclipsis"
    if uru_moved:
        better_uru = SCORE[h_status[0]] > SCORE[b_status[0]]
        return "eclipsis-recovered" if better_uru else "eclipsis-regressed"
    if pre_moved or suf_moved:
        if len(h_tokens) > len(b_tokens):
            return "over-segmentation"
        return "base-reencoding"
    return "other"


def analyse(label, base_key, hyb_key, per_word, affix, split, demut, n=50):
    base, hyb = per_word[base_key], per_word[hyb_key]
    shared = [w for w in base if w in hyb]

    worse, better = [], []
    for w in shared:
        if hyb[w][1] < base[w][1]:
            worse.append(w)
        elif hyb[w][1] > base[w][1]:
            better.append(w)

    out = {
        "label": label, "baseline": base_key, "hybrid": hyb_key,
        "n_shared": len(shared), "n_worse": len(worse), "n_better": len(better),
        "n_same": len(shared) - len(worse) - len(better),
        "directions": {},
    }

    rng = random.Random(SEED)
    for name, pool in (("worse", worse), ("better", better)):
        counts = Counter()
        by_bucket = defaultdict(list)
        for w in pool:
            bk = bucket(w, affix[w], base[w], hyb[w], split, demut.get(w, []))
            counts[bk] += 1
            by_bucket[bk].append(w)

        sample = sorted(pool) if len(pool) <= n else rng.sample(sorted(pool), n)
        sample_counts = Counter(
            bucket(w, affix[w], base[w], hyb[w], split, demut.get(w, [])) for w in sample
        )

        examples = {}
        for bk, ws in by_bucket.items():
            examples[bk] = [
                {
                    "word": w, "gold": list(affix[w]),
                    "baseline_tokens": base[w][2], "baseline_score": round(base[w][1], 4),
                    "hybrid_tokens": hyb[w][2], "hybrid_score": round(hyb[w][1], 4),
                }
                for w in sorted(ws)[:3]
            ]

        out["directions"][name] = {
            "n_total": len(pool),
            "n_sampled": len(sample),
            "buckets_all": dict(counts.most_common()),
            "buckets_sampled": dict(sample_counts.most_common()),
            "sample": sorted(sample),
            "examples": examples,
        }
    return out


def show(res):
    print(f"\n=== {res['label']}")
    print(f"    {res['hybrid']}  vs  {res['baseline']}")
    print(f"    shared {res['n_shared']}   worse {res['n_worse']}   "
          f"better {res['n_better']}   same {res['n_same']}")
    for name in ("worse", "better"):
        d = res["directions"][name]
        print(f"  -- {name}: {d['n_total']} total, {d['n_sampled']} sampled")
        if not d["n_total"]:
            print("     none")
            continue
        for bk, c in d["buckets_all"].items():
            s = d["buckets_sampled"].get(bk, 0)
            print(f"     {bk:<24} {c:>5}  (in sample: {s})")
            for ex in d["examples"][bk][:3]:
                print(f"        {ex['word']:<22} gold={tuple(ex['gold'])}")
                print(f"          base {ex['baseline_tokens']} -> {ex['baseline_score']:.2f}")
                print(f"          hyb  {ex['hybrid_tokens']} -> {ex['hybrid_score']:.2f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--per-word", required=True)
    ap.add_argument("--moirfeolas", default=str(MOIRFEOLAS))
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    affix = load_moirfeolas(args.moirfeolas)
    words = list(affix.keys())
    demut = demutate_all(words)
    split = mutation_splits(words, demut)

    per_word = json.load(open(args.per_word, encoding="utf-8"))

    results = [
        analyse("within back-off: UnigramLM 8k",
                "backoff_UnigramLM_8k", "hybrid_UnigramLM_8k",
                per_word, affix, split, demut),
        analyse("within back-off: SentencePieceULM 8k (best hybrid)",
                "backoff_SentencePieceULM_8k", "hybrid_SentencePieceULM_8k",
                per_word, affix, split, demut),
        analyse("across: best hybrid vs Jane's Unigram 8k row",
                "backoff_UnigramLM_8k", "hybrid_SentencePieceULM_8k",
                per_word, affix, split, demut),
    ]
    for r in results:
        show(r)

    # The urú column on its own: what layer 1 does against the gold.
    gold_uru = [w for w in words if affix[w][0]]
    exact = [w for w in gold_uru if w in split and split[w][1] == affix[w][0]]
    differs = [w for w in gold_uru if w in split and split[w][1] != affix[w][0]]
    silent = [w for w in gold_uru if w not in split]
    print(f"\n=== urú column, layer 1 against the gold ({len(gold_uru)} words)")
    print(f"    recovered exactly      {len(exact):>5}  {len(exact)/len(gold_uru)*100:.1f}%")
    print(f"    different material     {len(differs):>5}  {len(differs)/len(gold_uru)*100:.1f}%")
    print(f"    no split proposed      {len(silent):>5}  {len(silent)/len(gold_uru)*100:.1f}%")

    causes = Counter()
    for w in silent:
        if w.startswith("d'") or w.startswith("D'"):
            causes["d' particle, no rule in Scalpel"] += 1
        elif w[:2].lower() == "n-":
            causes["n- before a consonant, non-standard spelling"] += 1
        elif w[:2].lower() == "t-":
            causes["t- before a consonant, non-standard spelling"] += 1
        else:
            causes["other"] += 1
    print("    misses by cause:")
    for c, n in causes.most_common():
        ex = [w for w in silent if (
            (c.startswith("d'") and w[:2] in ("d'", "D'")) or
            (c.startswith("n-") and w[:2].lower() == "n-") or
            (c.startswith("t-") and w[:2].lower() == "t-") or
            (c == "other" and w[:2] not in ("d'", "D'") and w[:2].lower() not in ("n-", "t-")))][:4]
        print(f"      {c:<46} {n:>4}  {', '.join(ex)}")

    false_pos = sorted(w for w in split if not affix[w][0])
    print(f"\n    layer 1 fires where the gold marks no urú: {len(false_pos)}")
    print(f"      {', '.join(false_pos[:10])}")

    # The two reasons layer 1 declines a word it had a reading for. Neither
    # can change a score, because declining leaves the word untouched.
    len_only = sorted(
        w for w in words
        if w not in split and any(t == "Len" for t, _ in demut.get(w, []))
    )
    len_only_gold_uru = [w for w in len_only if affix[w][0]]
    print(f"\n=== where layer 1 declines")
    print(f"    lenition-only reading, discarded by design   {len(len_only):>5}"
          f"   e.g. {', '.join(len_only[:4])}")
    print(f"      of those, gold marks an urú                {len(len_only_gold_uru):>5}")
    print(f"    gold marks an urú and no reading at all      {len(silent):>5}"
          f"   (the lexicon gap, broken down above)")

    if args.out:
        dest = Path(args.out)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(json.dumps({
            "comparisons": results,
            "uru_column": {
                "n_gold_uru": len(gold_uru),
                "recovered_exactly": len(exact),
                "different_material": len(differs),
                "no_split": len(silent),
                "miss_causes": dict(causes.most_common()),
                "false_positives": false_pos,
            },
            "declines": {
                "lenition_only_reading": len(len_only),
                "lenition_only_with_gold_uru": len(len_only_gold_uru),
                "lenition_examples": len_only[:10],
                "no_reading_with_gold_uru": len(silent),
            },
        }, indent=2, ensure_ascii=False))
        print(f"\nwrote {dest}")


if __name__ == "__main__":
    main()
