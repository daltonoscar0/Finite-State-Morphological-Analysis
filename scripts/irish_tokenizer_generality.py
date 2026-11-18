"""Is the hybrid's gain a Unigram artefact? Three conditions, three models.

Every published number so far is Unigram 8k behind the pre-tokenizer. This
scores the matched set trained by ``irish_train_tokenizers.py`` under three
conditions:

``plain``          the subword model alone, trained on the raw corpus.
``scalpel_inf``    the same model, with layer 1 in front at inference only.
                   This is what the shipped hybrid does.
``scalpel_train``  layer 1 applied to the corpus before training as well as
                   at inference, so the mutation markers are candidate pieces
                   and the model never has to spell a boundary it cannot see.

The third condition exists because eight of the sixteen markers layer 1 emits
are absent from every vocabulary trained on raw text, so under
``scalpel_inf`` a real encoder re-splits them. Training on the pre-tokenized
corpus is the fix, and the point of the comparison is how much it is worth.

Reported per cell: every CríochScore category, fertility (mean tokens per
word over the scored set) and Jane's absorption rates. Both protocols:
hers (--exclude-single-token) and all-words.
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
from moirfeolas_score import (                                   # noqa: E402
    CATEGORIES, MOIRFEOLAS, exclude_single, score,
)

from criochscore.moirfeolas import load_moirfeolas               # noqa: E402
from criochscore.tokenizers_io import (                          # noqa: E402
    get_token_lens, load_tokenizer, remove_specials,
)

MODELS_DIR = Path.home() / "data" / "tok" / "results" / "irish" / "stage3" / "models"
DEFAULT_CSV = ROOT / "results" / "irish" / "tokenizer_generality.csv"
DEFAULT_JSON = ROOT / "results" / "irish" / "tokenizer_generality.json"

# Continuation marker per model, so remove_specials lines the pieces up with
# plain text the same way it does for Jane's models.
SPECIAL = {"unigram": None, "bpe": None, "wordpiece": "##"}

CONDITIONS = ["plain", "scalpel_inf", "scalpel_train"]

# The three absorption rates worth carrying into the table. Her script
# reports six; these are the "fully absorbed" ones, one per affix type, which
# are the ones a mutation-aware pre-tokenizer should move. Averaging them
# would be meaningless, so they stay separate.
ABSORPTION_KEYS = {
    "uru_absorbed": "uru_absorbed_%",
    "prefix_absorbed": "prefix_fully_absorbed_%",
    "suffix_absorbed": "suffix_fully_absorbed_%",
}

FIELDS = (["condition", "model", "vocab_size", "n_evaluated", "overall"]
          + CATEGORIES + ["fertility"] + list(ABSORPTION_KEYS)
          + ["protocol"])


def encoder(path, special):
    tok = load_tokenizer(path, "standard")

    def encode(ws):
        lens = remove_specials(get_token_lens(tok, list(ws), "standard"),
                               special)
        return {w: list(v[1]) for w, v in lens.items()}

    return encode


def layered(words, encode, split):
    to_encode = sorted({split[w][2] if w in split else w for w in words})
    enc = encode(to_encode)
    out = {}
    for w in words:
        if w in split:
            _, marker, base = split[w]
            out[w] = [marker] + enc[base]
        else:
            out[w] = enc[w]
    return {w: (len(t), t) for w, t in out.items()}


def plain_tl(words, encode):
    return {w: (len(t), t) for w, t in encode(words).items()}


def fertility(token_lens):
    if not token_lens:
        return None
    return sum(n for n, _ in token_lens.values()) / len(token_lens)


def absorption(result):
    a = result.get("absorption") or {}
    return {name: (round(a[key], 2) if isinstance(a.get(key), (int, float))
                   else None)
            for name, key in ABSORPTION_KEYS.items()}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--models-dir", default=str(MODELS_DIR))
    ap.add_argument("--moirfeolas", default=str(MOIRFEOLAS))
    ap.add_argument("--out-csv", default=str(DEFAULT_CSV))
    ap.add_argument("--out-json", default=str(DEFAULT_JSON))
    args = ap.parse_args()

    manifest_path = Path(args.models_dir) / "manifest.json"
    if not manifest_path.exists():
        raise SystemExit(f"no manifest at {manifest_path}; train first")
    manifest = json.loads(manifest_path.read_text())

    affix = load_moirfeolas(args.moirfeolas)
    words = list(affix)
    skipped = []
    demut = demutate_all(words, skipped=skipped)
    demut, n_clitic = add_clitic_readings(words, demut)
    splits = mutation_splits(words, demut, (CLITIC_TAG,) + SPLIT_TAGS)
    print(f"{len(words)} words; layer 1 fires on {len(splits)}; "
          f"{n_clitic} clitic; {len(skipped)} set aside {skipped}")

    cells = {}
    for name, meta in manifest.items():
        cells.setdefault((meta["model"], meta["vocab_size"]), {})[
            meta["condition"]] = Path(meta["path"])

    rows, out = [], {}
    hdr = (f"{'condition':<15}{'model':<10}{'size':>7}{'prot':>5}"
           f"{'overall':>9}{'fert':>7}" +
           "".join(f"{c[:6]:>8}" for c in CATEGORIES))
    print("\n" + hdr)

    for (model, size) in sorted(cells):
        special = SPECIAL[model]
        paths = cells[(model, size)]
        if "plain" not in paths:
            continue
        enc_plain = encoder(paths["plain"], special)
        enc_scal = (encoder(paths["scalpel"], special)
                    if "scalpel" in paths else None)

        for cond in CONDITIONS:
            if cond == "plain":
                tl_full = plain_tl(words, enc_plain)
            elif cond == "scalpel_inf":
                tl_full = layered(words, enc_plain, splits)
            else:
                if enc_scal is None:
                    continue
                tl_full = layered(words, enc_scal, splits)

            for protocol in ("her", "all"):
                tl = tl_full if protocol == "all" else exclude_single(tl_full)
                r = score(tl, affix, f"{cond} {model} {size} {protocol}")
                fert = fertility(tl)
                row = {
                    "condition": cond, "model": model, "vocab_size": size,
                    "n_evaluated": r["n_evaluated"],
                    "overall": round(r["overall"], 2),
                    "fertility": round(fert, 3) if fert else None,
                    "protocol": protocol,
                }
                row.update(absorption(r))
                for c in CATEGORIES:
                    v = r["categories"][c]
                    row[c] = round(v, 2) if v is not None else None
                rows.append(row)
                out[f"{cond}_{model}_{size}_{protocol}"] = {
                    k: v for k, v in r.items()
                    if not k.startswith("_")
                }
                if protocol == "her":
                    print(f"{cond:<15}{model:<10}{size:>7}{protocol:>5}"
                          f"{row['overall']:>9.2f}{row['fertility']:>7.2f}" +
                          "".join(
                              f"{(row[c] if row[c] is not None else 0):>8.2f}"
                              for c in CATEGORIES))

    dest = Path(args.out_csv)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with open(dest, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)
    print(f"\nwrote {dest} ({len(rows)} rows)")

    Path(args.out_json).write_text(
        json.dumps(out, indent=2, ensure_ascii=False))
    print(f"wrote {args.out_json}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
