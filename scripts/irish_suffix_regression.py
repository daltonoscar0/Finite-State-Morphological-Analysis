"""Which words lose their suffix boundary when the pre-tokenizer runs?

`suffix_only` falls by 0.01 to 0.11 in nine of the twelve Stage 3 cells. It is
the only category that ever regresses, and it is small but one-directional, so
it is an effect rather than noise. This finds the words behind it.

Method. For one model, score the same word list twice: the plain back-off, and
the back-off with layer 1 in front. Both go through Jane's
``find_morphological_info``, so the suffix status per word is hers, not a
reimplementation. Any word whose suffix status differs between the two is
written out with both token sequences, so the mechanism can be read off the
rows rather than guessed at.

Only ``suffix_only`` words are considered: those with a suffix annotated and
no urú and no prefix. A word with urú as well would conflate the suffix change
with the urú change the pre-tokenizer is there to make.
"""
import argparse
import csv
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT / "external" / "tknzn4Irish"
sys.path.insert(0, str(REPO / "src"))
sys.path.insert(0, str(ROOT / "scripts"))

from ga_clitic import CLITIC_TAG, add_clitic_readings            # noqa: E402
from ga_mutation_split import (                                  # noqa: E402
    SPLIT_TAGS, demutate_all, mutation_splits,
)
from irish_tokenizer_generality import (                         # noqa: E402
    SPECIAL, encoder, layered, plain_tl,
)
from moirfeolas_score import MOIRFEOLAS, exclude_single          # noqa: E402

from criochscore.moirfeolas import load_moirfeolas               # noqa: E402
from criochscore.scoring import find_morphological_info          # noqa: E402

MODELS_DIR = Path.home() / "data" / "tok" / "results" / "irish" / "stage3" / "models"
OUT = ROOT / "results" / "irish" / "suffix_only_regressions.csv"

# The two cells named in the brief, plus the two Unigram sizes that actually
# regress: Unigram 8k is one of the three cells where suffix_only does not
# move, so on its own it would show nothing.
CELLS = [("unigram", 8000), ("wordpiece", 8000),
         ("unigram", 32000), ("unigram", 64000)]

# Her scorer's suffix verdicts, best to worst.
RANK = {"tokenized exactly": 1.0, "tokenized in parts": 0.5,
        "tokenized incorrectly": 0.0}

FIELDS = ["model", "vocab_size", "word", "suffix", "direction",
          "status_plain", "status_scalpel", "tokens_plain", "tokens_scalpel"]


def suffix_status(token_lens, affix):
    """{word: status} for the suffix slot, straight out of her scorer."""
    info = find_morphological_info(token_lens, affix)
    return {w: v[2][0] for w, v in info.items()}, info


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--models-dir", default=str(MODELS_DIR))
    ap.add_argument("--moirfeolas", default=str(MOIRFEOLAS))
    ap.add_argument("--out", default=str(OUT))
    ap.add_argument("--allow-skips", action="store_true")
    args = ap.parse_args()

    manifest = json.loads((Path(args.models_dir) / "manifest.json").read_text())
    affix = load_moirfeolas(args.moirfeolas)
    words = list(affix)

    demut = demutate_all(words, allow_skips=args.allow_skips)
    demut, _ = add_clitic_readings(words, demut)
    splits = mutation_splits(words, demut, (CLITIC_TAG,) + SPLIT_TAGS)

    # suffix_only: a suffix and nothing else.
    suffix_only = {w for w in words
                   if affix[w][2] and not affix[w][0] and not affix[w][1]}
    print(f"{len(words)} words; {len(suffix_only)} are suffix_only")

    rows = []
    for model, size in CELLS:
        path = next((Path(c["path"]) for c in manifest.values()
                     if c["model"] == model and c["vocab_size"] == size
                     and c["condition"] == "plain"), None)
        if path is None or not path.exists():
            print(f"skipping {model} {size}: no plain model")
            continue
        enc = encoder(path, SPECIAL[model])

        tl_plain = exclude_single(plain_tl(words, enc))
        tl_scal = exclude_single(layered(words, enc, splits))
        st_p, info_p = suffix_status(tl_plain, affix)
        st_s, info_s = suffix_status(tl_scal, affix)

        # Two different things can move the average, and they have to be
        # told apart. A shared word can change status, or a word can enter or
        # leave the scored set, because --exclude-single-token is applied per
        # system: a word the plain back-off left whole is excluded, and the
        # same word split by layer 1 is two tokens and so included.
        both = (set(st_p) & set(st_s)) & suffix_only
        entered = (set(st_s) - set(st_p)) & suffix_only
        left = (set(st_p) - set(st_s)) & suffix_only
        changed = [w for w in both if st_p[w] != st_s[w]]
        worse = [w for w in changed
                 if RANK.get(st_s[w], 0) < RANK.get(st_p[w], 0)]
        better = [w for w in changed
                  if RANK.get(st_s[w], 0) > RANK.get(st_p[w], 0)]
        print(f"\n{model} {size}: {len(both)} suffix_only words scored in "
              f"both conditions")
        print(f"  status changed: {len(changed)}  "
              f"(worse {len(worse)}, better {len(better)}, "
              f"net {len(better) - len(worse):+d} of {len(both)})")
        print(f"  entered the scored set: {len(entered)}, "
              f"left it: {len(left)}")
        if entered:
            ent_mean = sum(RANK.get(st_s[w], 0) for w in entered) / len(entered)
            shared_mean = sum(RANK.get(st_s[w], 0) for w in both) / len(both)
            print(f"  mean suffix credit: entrants {ent_mean:.3f} vs "
                  f"shared {shared_mean:.3f}")

        for w in sorted(entered):
            rows.append({
                "model": model, "vocab_size": size, "word": w,
                "suffix": affix[w][2], "direction": "entered_scored_set",
                "status_plain": "(not scored: single token)",
                "status_scalpel": st_s[w],
                "tokens_plain": " ".join(plain_tl([w], enc)[w][1]),
                "tokens_scalpel": " ".join(info_s[w][3]),
            })
        for w in sorted(changed):
            rows.append({
                "model": model, "vocab_size": size, "word": w,
                "suffix": affix[w][2],
                "direction": ("worse" if w in worse
                              else "better" if w in better else "changed"),
                "status_plain": st_p[w], "status_scalpel": st_s[w],
                "tokens_plain": " ".join(info_p[w][3]),
                "tokens_scalpel": " ".join(info_s[w][3]),
            })

    dest = Path(args.out)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with open(dest, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)
    print(f"\nwrote {len(rows)} rows to {dest}")

    if rows:
        print("\nexamples:")
        for r in rows[:12]:
            print(f"  [{r['model']} {r['vocab_size']}] {r['word']} "
                  f"(suffix {r['suffix']!r}, {r['direction']})")
            print(f"      plain   {r['tokens_plain']}")
            print(f"      scalpel {r['tokens_scalpel']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
