"""Score Jane Adkins' own baseline rows with the prepared patches applied.

The patches live on branches in a separate working copy at
``~/code/tknzn4Irish-patches``; ``external/tknzn4Irish`` is never touched.
This script checks out a branch there, imports ``criochscore`` from that
copy, scores her shipped tokenizers, and writes the result.

The numbers this produces are **patched-scorer numbers**. They are not
comparable with anything else in docs/MOIRFEOLAS.md, and not with anything
she has published, because the code computing them is not the code that
computed those. They exist only to say what each patch would move.
"""
import argparse
import importlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCH_REPO = Path.home() / "code" / "tknzn4Irish-patches"
DATA = ROOT / "external" / "tknzn4Irish"
RESULTS = Path.home() / "data" / "tok" / "results" / "irish" / "corrected"

BASE_COMMIT = "0f31695"

# The rows worth checking: every SentencePiece setting, where the empty-token
# defect lives, plus UnigramLM as the control that should not move at all.
ROWS = [
    ("UnigramLM", "unigram_tokenizer_8k.json", "standard", None),
    ("SentencePieceULM", "sentencepiece_unigram_8k.model", "sentencepiece", "▁"),
    ("SentencePieceULM", "sentencepiece_unigram_16k.model", "sentencepiece", "▁"),
    ("SentencePieceULM", "sentencepiece_unigram_32k.model", "sentencepiece", "▁"),
    ("SentencePieceULM", "sentencepiece_unigram_43k.model", "sentencepiece", "▁"),
    ("SentencePieceBPE", "sentencepiece_bpe_8k.model", "sentencepiece", "▁"),
    ("Byte-BPE", "byte_level_bpe_tokenizer_8k.json", "byte-level", None),
    ("WordPiece", "wordpiece_tokenizer_8k.json", "standard", "##"),
]

CATEGORIES = ["uru_only", "prefix_only", "suffix_only",
              "uru+prefix", "uru+suffix", "prefix+suffix", "all_affixes"]


def checkout(ref):
    subprocess.run(["git", "-C", str(PATCH_REPO), "checkout", "-q", ref],
                   check=True)
    head = subprocess.run(["git", "-C", str(PATCH_REPO), "log", "-1",
                           "--format=%h %s"], capture_output=True, text=True,
                          check=True).stdout.strip()
    print(f"  {PATCH_REPO.name} at {head}")


def fresh_import():
    """Drop any already-imported criochscore so the checked-out copy loads."""
    for name in [m for m in sys.modules if m.startswith("criochscore")]:
        del sys.modules[name]
    src = str(PATCH_REPO / "src")
    while src in sys.path:
        sys.path.remove(src)
    sys.path.insert(0, src)
    return (importlib.import_module("criochscore.moirfeolas"),
            importlib.import_module("criochscore.scoring"),
            importlib.import_module("criochscore.tokenizers_io"),
            importlib.import_module("criochscore.absorption"))


def score_all():
    mf, sc, tio, ab = fresh_import()
    affix = mf.load_moirfeolas(DATA / "data" / "MoirfEolas" / "MoirfEolas.csv")
    words = list(affix.keys())

    out = {}
    for family, fname, model_type, special in ROWS:
        path = DATA / "tokenizers" / family / fname
        if not path.exists():
            continue
        tok = tio.load_tokenizer(path, model_type)
        token_lens = tio.remove_specials(
            tio.get_token_lens(tok, words, model_type), special)
        token_lens = {w: v for w, v in token_lens.items() if v[0] > 1}
        info = sc.find_morphological_info(token_lens, affix)
        _, final, _, averages = sc.calculate_morphological_score(info)
        key = f"{family}_{fname.split('_')[-1].split('.')[0]}"
        out[key] = {
            "overall": final * 100,
            "categories": {k: (v * 100 if v is not None else None)
                           for k, v in averages.items()},
            "n_evaluated": len(info),
            "n_empty_token_words": sum(
                1 for _, t in token_lens.values() if any(x == "" for x in t)),
            "absorption": ab.compute_absorption(info, affix),
        }
        print(f"    {key:<26} {out[key]['overall']:6.2f}  "
              f"n={out[key]['n_evaluated']}  "
              f"empty={out[key]['n_empty_token_words']}")
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=str(RESULTS / "patched_scorer.json"))
    args = ap.parse_args()

    refs = {
        "base": BASE_COMMIT,
        "patch_a_remove_specials": "fix-remove-specials-empty-token",
        "patch_b_special_prefixes": "fix-special-prefixes-case",
    }

    result = {}
    for label, ref in refs.items():
        print(f"\n=== {label}")
        checkout(ref)
        result[label] = score_all()

    checkout(BASE_COMMIT)

    dest = Path(args.out)
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(json.dumps(result, indent=2, ensure_ascii=False))
    print(f"\nwrote {dest}")

    print(f"\n{'row':<26} {'base':>8} {'patch a':>8} {'delta':>7} "
          f"{'patch b':>8} {'delta':>7}")
    for key in result["base"]:
        b = result["base"][key]["overall"]
        a = result["patch_a_remove_specials"][key]["overall"]
        c = result["patch_b_special_prefixes"][key]["overall"]
        print(f"{key:<26} {b:>8.2f} {a:>8.2f} {a - b:>+7.2f} "
              f"{c:>8.2f} {c - b:>+7.2f}")


if __name__ == "__main__":
    main()
