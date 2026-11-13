"""Score gaBERT on MoirfEolas under both protocols, and write gabert.json.

gaBERT is the one reference row of Jane's whose back-off is not a file in her
repo, so it cannot go through moirfeolas_score.py's family table. It is also
the row that shows her three published rows do not share a protocol: it
reproduces only with all words plus --do-lower-case, where the Unigram row
reproduces only with --exclude-single-token.

Everything here is her code. This script only chooses the flags and records
the four combinations so the email and call sheet can read them rather than
quote them from memory.
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT / "external" / "tknzn4Irish"
sys.path.insert(0, str(REPO / "src"))

from criochscore.moirfeolas import load_moirfeolas            # noqa: E402
from criochscore.scoring import (                             # noqa: E402
    calculate_morphological_score, find_morphological_info,
)
from criochscore.tokenizers_io import (                       # noqa: E402
    get_token_lens, load_tokenizer, remove_specials,
)

MOIRFEOLAS = REPO / "data" / "MoirfEolas" / "MoirfEolas.csv"
RESULTS = Path.home() / "data" / "tok" / "results" / "irish" / "corrected"
MODEL = "DCU-NLP/bert-base-irish-cased-v1"

CATEGORIES = ["uru_only", "prefix_only", "suffix_only",
              "uru+prefix", "uru+suffix", "prefix+suffix", "all_affixes"]


def score_gabert(words, affix, lower, exclude_single):
    tok = load_tokenizer(MODEL, "huggingface", do_lower_case=lower)
    token_lens = remove_specials(get_token_lens(tok, words, "huggingface"), "##")
    if exclude_single:
        token_lens = {w: v for w, v in token_lens.items() if v[0] > 1}
    score_info = find_morphological_info(token_lens, affix)
    _, final, _, averages = calculate_morphological_score(score_info)
    return {
        "do_lower_case": lower,
        "exclude_single_token": exclude_single,
        "n_evaluated": len(score_info),
        "overall": final * 100,
        "categories": {k: (v * 100 if v is not None else None)
                       for k, v in averages.items()},
    }


def main():
    affix = load_moirfeolas(MOIRFEOLAS)
    words = list(affix.keys())
    print(f"Loaded {len(words)} words.")

    out = {}
    for lower in (False, True):
        for excl in (False, True):
            key = ("lower" if lower else "cased") + ("_excl" if excl else "_all")
            r = score_gabert(words, affix, lower, excl)
            out[key] = r
            cells = " / ".join(f"{v:6.2f}" for v in
                               [r["overall"]] + [r["categories"][c] for c in CATEGORIES])
            print(f"  {key:<12} {cells}   n={r['n_evaluated']}")

    RESULTS.mkdir(parents=True, exist_ok=True)
    dest = RESULTS / "gabert.json"
    dest.write_text(json.dumps(out, indent=2, ensure_ascii=False))
    print(f"\nwrote {dest}")
    print("lower_all is the combination that reproduces her published row.")


if __name__ == "__main__":
    main()
